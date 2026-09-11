#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "support/Fakes.h"
#include "writer/GitBootstrapService.h"
#include "writer/WriterMenu.h"

using buddyshare::writer::GitBootstrapService;
using buddyshare::writer::WriterCommand;
using buddyshare::writer::WriterMenu;

namespace fs = std::filesystem;

namespace {

class WriterMenuTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_menu_test_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    fs::path working_dir_;
    FakeGitHubClient github_client_;
    FakeGitProcess git_process_;
    FakeConsole console_;
};

// AC3: "Initialize repo" is offered while the bootstrap hasn't run yet.
TEST_F(WriterMenuTest, InitializeRepoVisible_WhenNotYetBootstrapped) {
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_NE(std::find(commands.begin(), commands.end(), WriterCommand::initialize_repo),
              commands.end());
}

// AC4: "Initialize repo" is hidden once a .git folder already exists.
TEST_F(WriterMenuTest, InitializeRepoHidden_WhenAlreadyBootstrapped) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_EQ(std::find(commands.begin(), commands.end(), WriterCommand::initialize_repo),
              commands.end());
}

// Spec 003: "Manage reader access" is not offered before the writer has bootstrapped a repo --
// there is no access.txt to manage yet.
TEST_F(WriterMenuTest, ManageReaderAccessHidden_WhenNotYetBootstrapped) {
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_EQ(
        std::find(commands.begin(), commands.end(), WriterCommand::manage_reader_access),
        commands.end());
}

// Spec 003: "Manage reader access" becomes available once the bootstrap has already run.
TEST_F(WriterMenuTest, ManageReaderAccessVisible_WhenAlreadyBootstrapped) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_NE(
        std::find(commands.begin(), commands.end(), WriterCommand::manage_reader_access),
        commands.end());
}

// Spec 002: "Encrypt a chapter" is not offered before the writer has bootstrapped a repo --
// there is no origin to push encrypted chapters to yet.
TEST_F(WriterMenuTest, EncryptChapterHidden_WhenNotYetBootstrapped) {
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_EQ(std::find(commands.begin(), commands.end(), WriterCommand::encrypt_chapter),
              commands.end());
}

// Spec 002: "Encrypt a chapter" becomes available once the bootstrap has already run, offered
// alongside (not instead of) "Manage reader access".
TEST_F(WriterMenuTest, EncryptChapterVisible_WhenAlreadyBootstrapped_AlongsideManageReaderAccess) {
    fs::create_directories(working_dir_ / ".git");
    GitBootstrapService bootstrap(working_dir_.string(), github_client_, git_process_, console_);
    WriterMenu menu(bootstrap);

    const auto commands = menu.available_commands();

    EXPECT_NE(std::find(commands.begin(), commands.end(), WriterCommand::encrypt_chapter),
              commands.end());
    EXPECT_NE(
        std::find(commands.begin(), commands.end(), WriterCommand::manage_reader_access),
        commands.end());
}

}  // namespace
