#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "reader/ReaderViewerService.h"
#include "shared/ChapterFile.h"
#include "shared/CryptoProvider.h"
#include "support/Fakes.h"

using buddyshare::reader::DecryptChapterOutcome;
using buddyshare::reader::ReaderViewerService;
using buddyshare::shared::Bytes;
using buddyshare::shared::ChapterFile;
using buddyshare::shared::CryptoProvider;
using buddyshare::shared::DerivedKey;
using buddyshare::shared::EncryptedContent;
using buddyshare::shared::MessageStyle;
using buddyshare::shared::RsaKeyPair;

namespace {

Bytes to_bytes(const std::string& text) { return Bytes(text.begin(), text.end()); }

// Builds a real, well-formed ChapterFile the way ChapterEncryptionService would (spec 002),
// so decrypt_chapter can be tested against a genuine fixture rather than a mock -- per spec
// 004 Acceptance Criterion 5 ("a test harness calling CryptoProvider directly").
ChapterFile build_chapter_file(CryptoProvider& crypto, const std::string& reader_name,
                                const std::string& reader_public_key_base64,
                                const std::string& plaintext_text,
                                const std::string& password = "writer's password") {
    const DerivedKey derived = crypto.derive_key(password);
    const EncryptedContent encrypted = crypto.encrypt(to_bytes(plaintext_text), derived.key);
    const Bytes wrapped = crypto.wrap_key(derived.key, reader_public_key_base64);

    ChapterFile file;
    file.salt_base64 = CryptoProvider::base64_encode(derived.salt);
    file.iv_base64 = CryptoProvider::base64_encode(encrypted.iv);
    file.ciphertext_base64 = CryptoProvider::base64_encode(encrypted.ciphertext);
    file.wrapped_keys_base64[reader_name] = CryptoProvider::base64_encode(wrapped);
    return file;
}

class ReaderViewerServiceTest : public ::testing::Test {
protected:
    FakePrivateKeyStore key_store_;
    CryptoProvider crypto_;
    FakeConsole console_;
};

// --- ensure_local_keypair (spec 004 Behavior steps 1-2, Acceptance Criteria 1-2) -------------

TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_NoStoredKey_GeneratesAndStoresANewKeypair) {
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto info = service.ensure_local_keypair("alice");

    EXPECT_TRUE(info.newly_generated);
    EXPECT_FALSE(info.public_key_base64.empty());
    EXPECT_EQ(key_store_.write_count(), 1);
    EXPECT_EQ(key_store_.last_written_username(), "alice");
}

TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_NoStoredKey_PrintsPublicKeyWithInstructions) {
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto info = service.ensure_local_keypair("alice");

    EXPECT_NE(console_.all_output().find(info.public_key_base64), std::string::npos)
        << "the public key must be printed for the reader to send to the writer";
}

// AC2: a subsequent run for the same username reuses the existing key -- no regeneration.
TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_AlreadyStoredKey_ReusesWithoutRegenerating) {
    const RsaKeyPair existing = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", existing.private_key_pem);
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto info = service.ensure_local_keypair("alice");

    EXPECT_FALSE(info.newly_generated);
    EXPECT_EQ(info.public_key_base64, existing.public_key_base64);
    EXPECT_EQ(key_store_.write_count(), 0) << "must not overwrite an already-existing keypair";
}

TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_TwoDifferentUsernames_GetIndependentKeypairs) {
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto alice = service.ensure_local_keypair("alice");
    const auto bob = service.ensure_local_keypair("bob");

    EXPECT_NE(alice.public_key_base64, bob.public_key_base64);
    EXPECT_EQ(key_store_.write_count(), 2);
}

// Acceptance Criterion 7 / security: the private key must never be printed anywhere.
TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_NeverPrintsThePrivateKey) {
    ReaderViewerService service(key_store_, crypto_, console_);

    service.ensure_local_keypair("alice");

    const auto stored = key_store_.read_private_key_pem("alice");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(console_.all_output().find(*stored), std::string::npos);
}

// --- decrypt_chapter (spec 004 Behavior step 8, Acceptance Criteria 5-6) ---------------------

TEST_F(ReaderViewerServiceTest, DecryptChapter_ValidRoundTrip_RecoversByteIdenticalPlaintext) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", alice.private_key_pem);
    const std::string original_text = "Chapter One: the true content of the story.";
    const ChapterFile file =
        build_chapter_file(crypto_, "alice", alice.public_key_base64, original_text);
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto result = service.decrypt_chapter("alice", file);

    ASSERT_EQ(result.outcome, DecryptChapterOutcome::success);
    EXPECT_EQ(result.plaintext, original_text);
}

// Edge case: Unicode/emoji and larger content must survive the round trip byte-for-byte.
TEST_F(ReaderViewerServiceTest, DecryptChapter_UnicodeAndEmojiContent_RoundTrips) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", alice.private_key_pem);
    const std::string original_text = u8"Café \U0001F4DA 日本語";
    const ChapterFile file =
        build_chapter_file(crypto_, "alice", alice.public_key_base64, original_text);
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto result = service.decrypt_chapter("alice", file);

    ASSERT_EQ(result.outcome, DecryptChapterOutcome::success);
    EXPECT_EQ(result.plaintext, original_text);
}

// Acceptance Criterion 6: tampered ciphertext must fail loudly, never garbage output.
TEST_F(ReaderViewerServiceTest, DecryptChapter_TamperedCiphertext_ReturnsDecryptionFailed_NoPlaintext) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", alice.private_key_pem);
    ChapterFile file =
        build_chapter_file(crypto_, "alice", alice.public_key_base64, "original content");
    Bytes ciphertext = CryptoProvider::base64_decode(file.ciphertext_base64);
    ASSERT_FALSE(ciphertext.empty());
    ciphertext[0] ^= 0xFF;
    file.ciphertext_base64 = CryptoProvider::base64_encode(ciphertext);
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto result = service.decrypt_chapter("alice", file);

    EXPECT_EQ(result.outcome, DecryptChapterOutcome::decryption_failed);
    EXPECT_TRUE(result.plaintext.empty());
}

// Acceptance Criterion 6 / Edge Cases: a private key that doesn't match what the writer
// registered (e.g. after local key loss and regeneration) must fail clearly, not silently.
TEST_F(ReaderViewerServiceTest, DecryptChapter_MismatchedLocalPrivateKey_ReturnsKeyUnwrapFailed_NoPlaintext) {
    const RsaKeyPair registered = CryptoProvider::generate_rsa_keypair();
    const RsaKeyPair regenerated = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", regenerated.private_key_pem);  // local key no longer matches.
    const ChapterFile file =
        build_chapter_file(crypto_, "alice", registered.public_key_base64, "original content");
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto result = service.decrypt_chapter("alice", file);

    EXPECT_EQ(result.outcome, DecryptChapterOutcome::key_unwrap_failed);
    EXPECT_TRUE(result.plaintext.empty());
}

// Edge case: no local private key stored at all for this username -- must not crash.
TEST_F(ReaderViewerServiceTest, DecryptChapter_NoLocalPrivateKeyStored_ReturnsKeyUnwrapFailed) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    const ChapterFile file =
        build_chapter_file(crypto_, "alice", alice.public_key_base64, "original content");
    ReaderViewerService service(key_store_, crypto_, console_);  // key_store_ never seeded.

    const auto result = service.decrypt_chapter("alice", file);

    EXPECT_EQ(result.outcome, DecryptChapterOutcome::key_unwrap_failed);
    EXPECT_TRUE(result.plaintext.empty());
}

// Edge case: this reader isn't in the file's wrappedKeys at all -- defensive, must not crash
// (menu-building already filters these out, but decrypt_chapter must still handle it safely).
TEST_F(ReaderViewerServiceTest, DecryptChapter_ReaderNotInWrappedKeys_ReturnsReaderNotWrapped) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    key_store_.seed("alice", alice.private_key_pem);
    const RsaKeyPair bob = CryptoProvider::generate_rsa_keypair();
    const ChapterFile file =
        build_chapter_file(crypto_, "bob", bob.public_key_base64, "original content");
    ReaderViewerService service(key_store_, crypto_, console_);

    const auto result = service.decrypt_chapter("alice", file);

    EXPECT_EQ(result.outcome, DecryptChapterOutcome::reader_not_wrapped);
    EXPECT_TRUE(result.plaintext.empty());
}

// --- Spec 005: CLI output styling -----------------------------------------------------------
//
// Categorization rule item 6 example "ReaderViewerService.cpp:31" (new-keypair message) is info.

TEST_F(ReaderViewerServiceTest, EnsureLocalKeypair_NoStoredKey_NewKeypairMessageStyledInfo) {
    ReaderViewerService service(key_store_, crypto_, console_);

    service.ensure_local_keypair("alice");

    const auto& styles = console_.style_calls();
    const auto it = std::find_if(styles.begin(), styles.end(), [](const auto& call) {
        return call.message.find("generated a new one for you") != std::string::npos;
    });
    ASSERT_NE(it, styles.end());
    EXPECT_EQ(it->style, MessageStyle::info);
}

}  // namespace
