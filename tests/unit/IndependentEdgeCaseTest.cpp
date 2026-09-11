#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "reader/ChapterFetcher.h"
#include "shared/AccessRegistry.h"
#include "shared/ChapterFile.h"
#include "shared/CryptoProvider.h"
#include "shared/GitHubClient.h"
#include "support/Fakes.h"
#include "writer/ChapterEncryptionService.h"
#include "writer/GitBootstrapService.h"
#include "writer/ReaderAccessController.h"
#include "reader/ReaderViewerService.h"

// Independent-tester edge case (blind re-verification pass for spec 001).
//
// Spec 001's edge-case list says: "Username/email left blank -> re-prompt rather than
// silently committing with an empty identity." The coder's own tests only cover the
// literal empty string ("") for username/email. A whitespace-only answer (e.g. the user
// hits spacebar then Enter) is not literally empty but is just as "blank" in intent -- this
// test checks whether GitBootstrapService::run() treats it that way too.

using buddyshare::shared::RepoInfo;
using buddyshare::writer::BootstrapOutcome;
using buddyshare::writer::GitBootstrapService;

namespace fs = std::filesystem;

namespace {

class IndependentEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_independent_edge_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    fs::path working_dir_;
    FakeGitHubClient github_client_;
    FakeGitProcess git_process_;
    FakeConsole console_;
};

TEST_F(IndependentEdgeCaseTest,
       Run_WhitespaceOnlyUsername_TreatedAsBlank_DoesNotCommitWithBlankIdentity) {
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    info.has_commits = false;
    github_client_.set_response(info);

    // Whitespace-only answer for username (not literally ""), then a real one.
    console_.queue_answer("   ");
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");

    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);
    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::success);

    const auto& commands = git_process_.recorded_commands();
    const auto name_cmd = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return std::find(args.begin(), args.end(), "user.name") != args.end();
    });
    ASSERT_NE(name_cmd, commands.end());
    // The whitespace-only answer must not be the identity git ends up configured with.
    EXPECT_EQ(std::find(name_cmd->begin(), name_cmd->end(), "   "), name_cmd->end())
        << "whitespace-only username was silently accepted as the git identity";
}

// Second independent-tester edge case: spec 001 says "Git missing and winget also
// fails/unavailable -> clear manual-install error, return to menu." The coder's own tests
// cover winget outright failing (install_git_via_winget() returns false) and winget fully
// succeeding (install succeeds AND the post-install availability re-check reports true), but
// not the case where winget *reports* success while git is still not actually available
// afterward (e.g. a PATH that isn't refreshed yet, or a silent installer failure). This must
// still be treated as a failure -- not silently proceed to prompt for GitHub details and run
// git commands that would then fail unpredictably.
TEST_F(IndependentEdgeCaseTest,
       Run_WingetReportsSuccessButGitStillUnavailableAfterward_TreatedAsGitUnavailable) {
    git_process_.queue_availability(false);  // initial check: git missing
    git_process_.set_install_result(true);   // winget claims success...
    git_process_.queue_availability(false);  // ...but the re-check still finds no git

    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);
    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::git_unavailable);
    EXPECT_FALSE(result.message.empty());
    // No prompts and no GitHub call should happen once git is confirmed still unavailable.
    EXPECT_EQ(console_.prompt_call_count(), 0);
    EXPECT_EQ(github_client_.call_count(), 0);
    EXPECT_TRUE(git_process_.recorded_commands().empty());
}

// Third independent-tester edge case, for spec 003: the coder's own malformed-line tests for
// AccessRegistry only cover non-numeric chapter tokens (e.g. "two,three"), never a token that
// *is* all-digits but too large for `int` (e.g. a 20-digit chapter number from a fat-fingered
// or hand-edited access.txt). Spec 003's Edge Cases section requires such a line to be skipped
// with a warning "same pattern as spec 002's malformed-line handling -- don't abort the whole
// run" -- not to crash the process. AccessRegistry::load_from_file's is_all_digits() check lets
// this token through to an unguarded std::stoi(), which throws std::out_of_range.
using buddyshare::shared::AccessRegistry;

class IndependentEdgeCaseAccessTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_independent_edge_access_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
        file_path_ = (working_dir_ / "access.txt").string();
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    void write_raw(const std::string& contents) const {
        std::ofstream out(file_path_);
        out << contents;
    }

    fs::path working_dir_;
    std::string file_path_;
    FakeConsole console_;
};

TEST_F(IndependentEdgeCaseAccessTest,
       LoadFromFile_ChapterNumberTooLargeForInt_SkippedWithWarning_DoesNotCrash_OthersStillLoad) {
    write_raw(
        "alice:MIIB_alice_key:Master:\n"
        "bob:MIIB_bob_key:Apprentice:99999999999999999999\n");
    AccessRegistry registry(file_path_, console_);

    // Must not throw/crash (an uncaught std::out_of_range from std::stoi would abort the whole
    // run, which spec 003's malformed-line handling explicitly forbids).
    EXPECT_NO_THROW(registry.load_from_file());

    EXPECT_FALSE(registry.find_by_name("bob").has_value())
        << "the too-large chapter number line should be skipped, not loaded";
    EXPECT_TRUE(registry.find_by_name("alice").has_value())
        << "the rest of the file should still load despite the one bad line";
}

// Same overflow gap, but on the writer-input side: ReaderAccessController::prompt_for_details
// (via collect_registration_details) must reject an out-of-range chapter number the same way it
// rejects a non-numeric one -- a clear re-prompt, not a crash.
using buddyshare::writer::ReaderAccessController;

class IndependentEdgeCaseReaderAccessTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ =
            fs::temp_directory_path() /
            ("buddyshare_independent_edge_reader_access_" + std::string(test_info->name()));
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

TEST_F(IndependentEdgeCaseReaderAccessTest,
       CollectRegistrationDetails_ApprenticeChapterTooLargeForInt_RejectedAndReprompts_DoesNotCrash) {
    console_.queue_answer("Apprentice");
    console_.queue_answer("99999999999999999999");  // all-digits, but overflows int
    console_.queue_answer("4");
    ReaderAccessController controller(*registry_, console_, git_process_);

    buddyshare::writer::RegistrationDetails details;
    EXPECT_NO_THROW(details = controller.collect_registration_details());

    EXPECT_EQ(details.chapters, (std::unordered_set<int>{4}));
}

// Fourth independent-tester edge case, for spec 003: AccessRegistry::load_from_file()
// validates that a line's chapter set matches its level (chapters_match_level), but
// AccessRegistry::append_reader()/update_reader() -- its own write-side API -- perform no
// such check before persisting. The coder's tests only ever construct ReaderEntry values
// with an already-consistent level/chapters pairing, so this write/reload round-trip gap
// was never exercised: writing an inconsistent entry (e.g. Master with a non-empty chapter
// set) succeeds silently, but the very same registry can no longer see that reader after a
// reload, because load_from_file's validation then treats the persisted line as malformed
// and skips it -- a self-inflicted data-loss path distinct from the hand-edited-file case
// spec 003's Edge Cases section describes.
using buddyshare::shared::ReaderEntry;

class IndependentEdgeCaseAccessWriteTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_independent_edge_access_write_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_);
        file_path_ = (working_dir_ / "access.txt").string();
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    fs::path working_dir_;
    std::string file_path_;
    FakeConsole console_;
};

TEST_F(IndependentEdgeCaseAccessWriteTest,
       AppendReader_ChaptersInconsistentWithLevel_SucceedsButReaderIsLostOnReload) {
    AccessRegistry registry(file_path_, console_);
    registry.load_from_file();

    ReaderEntry inconsistent;
    inconsistent.name = "alice";
    inconsistent.public_key_base64 = "MIIB_alice_key";
    inconsistent.level = buddyshare::shared::AccessLevel::master;  // Master must carry no chapters...
    inconsistent.chapters = {5};                                    // ...but this one does.

    // append_reader has no chapters-match-level guard of its own -- it accepts and persists
    // the inconsistent entry without complaint.
    EXPECT_TRUE(registry.append_reader(inconsistent));
    EXPECT_TRUE(registry.find_by_name("alice").has_value())
        << "the in-memory registry that just wrote it can still see it";

    // Reloading the same file (e.g. the next program run) re-validates every line and now
    // treats this one as malformed, since Master must carry an empty chapter list.
    AccessRegistry reloaded(file_path_, console_);
    reloaded.load_from_file();

    EXPECT_FALSE(reloaded.find_by_name("alice").has_value())
        << "a reader written via append_reader() with a level/chapters mismatch silently "
           "disappears on the next load -- append_reader()/update_reader() should reject "
           "(or the caller must guarantee) a consistent combination before persisting";
}

// Fifth independent-tester edge case, for spec 002: ChapterEncryptionService::register_reader()
// stores whatever public key the writer pastes with no format validation (AccessRegistry's
// append_reader() likewise only checks for a duplicate name -- see
// src/shared/AccessRegistry.h). If that pasted value is not valid base64-encoded SPKI/DER (e.g.
// a copy/paste mistake -- truncated, wrong format, or plain garbage), the reader is still
// registered successfully. The problem surfaces later: encrypt_chapter()'s per-reader wrap loop
// (src/writer/ChapterEncryptionService.cpp) calls CryptoProvider::wrap_key() unconditionally
// for every authorized reader, and wrap_key() throws std::runtime_error("Invalid RSA public
// key.") when d2i_PUBKEY() fails to parse the decoded bytes. Nothing between that throw and
// main() catches std::runtime_error -- main.cpp's try/catch around encrypt_chapter() only
// catches std::out_of_range (for an oversized chapter number) -- so encrypting ANY chapter after
// such a reader is registered crashes the whole BuddyShare.exe process (std::terminate),
// instead of the "skip with a warning, don't abort the whole run" treatment this codebase
// applies everywhere else a malformed access.txt-adjacent value is encountered (AccessRegistry's
// malformed-line skipping; the two chapter-number-overflow edge cases above). It also silently
// blocks every *other* already-valid reader from getting their wrapped key for that chapter,
// and leaves the .enc file unwritten and nothing committed/pushed -- worse than spec 002's
// documented "zero-reader chapter" case, which at least still produces a valid, pushed file.
using buddyshare::shared::AccessLevel;
using buddyshare::shared::ChapterFile;
using buddyshare::shared::CryptoProvider;
using buddyshare::shared::ReaderEntry;
using buddyshare::shared::RsaKeyPair;
using buddyshare::writer::ChapterEncryptionService;
using buddyshare::writer::EncryptChapterOutcome;
using buddyshare::writer::EncryptChapterResult;

class IndependentEdgeCaseChapterEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        working_dir_ = fs::temp_directory_path() /
                       ("buddyshare_independent_edge_chapter_enc_" + std::string(test_info->name()));
        fs::remove_all(working_dir_);
        fs::create_directories(working_dir_ / ".git");  // marks the repo as already-bootstrapped.
        access_file_path_ = (working_dir_ / "access.txt").string();
        registry_ = std::make_unique<AccessRegistry>(access_file_path_, console_);
        registry_->load_from_file();
        access_controller_ =
            std::make_unique<ReaderAccessController>(*registry_, console_, git_process_);
    }

    void TearDown() override { fs::remove_all(working_dir_); }

    std::string write_plaintext(const std::string& contents) const {
        const fs::path path = working_dir_ / "chapter-source.txt";
        std::ofstream out(path);
        out << contents;
        return path.string();
    }

    fs::path working_dir_;
    std::string access_file_path_;
    FakeConsole console_;
    FakeGitProcess git_process_;
    CryptoProvider crypto_;
    std::unique_ptr<AccessRegistry> registry_;
    std::unique_ptr<ReaderAccessController> access_controller_;
};

TEST_F(IndependentEdgeCaseChapterEncryptionTest,
       EncryptChapter_ReaderWithMalformedPublicKey_FailsGracefully_DoesNotCrashTheProcess) {
    ReaderEntry malformed;
    malformed.name = "typo-victim";
    malformed.public_key_base64 = "not-a-real-base64-spki-key";  // a copy/paste mistake.
    malformed.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(malformed));

    console_.queue_answer("n");        // "Add a reader?" -- decline, one is already registered.
    console_.queue_answer("password");  // encryption password.
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service(working_dir_.string(), *registry_, *access_controller_,
                                      crypto_, git_process_, console_);

    EncryptChapterResult result{};
    EXPECT_NO_THROW(result = service.encrypt_chapter(1, plaintext_path))
        << "a single reader with a malformed public key must not crash the whole process -- "
           "it should be treated like any other malformed per-reader entry (skip + warn), "
           "matching this project's handling of every other bad access.txt-adjacent value";
    EXPECT_NE(result.outcome, EncryptChapterOutcome::success)
        << "at minimum this must not silently report success while masking the failure";
}

// Sixth independent-tester edge case, for spec 002: the coder's own malformed-key test above
// (and the pre-existing ChapterEncryptionServiceTest suite) only ever exercises a malformed
// public key in isolation -- either zero readers, or exactly one reader and it's the malformed
// one. Neither proves that a *co-existing, valid* reader's wrapped key still makes it into the
// output file when the wrap loop (src/writer/ChapterEncryptionService.cpp) throws partway
// through iterating an unordered_map of readers. Since std::unordered_map iteration order is
// unspecified, the malformed reader could be visited before or after the valid one; if the
// catch/continue in the loop were ever subtly broken (e.g. a `return` instead of `continue`, or
// the exception unwound past the loop for some reader ordering), the valid reader's key would
// silently go missing from the .enc file with nothing catching it in existing tests -- this
// test locks in that alice's key is wrapped and written regardless of typo-victim being present.
TEST_F(IndependentEdgeCaseChapterEncryptionTest,
       EncryptChapter_OneValidAndOneMalformedReader_ValidReaderStillGetsWrappedKey) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    ReaderEntry valid;
    valid.name = "alice";
    valid.public_key_base64 = alice.public_key_base64;
    valid.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(valid));

    ReaderEntry malformed;
    malformed.name = "typo-victim";
    malformed.public_key_base64 = "not-a-real-base64-spki-key";  // a copy/paste mistake.
    malformed.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(malformed));

    console_.queue_answer("n");         // "Add a reader?" -- decline, two are already registered.
    console_.queue_answer("password");  // encryption password.
    const std::string plaintext_path = write_plaintext("Chapter One.");
    ChapterEncryptionService service(working_dir_.string(), *registry_, *access_controller_,
                                      crypto_, git_process_, console_);

    EncryptChapterResult result{};
    EXPECT_NO_THROW(result = service.encrypt_chapter(1, plaintext_path));

    EXPECT_EQ(result.outcome, EncryptChapterOutcome::some_readers_skipped);
    EXPECT_EQ(result.reader_count, 1)
        << "alice's valid key must still be wrapped despite typo-victim's malformed key";

    std::ifstream in(ChapterFile::file_path_for_chapter(working_dir_.string(), 1), std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::optional<ChapterFile> written = ChapterFile::from_json(buffer.str());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->wrapped_keys_base64.count("alice"), 1u)
        << "the valid reader's wrapped key must be present in the written file";
    EXPECT_EQ(written->wrapped_keys_base64.count("typo-victim"), 0u)
        << "the malformed reader must be skipped, not wrapped with garbage";
}

// Seventh independent-tester edge case, for spec 004: the coder's own tests for
// ReaderViewerService::ensure_local_keypair() only ever exercise "no stored key at all"
// (IPrivateKeyStore::read_private_key_pem returns std::nullopt -> generate a fresh keypair)
// versus "a valid, well-formed stored key" (reuse it as-is). Spec 004's Edge Cases section
// describes a third case: "Local private key file missing/corrupted ... the program would
// generate a *new* keypair". DpapiPrivateKeyStore folds a DPAPI-unprotect failure into
// std::nullopt (treated as "missing"), but ensure_local_keypair() itself has no equivalent
// fallback for a value that *is* present (read_private_key_pem returns a value, so the
// "existing key" branch is taken) but isn't a parseable PEM private key -- e.g. a private-key
// file that was truncated mid-write, or corrupted at rest for a reason other than a DPAPI
// scope mismatch. CryptoProvider::public_key_from_private() returns std::nullopt for such
// input, and ensure_local_keypair() silently swallows that via `.value_or("")`, returning a
// KeypairInfo with newly_generated == false and an *empty* public_key_base64 -- never
// regenerating a usable keypair, and (per main.cpp's run_reader_menu) an empty string would
// then be displayed to the reader as "their public key" to send the writer, rather than a
// clear error or a fresh, actually-usable keypair.
using buddyshare::reader::ReaderViewerService;

TEST(IndependentEdgeCaseReaderViewerServiceTest,
     EnsureLocalKeypair_StoredKeyIsCorruptedNotMissing_SilentlyReturnsEmptyPublicKey_DoesNotRegenerate) {
    FakePrivateKeyStore key_store;
    key_store.seed("alice", "this is not a valid PEM private key");  // present, but unparseable.
    buddyshare::shared::CryptoProvider crypto;
    FakeConsole console;
    ReaderViewerService service(key_store, crypto, console);

    const auto info = service.ensure_local_keypair("alice");

    EXPECT_FALSE(info.newly_generated)
        << "documenting current behavior: a corrupted-but-present stored key is treated as "
           "'existing' rather than triggering the spec's described regeneration";
    EXPECT_TRUE(info.public_key_base64.empty())
        << "a corrupted stored key silently yields an empty public key -- the reader would be "
           "shown nothing to send the writer, with no error surfaced anywhere";
    EXPECT_EQ(key_store.write_count(), 0)
        << "no new keypair was generated/stored for this corrupted-key case, unlike the "
           "missing-key case spec 004's Edge Cases section describes";
}

// Eighth independent-tester edge case, for the spec 003 defect fix (ReaderAccessController::
// edit_existing_reader now git add/commit/pushes access.txt). The coder's own git tests only
// ever exercise a registry containing the single reader being edited. In a real access.txt with
// multiple readers, editing one must still produce exactly one add/commit/push (not one per
// existing reader), the commit message must name only the edited reader, and every other
// reader's persisted name/key/level/chapter-list line must be completely untouched -- both in
// memory and in the file AccessRegistry::persist() rewrites in full on every update.
TEST_F(IndependentEdgeCaseReaderAccessTest,
       EditExistingReader_MultiReaderRegistry_CommitsOnceAndLeavesOtherReadersUntouched) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    ReaderEntry bob;
    bob.name = "bob";
    bob.public_key_base64 = "MIIB_bob_key";
    bob.level = AccessLevel::apprentice;
    bob.chapters = {1};
    ASSERT_TRUE(registry_->append_reader(bob));

    console_.queue_answer("Novice");
    console_.queue_answer("7");
    ReaderAccessController controller(*registry_, console_, git_process_);

    const bool result = controller.edit_existing_reader("alice");

    ASSERT_TRUE(result);

    // bob's in-memory entry must be byte-for-byte unchanged.
    const auto bob_after = registry_->find_by_name("bob");
    ASSERT_TRUE(bob_after.has_value());
    EXPECT_EQ(bob_after->public_key_base64, "MIIB_bob_key");
    EXPECT_EQ(bob_after->level, AccessLevel::apprentice);
    EXPECT_EQ(bob_after->chapters, (std::unordered_set<int>{1}));

    // Exactly one add/commit/push -- not one per reader in the registry.
    const auto& commands = git_process_.recorded_commands();
    const auto count_cmd = [&](const std::string& subcommand) {
        return std::count_if(commands.begin(), commands.end(), [&](const auto& args) {
            return !args.empty() && args[0] == subcommand;
        });
    };
    EXPECT_EQ(count_cmd("add"), 1);
    EXPECT_EQ(count_cmd("commit"), 1);
    EXPECT_EQ(count_cmd("push"), 1);

    const auto commit_it = std::find_if(commands.begin(), commands.end(), [](const auto& args) {
        return !args.empty() && args[0] == "commit";
    });
    ASSERT_NE(commit_it, commands.end());
    EXPECT_TRUE(std::any_of(commit_it->begin(), commit_it->end(), [](const std::string& a) {
        return a.find("chore: update access for alice to Novice") != std::string::npos;
    }));
    EXPECT_FALSE(std::any_of(commit_it->begin(), commit_it->end(), [](const std::string& a) {
        return a.find("bob") != std::string::npos;
    })) << "the commit message must name only the reader that was actually edited";

    // The rewritten file on disk must still carry bob's line unchanged.
    AccessRegistry reloaded(file_path_, console_);
    reloaded.load_from_file();
    const auto bob_reloaded = reloaded.find_by_name("bob");
    ASSERT_TRUE(bob_reloaded.has_value());
    EXPECT_EQ(bob_reloaded->public_key_base64, "MIIB_bob_key");
    EXPECT_EQ(bob_reloaded->level, AccessLevel::apprentice);
    EXPECT_EQ(bob_reloaded->chapters, (std::unordered_set<int>{1}));
}

// Ninth independent-tester edge case, for the GitHub rate-limit fix (this pass's Part 2). The
// spec text for src/reader/ChapterFetcher.h/.cpp explicitly asserts: "fetch_access_txt()'s
// existing network_error path already just prints error_message verbatim so it automatically
// gets the friendly wording once HttpGitHubClient sets it -- no separate change needed there."
// That's a claim about wiring, not a change the coder made -- and the coder's own
// ChapterFetcherTest.cpp never actually exercises a rate-limited ContentsResult through
// fetch_access_txt() (only through list_master_candidates() and try_fetch_chapter(), which DO
// have their own explicit rate_limited checks). This test locks in that the claim is actually
// true: a 403-shaped ContentsResult (network_error=true, rate_limited=true, the friendly
// error_message) flowing into fetch_access_txt() really does surface that exact message via the
// existing network_error branch, rather than some other path swallowing or rewording it.
using buddyshare::reader::ChapterFetcher;
using buddyshare::reader::FetchAccessTxtOutcome;
using buddyshare::shared::ContentsResult;

TEST(IndependentEdgeCaseGitHubRateLimitTest,
     FetchAccessTxt_RateLimitedResponse_SurfacesFriendlyMessageThroughExistingNetworkErrorPath) {
    FakeGitHubClient github_client;
    ContentsResult response;
    response.network_error = true;
    response.rate_limited = true;
    response.error_message =
        "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to "
        "reset, or connect to a VPN for a new IP address.";
    github_client.set_contents_response("access.txt", response);
    ChapterFetcher fetcher(github_client, "some-writer", "some-repo");

    const auto result = fetcher.fetch_access_txt();

    EXPECT_EQ(result.outcome, FetchAccessTxtOutcome::network_error);
    EXPECT_EQ(result.error_message, response.error_message)
        << "fetch_access_txt() must not swallow or reword the rate-limit message -- main.cpp's "
           "existing 'print error_message verbatim' handling depends on this being untouched";
}

// Tenth independent-tester edge case: GitBootstrapService::run() was deliberately left
// unmodified by Part 2 (the spec only calls out ChapterFetcher/main.cpp's reader-side paths for
// explicit rate_limited handling). But GitBootstrapService::validate_repo_info() consumes the
// same shared::RepoInfo that HttpGitHubClient::get_repo() now sets rate_limited=true AND
// network_error=true on for a 403. Nothing in the coder's GitBootstrapServiceTest.cpp exercises
// a rate-limited RepoInfo, so it was never confirmed that this "no separate change needed, it
// just flows through the existing network_error branch" assumption actually holds for the
// writer side too -- e.g. that the friendly message isn't lost, and that no git command runs
// before the rate limit is reported.
TEST(IndependentEdgeCaseGitHubRateLimitTest,
     GitBootstrapRun_RateLimitedRepoLookup_ReportsNetworkErrorWithFriendlyMessage_NoGitCommands) {
    const fs::path working_dir =
        fs::temp_directory_path() / "buddyshare_independent_edge_ratelimit_bootstrap";
    fs::remove_all(working_dir);
    fs::create_directories(working_dir);

    FakeGitHubClient github_client;
    buddyshare::shared::RepoInfo info;
    info.network_error = true;
    info.rate_limited = true;
    info.error_message =
        "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to "
        "reset, or connect to a VPN for a new IP address.";
    github_client.set_response(info);
    FakeGitProcess git_process;
    FakeConsole console;
    console.queue_answer("alice");
    console.queue_answer("alice@example.com");
    console.queue_answer("myrepo");
    console.queue_answer("");

    GitBootstrapService service(working_dir.string(), github_client, git_process, console);
    const auto result = service.run();

    EXPECT_EQ(result.outcome, BootstrapOutcome::network_error);
    EXPECT_NE(result.message.find("GitHub's request limit has been reached"), std::string::npos)
        << "a 403 during repo validation must still surface the friendly rate-limit message, "
           "not just a generic 'network error' string";
    EXPECT_TRUE(git_process.recorded_commands().empty())
        << "a rate-limited repo lookup must abort before any git command runs, same as every "
           "other validate_repo_info failure";

    fs::remove_all(working_dir);
}

// Eleventh independent-tester edge case, for the two-entry .gitignore seeding change
// (kGitignoreEntries = {"chapters-source/", "*.exe"}). The coder's own
// GitBootstrapServiceTest.cpp tests "one of the two entries already present" but never "both
// entries already present" -- seed_gitignore()'s missing_entries list would be empty in that
// case, and the function returns early before ever opening the file for append. That early
// return must mean the existing .gitignore is left completely byte-for-byte untouched (no
// trailing newline silently added, no duplicate lines) -- exactly like the coder's existing
// single-entry "already present" test implies for one entry, just confirmed here for both.
TEST_F(IndependentEdgeCaseTest,
       Run_BothGitignoreEntriesAlreadyPresent_FileLeftCompletelyUnmodified) {
    const std::string original_contents = "chapters-source/\n*.exe\n";
    {
        std::ofstream existing(working_dir_ / ".gitignore");
        existing << original_contents;
    }
    RepoInfo info;
    info.exists = true;
    info.default_branch = "main";
    info.has_commits = false;
    github_client_.set_response(info);
    console_.queue_answer("alice");
    console_.queue_answer("alice@example.com");
    console_.queue_answer("myrepo");
    console_.queue_answer("");

    GitBootstrapService service(working_dir_.string(), github_client_, git_process_, console_);
    const auto result = service.run();

    ASSERT_EQ(result.outcome, BootstrapOutcome::success);

    std::ifstream in(working_dir_ / ".gitignore");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    EXPECT_EQ(buffer.str(), original_contents)
        << "when every entry is already present, seed_gitignore() must leave the file "
           "byte-for-byte untouched -- no extra trailing newline, no duplicated lines";
}

// Twelfth independent-tester edge case, for spec 005 (CLI output styling). The coder's own
// ReaderAccessControllerTest.cpp has a pre-existing "Silent git-failure fix" test
// (EditExistingReader_PushFails_StillReturnsTrue_ButPrintsHonestPushFailedMessage) that asserts
// on the push-failed line's *text* via all_output(), but spec 005's Behavior item 6 (call-site
// categorization rule) requires that same line to actually carry MessageStyle::warning ("partial
// success or noteworthy-but-not-fatal") -- and unlike every other file spec 005's Architecture
// Context names (GitBootstrapService, ChapterEncryptionService, AccessRegistry,
// ReaderViewerService), no test in ReaderAccessControllerTest.cpp ever asserts on
// FakeConsole::style_calls() at all. This locks in that the categorization actually reached this
// call site, not just that the wording is honest.
using buddyshare::shared::MessageStyle;

TEST_F(IndependentEdgeCaseReaderAccessTest,
       EditExistingReader_PushFails_WarningMessageActuallyStyledWarning_NotLeftAtDefaultPlain) {
    ReaderEntry alice;
    alice.name = "alice";
    alice.public_key_base64 = "MIIB_alice_key";
    alice.level = AccessLevel::master;
    ASSERT_TRUE(registry_->append_reader(alice));

    console_.queue_answer("Apprentice");
    console_.queue_answer("2,3");
    git_process_.set_result_for("push", buddyshare::writer::GitResult{1, "", "remote: fatal"});
    ReaderAccessController controller(*registry_, console_, git_process_);

    ASSERT_TRUE(controller.edit_existing_reader("alice"));

    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("push failed") != std::string::npos;
    });
    ASSERT_NE(it, styles.end()) << "the push-failed message must have been printed at all";
    EXPECT_EQ(it->style, MessageStyle::warning)
        << "spec 005's categorization rule treats a locally-succeeded-but-push-failed edit as "
           "'partial-success or noteworthy-but-not-fatal' (warning), not left at the default "
           "plain style";
}

}  // namespace
