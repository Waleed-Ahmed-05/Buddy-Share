#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "shared/AccessRegistry.h"
#include "shared/ChapterFile.h"
#include "shared/CryptoProvider.h"
#include "support/Fakes.h"
#include "writer/ChapterEncryptionService.h"
#include "writer/ReaderAccessController.h"

using buddyshare::shared::AccessLevel;
using buddyshare::shared::AccessRegistry;
using buddyshare::shared::ChapterFile;
using buddyshare::shared::CryptoProvider;
using buddyshare::shared::MessageStyle;
using buddyshare::shared::ReaderEntry;
using buddyshare::shared::RsaKeyPair;
using buddyshare::writer::ChapterEncryptionService;
using buddyshare::writer::EncryptChapterOutcome;
using buddyshare::writer::EncryptChapterResult;
using buddyshare::writer::ReaderAccessController;

namespace fs = std::filesystem;

namespace {

class ChapterEncryptionServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_chapter_encryption_test_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
        access_file_path_ = (working_dir_ / "access.txt").string();
        registry_ = std::make_unique<AccessRegistry>(access_file_path_, console_);
        registry_->load_from_file();
        access_controller_ =
            std::make_unique<ReaderAccessController>(*registry_, console_, git_process_);
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    // Marks working_dir_ as an already-bootstrapped repo (spec 001) with an origin remote
    // configured -- the precondition ChapterEncryptionService requires.
    void mark_bootstrapped() {
        fs::create_directories(working_dir_ / ".git");
        // Default FakeGitProcess::run() already returns exit_code 0 for any unconfigured
        // subcommand, including "remote", so no explicit set_result_for is required here.
    }

    std::string write_plaintext(const std::string& contents) const {
        const fs::path path = working_dir_ / "chapter-source.txt";
        std::ofstream out(path);
        out << contents;
        return path.string();
    }

    ReaderEntry make_master_reader(const std::string& name, const std::string& public_key) const {
        ReaderEntry entry;
        entry.name = name;
        entry.public_key_base64 = public_key;
        entry.level = AccessLevel::master;
        return entry;
    }

    std::string read_file(const fs::path& path) const {
        std::ifstream in(path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    ChapterEncryptionService make_service() {
        return ChapterEncryptionService(working_dir_.string(), *registry_, *access_controller_,
                                         crypto_, git_process_, console_);
    }

    fs::path working_dir_;
    std::string access_file_path_;
    FakeConsole console_;
    FakeGitProcess git_process_;
    CryptoProvider crypto_;
    std::unique_ptr<AccessRegistry> registry_;
    std::unique_ptr<ReaderAccessController> access_controller_;
};

// --- Precondition: bootstrap must already have run (Edge Cases) ----------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_NoGitFolder_ReturnsNotBootstrapped_NoGitCommandsNoPrompts) {
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::not_bootstrapped);
    EXPECT_EQ(console_.prompt_call_count(), 0);
    const auto& commands = git_process_.recorded_commands();
    EXPECT_TRUE(std::none_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && (args[0] == "add" || args[0] == "commit" || args[0] == "push");
    }));
}

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_GitFolderButNoOriginRemote_ReturnsNotBootstrapped) {
    fs::create_directories(working_dir_ / ".git");
    git_process_.set_result_for("remote", buddyshare::writer::GitResult{1, "", "error: No such remote 'origin'"});
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::not_bootstrapped);
    const auto& commands = git_process_.recorded_commands();
    EXPECT_TRUE(std::none_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && (args[0] == "add" || args[0] == "commit" || args[0] == "push");
    }));
}

// --- Missing plaintext source ----------------------------------------------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_PlaintextFileDoesNotExist_ReturnsPlaintextNotFound) {
    mark_bootstrapped();
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result =
        service.encrypt_chapter(1, (working_dir_ / "does-not-exist.txt").string());

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::plaintext_not_found);
    const auto& commands = git_process_.recorded_commands();
    EXPECT_TRUE(std::none_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add";
    }));
}

// --- Acceptance Criterion 5: empty/missing access.txt must prompt, never silently proceed --

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_EmptyRegistry_AsksToAddReader_DeclinedProceedsWithZeroWrappedKeys) {
    mark_bootstrapped();
    console_.queue_answer("n");                  // "Add a reader? (y/n)"
    console_.queue_answer("writer's password");  // encryption password
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::success);
    EXPECT_EQ(result.reader_count, 0);
    const std::optional<ChapterFile> written =
        ChapterFile::from_json(read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1)));
    ASSERT_TRUE(written.has_value());
    EXPECT_TRUE(written->wrapped_keys_base64.empty());
    EXPECT_GE(console_.prompt_call_count(), 1) << "must explicitly ask before proceeding, never silent";
}

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_EmptyRegistry_AcceptedAddsReaderThenWrapsForThem) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("y");                     // "Add a reader? (y/n)"
    console_.queue_answer("alice");                 // reader name
    console_.queue_answer(alice.public_key_base64);  // reader public key
    console_.queue_answer("Master");                // access level
    console_.queue_answer("writer's password");     // encryption password
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::success);
    EXPECT_EQ(result.reader_count, 1);
    const std::optional<ChapterFile> written =
        ChapterFile::from_json(read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1)));
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->wrapped_keys_base64.count("alice"), 1u);
}

// --- Acceptance Criterion 1: N authorized readers -> exactly N wrapped-key entries ----------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_TwoMasterReaders_ProducesExactlyTwoWrappedKeyEntries) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    const RsaKeyPair bob = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    ASSERT_TRUE(registry_->append_reader(make_master_reader("bob", bob.public_key_base64)));
    console_.queue_answer("n");  // "Add a reader?" -- registry already non-empty, decline
    console_.queue_answer("writer's password");
    const std::string plaintext_path = write_plaintext("Chapter One: content.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::success);
    EXPECT_EQ(result.reader_count, 2);
    const std::optional<ChapterFile> written =
        ChapterFile::from_json(read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1)));
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->wrapped_keys_base64.size(), 2u);
    EXPECT_EQ(written->wrapped_keys_base64.count("alice"), 1u);
    EXPECT_EQ(written->wrapped_keys_base64.count("bob"), 1u);
}

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_WrittenFileNeverContainsThePlaintext) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("writer's password");
    const std::string secret_sentence = "The dragon's true name was Vermithrax.";
    const std::string plaintext_path = write_plaintext(secret_sentence);
    ChapterEncryptionService service = make_service();

    service.encrypt_chapter(1, plaintext_path);

    const std::string written =
        read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1));
    EXPECT_EQ(written.find(secret_sentence), std::string::npos);
    EXPECT_EQ(written.find("Vermithrax"), std::string::npos);
}

// --- Spec 003 integration: per-chapter authorization gates wrapping -------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_ApprenticeAuthorizedOnlyForListedChapters_WrappedOnlyThere) {
    mark_bootstrapped();
    const RsaKeyPair bob = CryptoProvider::generate_rsa_keypair();
    ReaderEntry apprentice;
    apprentice.name = "bob";
    apprentice.public_key_base64 = bob.public_key_base64;
    apprentice.level = AccessLevel::apprentice;
    apprentice.chapters = {2};
    ASSERT_TRUE(registry_->append_reader(apprentice));

    console_.queue_answer("n");
    console_.queue_answer("writer's password");
    const std::string plaintext_path = write_plaintext("Chapter One content.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::success);
    EXPECT_EQ(result.reader_count, 0)
        << "bob is only authorized for chapter 2, not chapter 1";
}

// --- Password handling (Edge Cases + Acceptance Criterion 6) --------------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_BlankPassword_RepromptsRatherThanProceeding) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("");                    // blank password: must re-prompt
    console_.queue_answer("real password");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::success);
    const auto masked_calls = std::count_if(console_.calls().begin(), console_.calls().end(),
                                             [](const auto& call) { return call.masked; });
    EXPECT_GE(masked_calls, 2) << "blank password must trigger a re-prompt";
}

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_PasswordNeverAppearsInConsoleOutputOrWrittenFile) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("super-secret-password-12345");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    service.encrypt_chapter(1, plaintext_path);

    EXPECT_EQ(console_.all_output().find("super-secret-password-12345"), std::string::npos);
    const std::string written =
        read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1));
    EXPECT_EQ(written.find("super-secret-password-12345"), std::string::npos);
}

// --- Git add/commit/push sequence -------------------------------------------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_StagesCommitsAndPushesTheEncFile) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("password");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    service.encrypt_chapter(3, plaintext_path);

    const auto& commands = git_process_.recorded_commands();
    const auto add_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add" &&
               std::any_of(args.begin(), args.end(),
                           [](const std::string& a) { return a.find("chapter-03.enc") != std::string::npos; });
    });
    ASSERT_NE(add_it, commands.end());
    const auto commit_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "commit";
    });
    ASSERT_NE(commit_it, commands.end());
    EXPECT_TRUE(std::any_of(commit_it->begin(), commit_it->end(), [](const std::string& a) {
        return a.find("3") != std::string::npos;
    }));
    const auto push_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "push";
    });
    EXPECT_NE(push_it, commands.end());
    EXPECT_LT(add_it, commit_it) << "the file must be staged before it's committed";
}

// --- Silent git-failure fix: push result must be checked (bugfix) --------------------------

// A failed push must not be reported as a plain success -- the chapter is still encrypted and
// written locally (add/commit still happen), but the writer must be told the push failed so
// they don't assume readers can already see it.
TEST_F(ChapterEncryptionServiceTest,
       EncryptChapter_PushFails_ReturnsPushFailedOutcome_ButFileIsStillWrittenLocally) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("password");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::push_failed);
    EXPECT_NE(result.message.find("encrypted locally"), std::string::npos);
    EXPECT_NE(result.message.find("NOT pushed"), std::string::npos);
    EXPECT_NE(result.message.find("git connection"), std::string::npos);
    // The local write must have actually happened -- the failure is about the push only.
    const std::optional<ChapterFile> written =
        ChapterFile::from_json(read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1)));
    ASSERT_TRUE(written.has_value());
    const auto& commands = git_process_.recorded_commands();
    EXPECT_TRUE(std::any_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add";
    }));
    EXPECT_TRUE(std::any_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "commit";
    }));
}

// --- register_reader: push result must be checked (bugfix) ---------------------------------

TEST_F(ChapterEncryptionServiceTest,
       RegisterReader_PushFails_StillReturnsTrue_ButPrintsHonestPushFailedMessage) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("alice");
    console_.queue_answer(alice.public_key_base64);
    console_.queue_answer("Master");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    ChapterEncryptionService service = make_service();

    const bool result = service.register_reader();

    EXPECT_TRUE(result) << "the local registry update did succeed";
    EXPECT_TRUE(registry_->find_by_name("alice").has_value());
    EXPECT_NE(console_.all_output().find("registered locally"), std::string::npos);
    EXPECT_NE(console_.all_output().find("push failed"), std::string::npos)
        << "must not silently claim success when the push failed";
}

// --- Prompt UX pass (cosmetic wording, no behavior change) ----------------------------------

TEST_F(ChapterEncryptionServiceTest,
       RegisterReader_PublicKeyPrompt_UsesFriendlyWordingReferencingReaderRun) {
    mark_bootstrapped();
    console_.queue_answer("alice");
    console_.queue_answer("some-key");
    console_.queue_answer("Master");
    ChapterEncryptionService service = make_service();

    service.register_reader();

    ASSERT_GE(console_.calls().size(), 2u);
    EXPECT_EQ(console_.calls()[1].message,
              "New reader's public key (they get this from their own first Reader run -- paste "
              "it exactly):");
}

// --- Edge case: adding a reader after a chapter is already encrypted doesn't retroactively --
// --- rewrap that earlier chapter ------------------------------------------------------------

TEST_F(ChapterEncryptionServiceTest, RegisterReaderAfterEncryption_DoesNotRetroactivelyRewrapEarlierChapter) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("password");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();
    ASSERT_EQ(service.encrypt_chapter(1, plaintext_path).outcome, EncryptChapterOutcome::success);
    const std::string before =
        read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1));

    const RsaKeyPair bob = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("bob");
    console_.queue_answer(bob.public_key_base64);
    console_.queue_answer("Master");
    ASSERT_TRUE(service.register_reader());

    const std::string after =
        read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1));
    EXPECT_EQ(before, after) << "chapter 1's .enc file must be untouched by a later registration";
}

// --- Full pipeline round trip (Acceptance Criteria 2 & 3) ------------------------------------

TEST_F(ChapterEncryptionServiceTest, EncryptChapter_FullRoundTrip_ReaderCanDecryptWithOwnPrivateKeyAndWriterPassword) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", alice.public_key_base64)));
    console_.queue_answer("n");
    console_.queue_answer("writer's password");
    const std::string original_text = "Chapter One: the true content of the story.";
    const std::string plaintext_path = write_plaintext(original_text);
    ChapterEncryptionService service = make_service();

    ASSERT_EQ(service.encrypt_chapter(1, plaintext_path).outcome, EncryptChapterOutcome::success);

    const std::optional<ChapterFile> written =
        ChapterFile::from_json(read_file(ChapterFile::file_path_for_chapter(working_dir_.string(), 1)));
    ASSERT_TRUE(written.has_value());

    const auto salt = CryptoProvider::base64_decode(written->salt_base64);
    const auto iv = CryptoProvider::base64_decode(written->iv_base64);
    const auto ciphertext = CryptoProvider::base64_decode(written->ciphertext_base64);
    const auto wrapped_key = CryptoProvider::base64_decode(written->wrapped_keys_base64.at("alice"));

    const auto aes_key = crypto_.unwrap_key(wrapped_key, alice.private_key_pem);
    ASSERT_TRUE(aes_key.has_value());
    const auto rederived_key = crypto_.derive_key_with_salt("writer's password", salt);
    EXPECT_EQ(*aes_key, rederived_key);

    const auto plaintext =
        crypto_.decrypt(buddyshare::shared::EncryptedContent{iv, ciphertext}, *aes_key);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(std::string(plaintext->begin(), plaintext->end()), original_text);
}

// --- register_reader --------------------------------------------------------------------------

TEST_F(ChapterEncryptionServiceTest, RegisterReader_NewReader_AppendsAndPushesAccessTxt) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("alice");
    console_.queue_answer(alice.public_key_base64);
    console_.queue_answer("Master");
    ChapterEncryptionService service = make_service();

    const bool result = service.register_reader();

    EXPECT_TRUE(result);
    EXPECT_TRUE(registry_->find_by_name("alice").has_value());
    const auto& commands = git_process_.recorded_commands();
    EXPECT_TRUE(std::any_of(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "add" &&
               std::find(args.begin(), args.end(), "access.txt") != args.end();
    }));
    EXPECT_TRUE(std::any_of(commands.begin(), commands.end(),
                             [](const auto& args) { return !args.empty() && args[0] == "push"; }));
}

// Edge case: duplicate reader name is refused, not silently overwritten.
TEST_F(ChapterEncryptionServiceTest, RegisterReader_DuplicateName_ReturnsFalse_DoesNotOverwriteOrPush) {
    mark_bootstrapped();
    const RsaKeyPair original_key = CryptoProvider::generate_rsa_keypair();
    ASSERT_TRUE(registry_->append_reader(make_master_reader("alice", original_key.public_key_base64)));
    const std::size_t push_count_before = std::count_if(
        git_process_.recorded_commands().begin(), git_process_.recorded_commands().end(),
        [](const auto& args) { return !args.empty() && args[0] == "push"; });

    const RsaKeyPair impostor_key = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("alice");  // duplicate name
    console_.queue_answer(impostor_key.public_key_base64);
    console_.queue_answer("Novice");
    console_.queue_answer("1");
    ChapterEncryptionService service = make_service();

    const bool result = service.register_reader();

    EXPECT_FALSE(result);
    const auto existing = registry_->find_by_name("alice");
    ASSERT_TRUE(existing.has_value());
    EXPECT_EQ(existing->public_key_base64, original_key.public_key_base64)
        << "the duplicate registration attempt must not overwrite alice's real key";
    const std::size_t push_count_after = std::count_if(
        git_process_.recorded_commands().begin(), git_process_.recorded_commands().end(),
        [](const auto& args) { return !args.empty() && args[0] == "push"; });
    EXPECT_EQ(push_count_after, push_count_before)
        << "a refused registration must not push anything new";
}

// --- Spec 005: CLI output styling -----------------------------------------------------------
//
// Behavior item 5 requires an info-styled "before" status line preceding each of this class's
// two blocking pushes (encrypt_chapter's and register_reader's), paired with the existing
// "after" message now styled per the categorization rule (item 6): success/error/warning for
// encrypt_chapter's push, warning for register_reader's (a failed access.txt push is a partial
// success -- the local registration still went through).

TEST_F(ChapterEncryptionServiceTest,
       EncryptChapter_HappyPath_PrintsInfoStatusLineBeforePushThenStylesSuccessMessageSuccess) {
    mark_bootstrapped();
    console_.queue_answer("n");
    console_.queue_answer("password");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::success);
    const auto& styles = console_.style_calls();
    const auto success_it = std::find_if(styles.begin(), styles.end(), [&](const auto& call) {
        return call.style == MessageStyle::success && call.message == result.message;
    });
    ASSERT_NE(success_it, styles.end());

    const auto info_it = std::find_if(styles.begin(), success_it, [](const auto& call) {
        return call.style == MessageStyle::info;
    });
    ASSERT_NE(info_it, success_it)
        << "an info-styled status line must precede the chapter push, per requirement 5";
    EXPECT_FALSE(info_it->message.empty());
}

TEST_F(ChapterEncryptionServiceTest,
       EncryptChapter_PushFails_PrintsInfoStatusLineBeforePushThenStylesFailureMessageError) {
    mark_bootstrapped();
    console_.queue_answer("n");
    console_.queue_answer("password");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    const EncryptChapterResult result = service.encrypt_chapter(1, plaintext_path);

    ASSERT_EQ(result.outcome, EncryptChapterOutcome::push_failed);
    const auto& styles = console_.style_calls();
    const auto error_it = std::find_if(styles.begin(), styles.end(), [&](const auto& call) {
        return call.style == MessageStyle::error && call.message == result.message;
    });
    ASSERT_NE(error_it, styles.end());

    const auto info_it = std::find_if(styles.begin(), error_it, [](const auto& call) {
        return call.style == MessageStyle::info;
    });
    EXPECT_NE(info_it, error_it)
        << "an info-styled status line must precede the chapter push even when it fails";
}

// Categorization rule item 6 example "ChapterEncryptionService.cpp:165" (zero wrapped keys).
TEST_F(ChapterEncryptionServiceTest, EncryptChapter_EmptyRegistryNotice_StyledWarning) {
    mark_bootstrapped();
    console_.queue_answer("n");
    console_.queue_answer("writer's password");
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service = make_service();

    service.encrypt_chapter(1, plaintext_path);

    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("zero wrapped keys") != std::string::npos;
    });
    ASSERT_NE(it, styles.end());
    EXPECT_EQ(it->style, MessageStyle::warning);
}

// Categorization rule item 6 example "ChapterEncryptionService.cpp:156" (registered locally but
// push failed) -- a partial success, styled warning rather than error, with its own info-styled
// status line preceding the access.txt push.
TEST_F(ChapterEncryptionServiceTest,
       RegisterReader_PushFails_PrintsInfoStatusLineBeforePushThenStylesMessageWarning) {
    mark_bootstrapped();
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    console_.queue_answer("alice");
    console_.queue_answer(alice.public_key_base64);
    console_.queue_answer("Master");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    ChapterEncryptionService service = make_service();

    service.register_reader();

    const auto& styles = console_.style_calls();
    const auto warning_it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("push failed") != std::string::npos;
    });
    ASSERT_NE(warning_it, styles.end());
    EXPECT_EQ(warning_it->style, MessageStyle::warning);

    const auto info_it = std::find_if(styles.begin(), warning_it, [](const auto& call) {
        return call.style == MessageStyle::info;
    });
    EXPECT_NE(info_it, warning_it)
        << "an info-styled status line must precede the access.txt push even when it fails";
}

}  // namespace
