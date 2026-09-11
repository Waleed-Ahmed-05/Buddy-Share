#ifndef BUDDYSHARE_TESTS_SUPPORT_FAKES_H
#define BUDDYSHARE_TESTS_SUPPORT_FAKES_H

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/RoleManager.h"
#include "reader/ReaderViewerService.h"
#include "shared/Console.h"
#include "shared/GitHubClient.h"
#include "writer/GitProcess.h"

// Test doubles shared across the GoogleTest suites for spec 001. These are fakes, not mocks:
// they hold state and canned answers so tests can drive RoleManager/GitBootstrapService
// without touching real stdin, the real %APPDATA%, a real git binary, or the network (per the
// project's cpp-testing skill: isolate unit tests from those dependencies).

// Records every prompt/print call and hands back queued canned answers, in order.
class FakeConsole : public buddyshare::shared::IConsole {
public:
    struct Call {
        std::string message;
        bool masked{false};
    };

    void queue_answer(std::string answer) { answers_.push_back(std::move(answer)); }

    std::string prompt(const std::string& message) override { return next_answer(message, false); }

    std::string prompt_masked(const std::string& message) override {
        return next_answer(message, true);
    }

    // Spec 005: style defaults to plain so every pre-005 call site (and every pre-005 test that
    // never mentions MessageStyle) keeps behaving exactly as before -- printed_/all_output()
    // record message text only, unchanged. style_calls() additionally records the style each
    // call used, so new spec-005 tests can assert on it without decoding literal ANSI bytes.
    void print(const std::string& message,
               buddyshare::shared::MessageStyle style = buddyshare::shared::MessageStyle::plain)
        override {
        printed_.push_back(message);
        style_calls_.push_back({message, style});
    }

    int prompt_call_count() const { return static_cast<int>(calls_.size()); }
    const std::vector<Call>& calls() const { return calls_; }

    struct StyledCall {
        std::string message;
        buddyshare::shared::MessageStyle style;
    };
    const std::vector<StyledCall>& style_calls() const { return style_calls_; }

    std::string all_output() const {
        std::string joined;
        for (const auto& line : printed_) {
            joined += line;
            joined += '\n';
        }
        return joined;
    }

private:
    std::string next_answer(const std::string& message, bool masked) {
        calls_.push_back({message, masked});
        if (answers_.empty()) return "";
        const std::string answer = answers_.front();
        answers_.pop_front();
        return answer;
    }

    std::deque<std::string> answers_;
    std::vector<Call> calls_;
    std::vector<std::string> printed_;
    std::vector<StyledCall> style_calls_;
};

// In-memory stand-in for the %APPDATA%\BuddyShare\.role file.
class FakeRoleStore : public buddyshare::core::IRoleStore {
public:
    explicit FakeRoleStore(std::optional<std::string> initial_contents)
        : contents_(std::move(initial_contents)) {}

    std::optional<std::string> read() const override { return contents_; }

    void write(const std::string& role_text) override {
        contents_ = role_text;
        ++write_count_;
        last_written_ = role_text;
    }

    int write_count() const { return write_count_; }
    const std::string& last_written() const { return last_written_; }

private:
    std::optional<std::string> contents_;
    int write_count_{0};
    std::string last_written_;
};

// Canned GitHub API responses; records the arguments of every call.
class FakeGitHubClient : public buddyshare::shared::IGitHubClient {
public:
    void set_response(buddyshare::shared::RepoInfo response) { response_ = std::move(response); }

    buddyshare::shared::RepoInfo get_repo(const std::string& owner, const std::string& repo,
                                           const std::string& token) override {
        ++call_count_;
        last_owner_ = owner;
        last_repo_ = repo;
        last_token_ = token;
        return response_;
    }

    int call_count() const { return call_count_; }
    const std::string& last_owner() const { return last_owner_; }
    const std::string& last_repo() const { return last_repo_; }
    const std::string& last_token() const { return last_token_; }

    // Canned Contents API responses (spec 004), keyed by the exact `path` argument. A path
    // with no canned response defaults to a 404 (not_found) -- matching a real repo that simply
    // doesn't have that file/folder yet.
    void set_contents_response(const std::string& path, buddyshare::shared::ContentsResult response) {
        contents_by_path_[path] = std::move(response);
    }

    buddyshare::shared::ContentsResult get_contents(const std::string& owner,
                                                     const std::string& repo,
                                                     const std::string& path) override {
        ++contents_call_count_;
        last_contents_owner_ = owner;
        last_contents_repo_ = repo;
        contents_paths_requested_.push_back(path);
        const auto it = contents_by_path_.find(path);
        if (it != contents_by_path_.end()) return it->second;
        buddyshare::shared::ContentsResult default_not_found;
        default_not_found.not_found = true;
        return default_not_found;
    }

    int contents_call_count() const { return contents_call_count_; }
    const std::vector<std::string>& contents_paths_requested() const {
        return contents_paths_requested_;
    }
    const std::string& last_contents_owner() const { return last_contents_owner_; }
    const std::string& last_contents_repo() const { return last_contents_repo_; }

private:
    buddyshare::shared::RepoInfo response_;
    int call_count_{0};
    std::string last_owner_;
    std::string last_repo_;
    std::string last_token_;

    std::map<std::string, buddyshare::shared::ContentsResult> contents_by_path_;
    int contents_call_count_{0};
    std::vector<std::string> contents_paths_requested_;
    std::string last_contents_owner_;
    std::string last_contents_repo_;
};

// In-memory stand-in for the %APPDATA%\BuddyShare\keys\<username>_private.pem files (spec 004),
// so tests never touch the real filesystem/DPAPI.
class FakePrivateKeyStore : public buddyshare::reader::IPrivateKeyStore {
public:
    // Pre-populates a key as if a previous run had already generated/stored it.
    void seed(const std::string& username, const std::string& private_key_pem) {
        keys_[username] = private_key_pem;
    }

    std::optional<std::string> read_private_key_pem(const std::string& username) const override {
        const auto it = keys_.find(username);
        if (it == keys_.end()) return std::nullopt;
        return it->second;
    }

    void write_private_key_pem(const std::string& username,
                                const std::string& private_key_pem) override {
        keys_[username] = private_key_pem;
        ++write_count_;
        last_written_username_ = username;
    }

    int write_count() const { return write_count_; }
    const std::string& last_written_username() const { return last_written_username_; }

private:
    std::map<std::string, std::string> keys_;
    int write_count_{0};
    std::string last_written_username_;
};

// Records every `git <args...>` invocation instead of touching a real git binary.
class FakeGitProcess : public buddyshare::writer::IGitProcess {
public:
    void queue_availability(bool available) { availability_answers_.push_back(available); }
    void set_install_result(bool succeeded) { install_result_ = succeeded; }

    void set_result_for(const std::string& subcommand, buddyshare::writer::GitResult result) {
        results_by_subcommand_[subcommand] = std::move(result);
    }

    // Matches any invocation with an argument containing `substring`, checked before the
    // subcommand-keyed lookup above. Lets a test target one specific command even when two
    // different invocations share the same subcommand (e.g. GitBootstrapService's `branch -M
    // main` vs. its later `branch --set-upstream-to=origin/main main`).
    void set_result_for_arg_containing(const std::string& substring,
                                        buddyshare::writer::GitResult result) {
        results_by_arg_substring_.emplace_back(substring, std::move(result));
    }

    bool is_git_available() override {
        if (availability_answers_.empty()) return true;
        const bool answer = availability_answers_.front();
        availability_answers_.pop_front();
        return answer;
    }

    bool install_git_via_winget() override {
        ++install_call_count_;
        return install_result_;
    }

    buddyshare::writer::GitResult run(const std::vector<std::string>& args) override {
        recorded_commands_.push_back(args);
        for (const auto& [substring, result] : results_by_arg_substring_) {
            const bool matches = std::any_of(args.begin(), args.end(), [&](const std::string& a) {
                return a.find(substring) != std::string::npos;
            });
            if (matches) return result;
        }
        if (!args.empty()) {
            const auto it = results_by_subcommand_.find(args[0]);
            if (it != results_by_subcommand_.end()) return it->second;
        }
        return buddyshare::writer::GitResult{0, "", ""};
    }

    const std::vector<std::vector<std::string>>& recorded_commands() const {
        return recorded_commands_;
    }
    int install_call_count() const { return install_call_count_; }

private:
    std::deque<bool> availability_answers_;
    bool install_result_{true};
    int install_call_count_{0};
    std::vector<std::vector<std::string>> recorded_commands_;
    std::map<std::string, buddyshare::writer::GitResult> results_by_subcommand_;
    std::vector<std::pair<std::string, buddyshare::writer::GitResult>> results_by_arg_substring_;
};

#endif  // BUDDYSHARE_TESTS_SUPPORT_FAKES_H
