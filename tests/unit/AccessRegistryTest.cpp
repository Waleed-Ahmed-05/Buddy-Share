#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "shared/AccessRegistry.h"
#include "support/Fakes.h"

using buddyshare::shared::AccessLevel;
using buddyshare::shared::AccessRegistry;
using buddyshare::shared::MessageStyle;
using buddyshare::shared::ReaderEntry;

namespace fs = std::filesystem;

namespace {

class AccessRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_access_registry_test_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
        file_path_ = (working_dir_ / "access.txt").string();
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    void write_raw(const std::string& contents) const {
        std::ofstream out(file_path_);
        out << contents;
    }

    std::string read_raw() const {
        std::ifstream in(file_path_);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    fs::path working_dir_;
    std::string file_path_;
    FakeConsole console_;
};

// --- load_from_file: missing file --------------------------------------------------------

TEST_F(AccessRegistryTest, LoadFromFile_MissingFile_ResultsInZeroReaders) {
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_TRUE(registry.all_readers().empty());
}

// --- load_from_file: well-formed lines, all three levels --------------------------------

TEST_F(AccessRegistryTest, LoadFromFile_ParsesMasterApprenticeAndNovice) {
    write_raw(
        "alice:MIIB_alice_key:Master:\n"
        "bob:MIIB_bob_key:Apprentice:2,3\n"
        "carol:MIIB_carol_key:Novice:1\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    const auto alice = registry.find_by_name("alice");
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->level, AccessLevel::master);
    EXPECT_TRUE(alice->chapters.empty());
    EXPECT_EQ(alice->public_key_base64, "MIIB_alice_key");

    const auto bob = registry.find_by_name("bob");
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->level, AccessLevel::apprentice);
    EXPECT_EQ(bob->chapters, (std::unordered_set<int>{2, 3}));

    const auto carol = registry.find_by_name("carol");
    ASSERT_TRUE(carol.has_value());
    EXPECT_EQ(carol->level, AccessLevel::novice);
    EXPECT_EQ(carol->chapters, (std::unordered_set<int>{1}));

    EXPECT_EQ(registry.all_readers().size(), 3u);
}

// --- load_from_file: malformed-line handling (skip + warn, keep loading) ----------------

TEST_F(AccessRegistryTest, LoadFromFile_UnknownLevelString_SkippedWithWarning_OthersStillLoad) {
    write_raw(
        "alice:MIIB_alice_key:Master:\n"
        "eve:MIIB_eve_key:Overlord:\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("eve").has_value());
    EXPECT_TRUE(registry.find_by_name("alice").has_value());
    EXPECT_NE(console_.all_output().find("eve"), std::string::npos)
        << "warning should name the offending line/reader";
}

TEST_F(AccessRegistryTest, LoadFromFile_WrongFieldCount_SkippedWithWarning) {
    write_raw(
        "alice:MIIB_alice_key:Master:\n"
        "malformed_line_missing_fields\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_EQ(registry.all_readers().size(), 1u);
    EXPECT_NE(console_.all_output().find("malformed_line_missing_fields"), std::string::npos);
}

TEST_F(AccessRegistryTest, LoadFromFile_ApprenticeWithEmptyChapterList_SkippedWithWarning) {
    write_raw("bob:MIIB_bob_key:Apprentice:\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("bob").has_value());
    EXPECT_FALSE(console_.all_output().empty());
}

TEST_F(AccessRegistryTest, LoadFromFile_NoviceWithMultipleChapters_SkippedWithWarning) {
    write_raw("carol:MIIB_carol_key:Novice:1,2\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("carol").has_value());
}

TEST_F(AccessRegistryTest, LoadFromFile_MasterWithNonEmptyChapterList_SkippedWithWarning) {
    write_raw("alice:MIIB_alice_key:Master:2\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("alice").has_value());
}

TEST_F(AccessRegistryTest, LoadFromFile_NonNumericChapter_SkippedWithWarning) {
    write_raw("bob:MIIB_bob_key:Apprentice:two,three\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("bob").has_value());
}

// Edge case: a hand-edited duplicate chapter number in the same line collapses naturally via
// the std::unordered_set backing rather than erroring -- the set itself provides dedup.
TEST_F(AccessRegistryTest, LoadFromFile_DuplicateChapterNumbersInLine_CollapseViaSet) {
    write_raw("bob:MIIB_bob_key:Apprentice:2,2,3\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    const auto bob = registry.find_by_name("bob");
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->chapters, (std::unordered_set<int>{2, 3}));
}

// Edge case: a chapter number that doesn't correspond to any chapter written yet is allowed,
// not an error (spec 003 Behavior item 5).
TEST_F(AccessRegistryTest, LoadFromFile_ChapterNumberNotYetWritten_LoadsWithoutError) {
    write_raw("bob:MIIB_bob_key:Apprentice:99\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    const auto bob = registry.find_by_name("bob");
    ASSERT_TRUE(bob.has_value());
    EXPECT_EQ(bob->chapters, (std::unordered_set<int>{99}));
}

// --- find_by_name -------------------------------------------------------------------------

TEST_F(AccessRegistryTest, FindByName_UnknownName_ReturnsNullopt) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    EXPECT_FALSE(registry.find_by_name("nobody").has_value());
}

// --- append_reader ------------------------------------------------------------------------

TEST_F(AccessRegistryTest, AppendReader_NewName_WritesFourFieldLineAndPersists) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    ReaderEntry entry;
    entry.name = "dave";
    entry.public_key_base64 = "MIIB_dave_key";
    entry.level = AccessLevel::apprentice;
    entry.chapters = {2, 3};

    const bool result = registry.append_reader(entry);

    EXPECT_TRUE(result);
    EXPECT_TRUE(registry.find_by_name("dave").has_value());

    // Reload from disk (a fresh instance) to confirm the 4-field line was actually persisted,
    // not just held in memory.
    AccessRegistry reloaded(file_path_, console_);
    reloaded.load_from_file();
    const auto dave = reloaded.find_by_name("dave");
    ASSERT_TRUE(dave.has_value());
    EXPECT_EQ(dave->level, AccessLevel::apprentice);
    EXPECT_EQ(dave->chapters, (std::unordered_set<int>{2, 3}));
    EXPECT_EQ(dave->public_key_base64, "MIIB_dave_key");
}

TEST_F(AccessRegistryTest, AppendReader_DuplicateName_RefusesAndDoesNotOverwrite) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    ReaderEntry original;
    original.name = "alice";
    original.public_key_base64 = "MIIB_alice_key";
    original.level = AccessLevel::master;
    ASSERT_TRUE(registry.append_reader(original));

    ReaderEntry duplicate;
    duplicate.name = "alice";
    duplicate.public_key_base64 = "MIIB_someone_elses_key";
    duplicate.level = AccessLevel::novice;
    duplicate.chapters = {1};

    const bool result = registry.append_reader(duplicate);

    EXPECT_FALSE(result);
    const auto alice = registry.find_by_name("alice");
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->public_key_base64, "MIIB_alice_key")
        << "the duplicate append must not silently overwrite the existing entry";
    EXPECT_EQ(alice->level, AccessLevel::master);
}

// --- update_reader ------------------------------------------------------------------------

TEST_F(AccessRegistryTest, UpdateReader_ExistingName_ChangesLevelAndChapters_KeepsNameAndKey) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    ReaderEntry entry;
    entry.name = "alice";
    entry.public_key_base64 = "MIIB_alice_key";
    entry.level = AccessLevel::master;
    ASSERT_TRUE(registry.append_reader(entry));

    const bool result =
        registry.update_reader("alice", AccessLevel::apprentice, std::unordered_set<int>{2, 3});

    EXPECT_TRUE(result);
    const auto alice = registry.find_by_name("alice");
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->level, AccessLevel::apprentice);
    EXPECT_EQ(alice->chapters, (std::unordered_set<int>{2, 3}));
    EXPECT_EQ(alice->name, "alice");
    EXPECT_EQ(alice->public_key_base64, "MIIB_alice_key");

    // Persisted change survives reload, and the file still shows exactly one "alice" line.
    AccessRegistry reloaded(file_path_, console_);
    reloaded.load_from_file();
    const auto reloaded_alice = reloaded.find_by_name("alice");
    ASSERT_TRUE(reloaded_alice.has_value());
    EXPECT_EQ(reloaded_alice->level, AccessLevel::apprentice);
    EXPECT_EQ(reloaded.all_readers().size(), 1u);
}

TEST_F(AccessRegistryTest, UpdateReader_UnknownName_ReturnsFalse_NoLineCreated) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    const bool result =
        registry.update_reader("ghost", AccessLevel::master, std::unordered_set<int>{});

    EXPECT_FALSE(result);
    EXPECT_FALSE(registry.find_by_name("ghost").has_value());
    EXPECT_TRUE(registry.all_readers().empty());
}

// --- Spec 005: CLI output styling -----------------------------------------------------------
//
// Categorization rule item 6 examples: "AccessRegistry.cpp:131" (malformed line skipped) and
// "AccessRegistry.cpp:186" (reader already registered, not overwriting) are both warning --
// partial-success / noteworthy-but-not-fatal, per the rule text.

TEST_F(AccessRegistryTest, LoadFromFile_MalformedLine_SkipWarningStyledWarning) {
    write_raw(
        "alice:MIIB_alice_key:Master:\n"
        "malformed_line_missing_fields\n");
    AccessRegistry registry(file_path_, console_);

    registry.load_from_file();

    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("malformed_line_missing_fields") != std::string::npos;
    });
    ASSERT_NE(it, styles.end());
    EXPECT_EQ(it->style, MessageStyle::warning);
}

TEST_F(AccessRegistryTest, AppendReader_DuplicateName_RefusalMessageStyledWarning) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    ReaderEntry original;
    original.name = "alice";
    original.public_key_base64 = "MIIB_alice_key";
    original.level = AccessLevel::master;
    ASSERT_TRUE(registry.append_reader(original));

    ReaderEntry duplicate;
    duplicate.name = "alice";
    duplicate.public_key_base64 = "MIIB_someone_elses_key";
    duplicate.level = AccessLevel::master;

    registry.append_reader(duplicate);

    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("already registered") != std::string::npos;
    });
    ASSERT_NE(it, styles.end());
    EXPECT_EQ(it->style, MessageStyle::warning);
}

}  // namespace
