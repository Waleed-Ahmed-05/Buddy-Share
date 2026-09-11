#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>

#include "shared/AccessRegistry.h"
#include "support/Fakes.h"
#include "writer/ReaderAccessController.h"

using buddyshare::shared::AccessLevel;
using buddyshare::shared::AccessRegistry;
using buddyshare::shared::ReaderEntry;
using buddyshare::writer::ReaderAccessController;
using buddyshare::writer::RegistrationDetails;

namespace fs = std::filesystem;

namespace {

class ReaderAccessControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ =
            fs::temp_directory_path() /
            ("buddyshare_reader_access_controller_test_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
        file_path_ = (working_dir_ / "access.txt").string();
        registry_ = std::make_unique<AccessRegistry>(file_path_, console_);
        registry_->load_from_file();
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    fs::path working_dir_;
    std::string file_path_;
    FakeConsole console_;
    FakeGitProcess git_process_;
    std::unique_ptr<AccessRegistry> registry_;
};

// --- collect_registration_details: Master ------------------------------------------------

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_Master_NoFurtherPrompt) {
    console_.queue_answer("Master");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.level, AccessLevel::master);
    EXPECT_TRUE(details.chapters.empty());
    EXPECT_EQ(console_.prompt_call_count(), 1) << "Master requires exactly one prompt (the level)";
}

// --- collect_registration_details: Apprentice --------------------------------------------

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_Apprentice_ParsesChapterList) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.level, AccessLevel::apprentice);
    EXPECT_EQ(details.chapters, (std::unordered_set<int>{2, 3}));
}

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_ApprenticeEmptyList_Reprompts) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("");  // invalid: must name at least one chapter
    console_.queue_answer("4");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.chapters, (std::unordered_set<int>{4}));
    EXPECT_EQ(console_.prompt_call_count(), 3);
}

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_ApprenticeNonNumericChapter_Reprompts) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("two,three");  // invalid: non-numeric
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.chapters, (std::unordered_set<int>{2, 3}));
}

// Edge case: duplicate chapter numbers in the same answer are rejected with a clear message
// and re-prompted, rather than silently deduplicated (spec 003 Edge Cases).
TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_ApprenticeDuplicateChapters_RejectedAndReprompts) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("2,2");  // invalid: duplicate chapter number
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.chapters, (std::unordered_set<int>{2, 3}));
    EXPECT_FALSE(console_.all_output().empty())
        << "a clear rejection message should be printed for the duplicate";
}

// --- collect_registration_details: Novice ------------------------------------------------

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_Novice_ParsesSingleChapter) {
    console_.queue_answer("Novice");
    console_.queue_answer("5");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.level, AccessLevel::novice);
    EXPECT_EQ(details.chapters, (std::unordered_set<int>{5}));
}

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_NoviceMultipleChapters_Reprompts) {
    console_.queue_answer("Novice");
    console_.queue_answer("1,2");  // invalid: Novice must have exactly one
    console_.queue_answer("3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.chapters, (std::unordered_set<int>{3}));
}

// --- collect_registration_details: invalid level ------------------------------------------

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_UnknownLevel_RepromptsRatherThanDefaulting) {
    console_.queue_answer("Overlord");  // invalid level
    console_.queue_answer("Master");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.level, AccessLevel::master);
}

// Edge case: blank level input is also invalid, never silently defaulted.
TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_BlankLevel_Reprompts) {
    console_.queue_answer("");
    console_.queue_answer("Novice");
    console_.queue_answer("1");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const RegistrationDetails details = controller.collect_registration_details();

    EXPECT_EQ(details.level, AccessLevel::novice);
}

// --- edit_existing_reader ------------------------------------------------------------------

TEST_F(ReaderAccessControllerTest, EditExistingReader_UnknownName_ReturnsFalse_NoPrompt) {
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("ghost");

    EXPECT_FALSE(result);
    EXPECT_EQ(console_.prompt_call_count(), 0)
        << "editing an unregistered name must not prompt -- it is not an implicit registration";
}

TEST_F(ReaderAccessControllerTest, EditExistingReader_ExistingReader_UpdatesLevelKeepsNameAndKey) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("alice");

    EXPECT_TRUE(result);
    const auto updated = registry_->find_by_name("alice");
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->level, AccessLevel::apprentice);
    EXPECT_EQ(updated->chapters, (std::unordered_set<int>{2, 3}));
    EXPECT_EQ(updated->name, "alice");
    EXPECT_EQ(updated->public_key_base64, "MIIB_alice_key");
}

// --- edit_existing_reader: git add/commit/push (spec 003 Acceptance Criterion 3) -----------

// A successful edit must produce its own commit -- stage access.txt, commit with the
// documented message convention ("chore: update access for <name> to <level>"), and push,
// reusing the shared git_process_ the same way ChapterEncryptionService::register_reader does.
TEST_F(ReaderAccessControllerTest, EditExistingReader_SuccessfulEdit_StagesCommitsAndPushesAccessTxt) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("alice");

    ASSERT_TRUE(result);
    const auto& commands = git_process_.recorded_commands();
    const auto add_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add" &&
               std::find(args.begin(), args.end(), "access.txt") != args.end();
    });
    ASSERT_NE(add_it, commands.end()) << "editing an existing reader must stage access.txt";

    const auto commit_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "commit";
    });
    ASSERT_NE(commit_it, commands.end()) << "editing an existing reader must produce its own commit";
    EXPECT_TRUE(std::any_of(commit_it->begin(), commit_it->end(), [](const std::string& a) {
        return a.find("chore: update access for alice to Apprentice") != std::string::npos;
    })) << "commit message must follow spec 003's documented convention";

    const auto push_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "push";
    });
    EXPECT_NE(push_it, commands.end()) << "editing an existing reader must push the commit";

    EXPECT_LT(add_it, commit_it) << "access.txt must be staged before it's committed";
}

// Edge case: editing a name that isn't registered must run zero git commands -- editing is
// not an implicit registration, and there is nothing new to commit.
TEST_F(ReaderAccessControllerTest, EditExistingReader_UnknownName_RunsNoGitCommands) {
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("ghost");

    EXPECT_FALSE(result);
    EXPECT_TRUE(git_process_.recorded_commands().empty())
        << "editing an unregistered name must not touch git at all";
}

// Edge case: invalid intermediate answers during the edit's prompt loop (unrecognized level,
// then a valid one) must not trigger any git command until the whole edit has actually
// succeeded -- exactly one add/commit/push, never one per invalid attempt.
TEST_F(ReaderAccessControllerTest, EditExistingReader_InvalidInputDuringPrompt_NoGitCommandsUntilValidCompletion) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    console_.queue_answer("Overlord");  // invalid level: must not commit/push anything yet
    console_.queue_answer("Novice");
    console_.queue_answer("1,2");  // invalid: Novice requires exactly one chapter
    console_.queue_answer("5");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("alice");

    ASSERT_TRUE(result);
    const auto& commands = git_process_.recorded_commands();
    const auto add_count = std::count_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add";
    });
    const auto commit_count = std::count_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "commit";
    });
    const auto push_count = std::count_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "push";
    });
    EXPECT_EQ(add_count, 1) << "invalid attempts during the prompt loop must not stage anything";
    EXPECT_EQ(commit_count, 1) << "invalid attempts during the prompt loop must not commit anything";
    EXPECT_EQ(push_count, 1) << "invalid attempts during the prompt loop must not push anything";
}

// --- Silent git-failure fix: push result must be checked (bugfix) --------------------------

TEST_F(ReaderAccessControllerTest,
       EditExistingReader_PushFails_StillReturnsTrue_ButPrintsHonestPushFailedMessage) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("alice");

    EXPECT_TRUE(result) << "the local edit did succeed";
    const auto updated = registry_->find_by_name("alice");
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->level, AccessLevel::apprentice);
    EXPECT_NE(console_.all_output().find("push failed"), std::string::npos)
        << "must not silently claim success when the push failed";
}

// --- Prompt UX pass (cosmetic wording, no behavior change) ----------------------------------

TEST_F(ReaderAccessControllerTest, CollectRegistrationDetails_LevelPrompt_UsesFriendlyWordingWithExplanation) {
    console_.queue_answer("Master");
    ReaderAccessController controller(*registry_, console_, git_process_);

    controller.collect_registration_details();

    ASSERT_FALSE(console_.calls().empty());
    EXPECT_EQ(console_.calls()[0].message,
              "Access level -- Master sees every chapter, Apprentice sees a chosen list, Novice "
              "sees exactly one. Enter Master, Apprentice, or Novice:");
}

TEST_F(ReaderAccessControllerTest,
       CollectRegistrationDetails_ApprenticeChapterPrompt_UsesFriendlyWordingWithExample) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    ReaderAccessController controller(*registry_, console_, git_process_);

    controller.collect_registration_details();

    ASSERT_GE(console_.calls().size(), 2u);
    EXPECT_EQ(console_.calls()[1].message,
              "Chapter numbers for this Apprentice reader, comma-separated (e.g. 2,3):");
}

TEST_F(ReaderAccessControllerTest,
       CollectRegistrationDetails_NoviceChapterPrompt_UsesFriendlyWordingWithExample) {
    console_.queue_answer("Novice");
    console_.queue_answer("1");
    ReaderAccessController controller(*registry_, console_, git_process_);

    controller.collect_registration_details();

    ASSERT_GE(console_.calls().size(), 2u);
    EXPECT_EQ(console_.calls()[1].message,
              "Chapter number for this Novice reader (exactly one, e.g. 1):");
}

// --- is_authorized_for_chapter ---------------------------------------------------------

TEST_F(ReaderAccessControllerTest, IsAuthorizedForChapter_Master_AlwaysTrue) {
    ReaderEntry master;
    master.name = "alice";
    master.level = AccessLevel::master;

    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(master, 1));
    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(master, 999));
}

TEST_F(ReaderAccessControllerTest, IsAuthorizedForChapter_Apprentice_TrueOnlyForListedChapters) {
    ReaderEntry apprentice;
    apprentice.name = "bob";
    apprentice.level = AccessLevel::apprentice;
    apprentice.chapters = {2, 3};

    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(apprentice, 2));
    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(apprentice, 3));
    EXPECT_FALSE(ReaderAccessController::is_authorized_for_chapter(apprentice, 1));
    EXPECT_FALSE(ReaderAccessController::is_authorized_for_chapter(apprentice, 4));
}

TEST_F(ReaderAccessControllerTest, IsAuthorizedForChapter_Novice_TrueOnlyForItsSingleChapter) {
    ReaderEntry novice;
    novice.name = "carol";
    novice.level = AccessLevel::novice;
    novice.chapters = {1};

    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(novice, 1));
    EXPECT_FALSE(ReaderAccessController::is_authorized_for_chapter(novice, 2));
}

// Edge case: a chapter number the writer pre-authorized before it exists yet is still
// evaluated purely numerically -- not a special case, not an error (spec 003 Behavior item 5).
TEST_F(ReaderAccessControllerTest, IsAuthorizedForChapter_PreAuthorizedFutureChapter_StillEvaluatesTrue) {
    ReaderEntry apprentice;
    apprentice.name = "bob";
    apprentice.level = AccessLevel::apprentice;
    apprentice.chapters = {99};

    EXPECT_TRUE(ReaderAccessController::is_authorized_for_chapter(apprentice, 99));
}

}  // namespace
