#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "support/Fakes.h"
#include "writer/GitBootstrapService.h"

using buddyshare::shared::MessageStyle;
using buddyshare::shared::RepoInfo;
using buddyshare::writer::BootstrapOutcome;
using buddyshare::writer::GitBootstrapService;
using buddyshare::writer::GitResult;
using buddyshare::writer::normalize_repo_name;

namespace fs = std::filesystem;

namespace {

class GitBootstrapServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_bootstrap_test_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    // Configures github_client_ for a fully valid, empty, existing repo and queues valid
    // console answers (username, email, repo name, blank token) for the happy path.
    void arrange_happy_path() {
        RepoInfo info;
        info.exists = true;
        info.default_branch = "main";
        info.has_commits = false;
        github_client_.set_response(info);
        console_.queue_answer("alice");
        console_.queue_answer("alice@example.com");
        console_.queue_answer("myrepo");
        console_.queue_answer("");
    }

    std::string read_gitignore() const {
        std::ifstream gitignore(working_dir_ / ".gitignore");
        std::stringstream contents;
        contents << gitignore.rdbuf();
        return contents.str();
    }

    fs::path working_dir_;
    FakeGitHubClient github_client_;
    FakeGitProcess git_process_;
    FakeConsole console_;
};

// --- Step 1: startup check (AC4) ---------------------------------------------------------

TEST_F(GitBootstrapServiceTest, IsAlreadyBootstrapped_TrueWhenDotGitDirectoryExists) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    EXPECT_TRUE(service.is_already_bootstrapped());
}

TEST_F(GitBootstrapServiceTest, IsAlreadyBootstrapped_FalseForFreshFolder) {
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    EXPECT_FALSE(service.is_already_bootstrapped());
}

TEST_F(GitBootstrapServiceTest, Run_WhenAlreadyBootstrapped_NoOpsAndReturnsAlreadyBootstrapped) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::already_bootstrapped);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
    EXPECT_EQ(console_.prompt_call_count(), 0);
    EXPECT_EQ(github_client_.call_count(), 0);
}

// --- Step 2: git availability -------------------------------------------------------------

TEST_F(GitBootstrapServiceTest, Run_GitMissingAndWingetFails_AbortsWithManualInstallMessage) {
    git_process_.queue_availability(false);
    git_process_.set_install_result(false);
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::git_unavailable);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(console_.prompt_call_count(), 0);
    EXPECT_EQ(github_client_.call_count(), 0);
}

TEST_F(GitBootstrapServiceTest, Run_GitMissingThenInstalledViaWinget_ContinuesBootstrap) {
    git_process_.queue_availability(false);
    git_process_.queue_availability(true);
    git_process_.set_install_result(true);
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    EXPECT_EQ(git_process_.install_call_count(), 1);
}

// --- Step 3: .gitignore seeding (AC9) -------------------------------------------------------

TEST_F(GitBootstrapServiceTest, Run_SeedsGitignoreWithChaptersSourceAndExeWhenMissing) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const std::string text = read_gitignore();
    EXPECT_NE(text.find("chapters-source/"), std::string::npos);
    EXPECT_NE(text.find("*.exe"), std::string::npos);
}

TEST_F(GitBootstrapServiceTest,
       Run_AppendsChaptersSourceToExistingGitignoreWithoutRemovingOtherEntries) {
    {
        std::ofstream existing(working_dir_ / ".gitignore");
        existing << "node_modules/\n";
    }
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const std::string text = read_gitignore();
    EXPECT_NE(text.find("node_modules/"), std::string::npos);
    EXPECT_NE(text.find("chapters-source/"), std::string::npos);
}

TEST_F(GitBootstrapServiceTest, Run_DoesNotDuplicateGitignoreEntryIfAlreadyPresent) {
    {
        std::ofstream existing(working_dir_ / ".gitignore");
        existing << "chapters-source/\n";
    }
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const std::string text = read_gitignore();
    const size_t first = text.find("chapters-source/");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(text.find("chapters-source/", first + 1), std::string::npos);
}

// Edge case: the user (or a prior run) already hand-added *.exe but not chapters-source/ --
// the existing entry must not be duplicated, while the still-missing entry gets appended.
TEST_F(GitBootstrapServiceTest, Run_OnlyAppendsGitignoreEntriesStillMissing_DoesNotDuplicateExisting) {
    {
        std::ofstream existing(working_dir_ / ".gitignore");
        existing << "*.exe\n";
    }
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const std::string text = read_gitignore();
    const size_t first_exe = text.find("*.exe");
    ASSERT_NE(first_exe, std::string::npos);
    EXPECT_EQ(text.find("*.exe", first_exe + 1), std::string::npos)
        << "*.exe was already present and must not be duplicated";
    EXPECT_NE(text.find("chapters-source/"), std::string::npos)
        << "chapters-source/ was still missing and must be appended";
}

TEST_F(GitBootstrapServiceTest, Run_GitignoreSeededBeforeGitAddIsInvoked) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const auto& commands = git_process_.recorded_commands();
    const auto add_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add";
    });
    ASSERT_NE(add_it, commands.end());
    EXPECT_NE(read_gitignore().find("chapters-source/"), std::string::npos);
}

// --- Step 4: prompts -----------------------------------------------------------------------

TEST_F(GitBootstrapServiceTest, Run_PromptsForUsernameEmailRepoTokenInOrder) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    ASSERT_EQ(console_.calls().size(), 4u);
    EXPECT_FALSE(console_.calls()[0].masked);
    EXPECT_FALSE(console_.calls()[1].masked);
    EXPECT_FALSE(console_.calls()[2].masked);
    EXPECT_TRUE(console_.calls()[3].masked);
}

TEST_F(GitBootstrapServiceTest, Run_BlankUsername_RepromptsRatherThanCommittingEmptyIdentity) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("");
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    EXPECT_EQ(github_client_.last_owner(), "alice");
}

TEST_F(GitBootstrapServiceTest, Run_BlankEmail_RepromptsRatherThanCommittingEmptyIdentity) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    const auto& commands = git_process_.recorded_commands();
    const auto email_cmd = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return std::find(args.begin(), args.end(), "user.email") != args.end();
    });
    ASSERT_NE(email_cmd, commands.end());
    EXPECT_NE(std::find(email_cmd->begin(), email_cmd->end(), "alice@example.com"),
              email_cmd->end());
}

// Repo-name normalization (used during step 4c prompting).
TEST(NormalizeRepoNameTest, StripsTrailingDotGitSuffix) {
    EXPECT_EQ(normalize_repo_name("myrepo.git"), "myrepo");
}

TEST(NormalizeRepoNameTest, StripsFullUrlWithDotGitSuffix) {
    EXPECT_EQ(normalize_repo_name("https://github.com/alice/myrepo.git"), "myrepo");
}

TEST(NormalizeRepoNameTest, StripsFullUrlWithoutDotGitSuffix) {
    EXPECT_EQ(normalize_repo_name("https://github.com/alice/myrepo"), "myrepo");
}

TEST(NormalizeRepoNameTest, LeavesBareNameUnchanged) {
    EXPECT_EQ(normalize_repo_name("myrepo"), "myrepo");
}

TEST_F(GitBootstrapServiceTest, Run_RepoNameTypedAsFullUrl_NormalizesBeforeValidation) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("https://github.com/alice/myrepo.git");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    EXPECT_EQ(github_client_.last_repo(), "myrepo");
}

// --- Step 5: validation errors (AC5, AC6) ---------------------------------------------------

TEST_F(GitBootstrapServiceTest, Run_RepoNotFound_AbortsWithAmbiguousNotFoundMessage) {
    RepoInfo info;
    info.ambiguous_not_found = true;
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::repo_not_found);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
    EXPECT_NE(result.message.find("github.com/new"), std::string::npos);
}

TEST_F(GitBootstrapServiceTest, Run_UnauthorizedToken_DistinctFromNotFoundAndNoGitOps) {
    RepoInfo info;
    info.unauthorized = true;
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_expired");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::unauthorized);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
    EXPECT_NE(result.message, "");
}

TEST_F(GitBootstrapServiceTest, Run_NetworkErrorDuringValidation_ReturnsSpecificErrorNoGitOps) {
    RepoInfo info;
    info.network_error = true;
    info.error_message = "connection reset by peer";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::network_error);
    EXPECT_NE(result.message.find("connection reset by peer"), std::string::npos);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
}

TEST_F(GitBootstrapServiceTest, Run_RemoteAlreadyHasCommits_AbortsBeforeAnyGitCommand) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    info.has_commits = true;
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::remote_not_empty);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
}

// --- Steps 6-7: happy path command sequence + outcome (AC3) ---------------------------------

TEST_F(GitBootstrapServiceTest, Run_HappyPathNoToken_RunsExpectedCommandSequenceAndSucceeds) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    EXPECT_EQ(result.branch, "main");
    EXPECT_NE(result.repo_url.find("alice/myrepo"), std::string::npos);

    const auto& commands = git_process_.recorded_commands();
    ASSERT_GE(commands.size(), 7u);
    EXPECT_EQ(commands[0][0], "init");
    EXPECT_EQ(commands[1][0], "branch");
    EXPECT_EQ(commands[2][0], "config");
    EXPECT_EQ(commands[3][0], "config");
    EXPECT_EQ(commands[4][0], "add");
    EXPECT_EQ(commands[4][1], ".");
    EXPECT_EQ(commands[5][0], "commit");
    EXPECT_EQ(commands[6][0], "remote");
}

TEST_F(GitBootstrapServiceTest, Run_NoToken_PushHasNoEmbeddedCredentials) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    const auto& commands = git_process_.recorded_commands();
    const auto push_cmd = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "push";
    });
    ASSERT_NE(push_cmd, commands.end());
    for (const auto& arg : *push_cmd) {
        EXPECT_EQ(arg.find('@'), std::string::npos) << "no-token push must not embed credentials";
    }
}

// --- Token handling / security (AC7) ---------------------------------------------------------

TEST_F(GitBootstrapServiceTest, Run_RemoteAddUsesTokenFreeUrlEvenWhenTokenWasProvided) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const auto& commands = git_process_.recorded_commands();
    const auto remote_cmd = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "remote";
    });
    ASSERT_NE(remote_cmd, commands.end());
    for (const auto& arg : *remote_cmd) {
        EXPECT_EQ(arg.find("ghp_supersecret"), std::string::npos);
    }
}

TEST_F(GitBootstrapServiceTest, Run_WithToken_PushUsesTokenOnceThenSetsUpstreamToOrigin) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success);
    const auto& commands = git_process_.recorded_commands();
    int push_count = 0;
    bool found_token_push = false;
    bool found_set_upstream = false;
    for (const auto& args : commands) {
        if (!args.empty() && args[0] == "push") {
            ++push_count;
            for (const auto& arg : args) {
                if (arg.find("ghp_supersecret@github.com") != std::string::npos) {
                    found_token_push = true;
                }
            }
        }
        if (!args.empty() && args[0] == "branch" &&
            std::find(args.begin(), args.end(), "--set-upstream-to=origin/main") != args.end()) {
            found_set_upstream = true;
        }
    }
    EXPECT_EQ(push_count, 1);
    EXPECT_TRUE(found_token_push);
    EXPECT_TRUE(found_set_upstream);
}

TEST_F(GitBootstrapServiceTest, Run_TokenNeverAppearsInAnyConsoleOutput) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    EXPECT_EQ(console_.all_output().find("ghp_supersecret"), std::string::npos);
}

TEST_F(GitBootstrapServiceTest, Run_GitEchoesTokenUrlOnPushFailure_RedactedBeforePrinting) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    git_process_.set_result_for(
        "push", GitResult{128, "",
                           "fatal: unable to access "
                           "'https://ghp_supersecret@github.com/alice/myrepo.git/': "
                           "Could not resolve host"});
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::push_failed);
    EXPECT_EQ(result.message.find("ghp_supersecret"), std::string::npos);
    EXPECT_EQ(console_.all_output().find("ghp_supersecret"), std::string::npos);
}

// --- Push failure / retry behavior (AC8, edge case: no auto-retry) ---------------------------

TEST_F(GitBootstrapServiceTest, Run_PushFails_ReturnsGitErrorVerbatimWithNoRetry) {
    arrange_happy_path();
    git_process_.set_result_for("push", GitResult{1, "", "remote: Permission denied"});
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::push_failed);
    EXPECT_NE(result.message.find("remote: Permission denied"), std::string::npos);
    int push_count = 0;
    for (const auto& args : git_process_.recorded_commands()) {
        if (!args.empty() && args[0] == "push") ++push_count;
    }
    EXPECT_EQ(push_count, 1);
}

// --- Silent git-failure fix: `branch --set-upstream-to` result must be checked (bugfix) ------

// The push itself succeeded, but the follow-up `branch --set-upstream-to=origin/<branch>
// <branch>` failed. That must not be reported as "now tracks origin/<branch>" -- the outcome is
// still success (the push did succeed), but the message must be honest about tracking being
// unconfirmed and tell the writer how to fix it manually.
TEST_F(GitBootstrapServiceTest,
       Run_WithToken_SetUpstreamToFails_StillSucceedsButMessageIsHonestAboutTracking) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    git_process_.set_result_for_arg_containing(
        "--set-upstream-to", GitResult{1, "", "error: could not set upstream"});
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::success)
        << "the push itself succeeded; only the follow-up tracking command failed";
    EXPECT_NE(result.message.find("branch tracking could not be confirmed"), std::string::npos);
    EXPECT_NE(result.message.find("git push -u origin main"), std::string::npos)
        << "must tell the writer the manual command to run before the next encrypt/edit-access "
           "action";
    EXPECT_EQ(result.message.find("now tracks origin"), std::string::npos)
        << "must not claim tracking succeeded when it didn't";
    EXPECT_NE(console_.all_output().find("branch tracking could not be confirmed"),
              std::string::npos)
        << "the honest message must actually be printed, not just returned";
}

// --- Prompt UX pass (cosmetic wording, no behavior change) -----------------------------------

TEST_F(GitBootstrapServiceTest, Run_RepoNamePrompt_UsesFriendlyWordingWithExample) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    ASSERT_GE(console_.calls().size(), 3u);
    EXPECT_EQ(console_.calls()[2].message,
              "Repo name (must already exist on GitHub, empty, e.g. \"Buddy-Share\"):");
}

// Edge case: whole folder minus gitignored paths is staged -- `git add .`, not a file list.
TEST_F(GitBootstrapServiceTest, Run_GitAddStagesWholeFolderRelyingOnGitignoreForExclusions) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    const auto& commands = git_process_.recorded_commands();
    const auto add_cmd = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add";
    });
    ASSERT_NE(add_cmd, commands.end());
    ASSERT_EQ(add_cmd->size(), 2u);
    EXPECT_EQ((*add_cmd)[1], ".");
}

// --- Spec 005: CLI output styling -----------------------------------------------------------
//
// Behavior item 5 requires an info-styled "before" status line immediately preceding the
// git init/commit/push sequence, paired with the existing "after" result message now styled
// per the categorization rule (item 6): success for the push-succeeded message,
// error for every terminal-failure message.

TEST_F(GitBootstrapServiceTest,
       Run_HappyPath_PrintsInfoStatusLineBeforePushThenStylesSuccessMessageSuccess) {
    arrange_happy_path();
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::success);
    const auto& styles = console_.style_calls();
    const auto success_it = std::find_if(styles.begin(), styles.end(), [&](const auto& call) {
        return call.style == MessageStyle::success && call.message == result.message;
    });
    ASSERT_NE(success_it, styles.end())
        << "the existing success message must be styled success, wording unchanged";

    const auto info_it = std::find_if(styles.begin(), success_it, [](const auto& call) {
        return call.style == MessageStyle::info;
    });
    ASSERT_NE(info_it, success_it)
        << "an info-styled status line must precede the push, per requirement 5";
    EXPECT_FALSE(info_it->message.empty());
}

TEST_F(GitBootstrapServiceTest,
       Run_PushFails_PrintsInfoStatusLineBeforePushThenStylesFailureMessageError) {
    arrange_happy_path();
    git_process_.set_result_for("push", GitResult{1, "", "remote: Permission denied"});
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::push_failed);
    const auto& styles = console_.style_calls();
    const auto error_it = std::find_if(styles.begin(), styles.end(), [&](const auto& call) {
        return call.style == MessageStyle::error && call.message == result.message;
    });
    ASSERT_NE(error_it, styles.end());

    const auto info_it = std::find_if(styles.begin(), error_it, [](const auto& call) {
        return call.style == MessageStyle::info;
    });
    EXPECT_NE(info_it, error_it)
        << "an info-styled status line must precede the push even when it fails";
}

// Edge case: the push itself succeeded but the follow-up upstream-tracking command failed --
// this is still the *same* success_message print (categorization rule item 6 example
// "GitBootstrapService.cpp:219"), so it must still be styled success, not warning/error, even
// though the wording admits tracking couldn't be confirmed.
TEST_F(GitBootstrapServiceTest,
       Run_WithToken_SetUpstreamToFails_MessageStillStyledSuccessNotWarningOrError) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("ghp_supersecret");
    git_process_.set_result_for_arg_containing(
        "--set-upstream-to", GitResult{1, "", "error: could not set upstream"});
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::success);
    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [&](const auto& call) {
        return call.message == result.message;
    });
    ASSERT_NE(it, styles.end());
    EXPECT_EQ(it->style, MessageStyle::success);
}

TEST_F(GitBootstrapServiceTest, Run_AlreadyBootstrapped_NoticeStyledInfo) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    service.run();

    ASSERT_FALSE(console_.style_calls().empty());
    EXPECT_EQ(console_.style_calls().back().style, MessageStyle::info);
}

TEST_F(GitBootstrapServiceTest, Run_GitUnavailable_MessageStyledError) {
    git_process_.queue_availability(false);
    git_process_.set_install_result(false);
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    ASSERT_FALSE(console_.style_calls().empty());
    EXPECT_EQ(console_.style_calls().back().style, MessageStyle::error);
    EXPECT_EQ(console_.style_calls().back().message, result.message);
}

TEST_F(GitBootstrapServiceTest, Run_RepoNotFound_MessageStyledError) {
    RepoInfo info;
    info.ambiguous_not_found = true;
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");
    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);

    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::repo_not_found);
    ASSERT_FALSE(console_.style_calls().empty());
    EXPECT_EQ(console_.style_calls().back().style, MessageStyle::error);
    EXPECT_EQ(console_.style_calls().back().message, result.message);
}

}  // namespace
