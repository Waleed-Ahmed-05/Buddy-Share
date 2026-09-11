#include "writer/GitBootstrapService.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace buddyshare::writer {

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 2> kGitignoreEntries = {"chapters-source/", "*.exe"};

// Redacts every occurrence of token (when non-empty) from text before it is stored in a
// BootstrapResult or printed, so a token echoed back by git in an error message never leaks.
std::string redact(std::string text, const std::string& token) {
    if (token.empty()) return text;
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.replace(pos, token.size(), "***");
        pos += 3;
    }
    return text;
}

// True if text contains no non-whitespace characters (including the case of being empty).
bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// Prompts on console until a non-blank answer is given (username/email must not be blank;
// whitespace-only input counts as blank, not as a valid identity).
std::string prompt_non_blank(shared::IConsole& console, const std::string& message) {
    while (true) {
        std::string answer = console.prompt(message);
        if (!is_blank(answer)) return answer;
    }
}

// Ensures the .gitignore in dir contains every entry in kGitignoreEntries, creating the file if
// needed or appending to it without disturbing any existing entries. Each entry is checked and
// appended independently, so an entry already present (from a prior run or hand-edited by the
// user) is never duplicated while any other missing entry still gets added.
void seed_gitignore(const fs::path& dir) {
    const fs::path path = dir / ".gitignore";
    std::string existing;
    if (fs::exists(path)) {
        std::ifstream in(path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        existing = buffer.str();
    }

    std::vector<std::string_view> missing_entries;
    for (const std::string_view entry : kGitignoreEntries) {
        if (existing.find(entry) == std::string::npos) missing_entries.push_back(entry);
    }
    if (missing_entries.empty()) return;

    std::ofstream out(path, std::ios::app);
    if (!existing.empty() && existing.back() != '\n') out << '\n';
    for (const std::string_view entry : missing_entries) {
        out << entry << '\n';
    }
}

// Makes sure git is on this machine, installing it via winget if it's missing. Returns a
// failure result if git still isn't available afterward; returns std::nullopt to mean
// "all good, keep going".
std::optional<BootstrapResult> ensure_git_installed(IGitProcess& git_process,
                                                     shared::IConsole& console) {
    if (git_process.is_git_available()) return std::nullopt;

    const bool installed = git_process.install_git_via_winget();
    if (installed && git_process.is_git_available()) return std::nullopt;

    const std::string message =
        "Git is not installed and automatic installation via winget failed. Install "
        "Git manually from https://git-scm.com/downloads and try again.";
    console.print(message, shared::MessageStyle::error);
    return BootstrapResult{BootstrapOutcome::git_unavailable, message, "", ""};
}

// Checks the GitHub repo lookup for the problems that must abort before any git command runs.
// Returns a failure result for the first problem found; returns std::nullopt if the repo is
// safe to push to.
std::optional<BootstrapResult> validate_repo_info(const shared::RepoInfo& info,
                                                   const std::string& username,
                                                   const std::string& repo,
                                                   shared::IConsole& console) {
    if (info.network_error) {
        const std::string message =
            "Network error while validating the repository: " + info.error_message;
        console.print(message, shared::MessageStyle::error);
        return BootstrapResult{BootstrapOutcome::network_error, message, "", ""};
    }
    if (info.unauthorized) {
        const std::string message =
            "The provided token was rejected (401 Unauthorized). Check that it is still valid.";
        console.print(message, shared::MessageStyle::error);
        return BootstrapResult{BootstrapOutcome::unauthorized, message, "", ""};
    }
    if (info.ambiguous_not_found) {
        const std::string message =
            "Repository " + username + "/" + repo +
            " was not found. Create it first at https://github.com/new (this may also mean "
            "the repo is private and no token was given).";
        console.print(message, shared::MessageStyle::error);
        return BootstrapResult{BootstrapOutcome::repo_not_found, message, "", ""};
    }
    if (info.has_commits) {
        const std::string message = "Remote repository " + username + "/" + repo +
                                     " is not empty; this action will not overwrite existing "
                                     "history.";
        console.print(message, shared::MessageStyle::error);
        return BootstrapResult{BootstrapOutcome::remote_not_empty, message, "", ""};
    }
    return std::nullopt;
}

}  // namespace

std::string normalize_repo_name(const std::string& input) {
    std::string name = input;
    const std::size_t last_slash = name.find_last_of('/');
    if (last_slash != std::string::npos) {
        name = name.substr(last_slash + 1);
    }
    constexpr std::string_view git_suffix = ".git";
    if (name.size() > git_suffix.size() &&
        name.compare(name.size() - git_suffix.size(), git_suffix.size(), git_suffix) == 0) {
        name.erase(name.size() - git_suffix.size());
    }
    return name;
}

GitBootstrapService::GitBootstrapService(std::string working_directory,
                                          shared::IGitHubClient& github_client,
                                          IGitProcess& git_process, shared::IConsole& console)
    : working_directory_(std::move(working_directory)),
      github_client_(github_client),
      git_process_(git_process),
      console_(console) {}

bool GitBootstrapService::is_already_bootstrapped() const {
    return fs::exists(fs::path(working_directory_) / ".git");
}

BootstrapResult GitBootstrapService::run() {
    if (is_already_bootstrapped()) {
        console_.print(
            "A git repository already exists in this folder; this bootstrap only runs once.",
            shared::MessageStyle::info);
        return BootstrapResult{BootstrapOutcome::already_bootstrapped, "", "", ""};
    }

    if (auto failure = ensure_git_installed(git_process_, console_)) return *failure;

    seed_gitignore(fs::path(working_directory_));

    const std::string username = prompt_non_blank(console_, "GitHub username:");
    const std::string email = prompt_non_blank(console_, "Email:");
    const std::string repo = normalize_repo_name(
        console_.prompt("Repo name (must already exist on GitHub, empty, e.g. \"Buddy-Share\"):"));
    const std::string token =
        console_.prompt_masked("Personal access token (optional, press Enter to skip):");

    const shared::RepoInfo info = github_client_.get_repo(username, repo, token);
    if (auto failure = validate_repo_info(info, username, repo, console_)) return *failure;

    const std::string& branch = info.default_branch;
    const std::string repo_url = "https://github.com/" + username + "/" + repo + ".git";

    git_process_.run({"init"});
    git_process_.run({"branch", "-M", branch});
    git_process_.run({"config", "--local", "user.name", username});
    git_process_.run({"config", "--local", "user.email", email});
    git_process_.run({"add", "."});
    git_process_.run({"commit", "-m", "Initial commit"});
    git_process_.run({"remote", "add", "origin", repo_url});

    console_.print("Pushing to GitHub...", shared::MessageStyle::info);

    GitResult push_result;
    bool upstream_tracking_confirmed = true;
    if (token.empty()) {
        push_result = git_process_.run({"push", "-u", "origin", branch});
    } else {
        push_result = git_process_.run(
            {"push", "https://" + token + "@github.com/" + username + "/" + repo + ".git",
             "HEAD:" + branch});
        if (push_result.exit_code == 0) {
            const GitResult upstream_result =
                git_process_.run({"branch", "--set-upstream-to=origin/" + branch, branch});
            upstream_tracking_confirmed = upstream_result.exit_code == 0;
        }
    }

    if (push_result.exit_code != 0) {
        std::string message = push_result.std_err.empty() ? push_result.std_out : push_result.std_err;
        message = redact(message, token);
        console_.print(message, shared::MessageStyle::error);
        return BootstrapResult{BootstrapOutcome::push_failed, message, "", ""};
    }

    // The push itself succeeded either way; only the follow-up tracking command can fail, and
    // that must never be reported as "now tracks origin/<branch>" when it didn't (bugfix).
    const std::string success_message =
        upstream_tracking_confirmed
            ? "Pushed to " + repo_url + " (branch " + branch + "); local " + branch +
                  " now tracks origin/" + branch + "."
            : "Pushed to " + repo_url + " (branch " + branch +
                  "); the push succeeded, but branch tracking could not be confirmed. You may "
                  "need to run `git push -u origin " +
                  branch + "` manually before your next encrypt/edit-access action.";
    console_.print(success_message, shared::MessageStyle::success);
    return BootstrapResult{BootstrapOutcome::success, success_message, repo_url, branch};
}

}  // namespace buddyshare::writer
