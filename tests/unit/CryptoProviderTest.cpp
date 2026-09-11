#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "shared/CryptoProvider.h"

using buddyshare::shared::Bytes;
using buddyshare::shared::CryptoProvider;
using buddyshare::shared::DerivedKey;
using buddyshare::shared::EncryptedContent;
using buddyshare::shared::RsaKeyPair;

namespace {

Bytes to_bytes(const std::string& text) { return Bytes(text.begin(), text.end()); }

std::string to_string(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

class CryptoProviderTest : public ::testing::Test {
protected:
    CryptoProvider crypto_;
};

// --- derive_key: PBKDF2-HMAC-SHA256 ---------------------------------------------------------

TEST_F(CryptoProviderTest, DeriveKey_ProducesThirtyTwoByteKeyForAes256) {
    const DerivedKey derived = crypto_.derive_key("correct horse battery staple");

    EXPECT_EQ(derived.key.size(), 32u);
    EXPECT_FALSE(derived.salt.empty());
}

TEST_F(CryptoProviderTest, DeriveKey_TwoCallsWithSamePassword_ProduceDifferentRandomSalts) {
    const DerivedKey first = crypto_.derive_key("same password");
    const DerivedKey second = crypto_.derive_key("same password");

    EXPECT_NE(first.salt, second.salt);
}

TEST_F(CryptoProviderTest, DeriveKeyWithSalt_SamePasswordAndSalt_IsDeterministic) {
    const DerivedKey original = crypto_.derive_key("same password");

    const Bytes rederived = crypto_.derive_key_with_salt("same password", original.salt);

    EXPECT_EQ(rederived, original.key);
}

TEST_F(CryptoProviderTest, DeriveKeyWithSalt_DifferentPassword_ProducesDifferentKey) {
    const DerivedKey original = crypto_.derive_key("correct password");

    const Bytes wrong = crypto_.derive_key_with_salt("wrong password", original.salt);

    EXPECT_NE(wrong, original.key);
}

// --- encrypt/decrypt: AES-256-GCM -----------------------------------------------------------

TEST_F(CryptoProviderTest, EncryptThenDecrypt_RoundTrips_RecoversByteIdenticalPlaintext) {
    const DerivedKey derived = crypto_.derive_key("writer's password");
    const Bytes plaintext = to_bytes("Chapter One: It was a dark and stormy night.");

    const EncryptedContent encrypted = crypto_.encrypt(plaintext, derived.key);
    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, derived.key);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

// Edge case: empty plaintext must still round-trip (an empty chapter body isn't an error).
TEST_F(CryptoProviderTest, EncryptThenDecrypt_EmptyPlaintext_RoundTrips) {
    const DerivedKey derived = crypto_.derive_key("password");
    const Bytes plaintext;

    const EncryptedContent encrypted = crypto_.encrypt(plaintext, derived.key);
    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, derived.key);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_TRUE(decrypted->empty());
}

// Edge case: Unicode/emoji content must survive the round trip byte-for-byte.
TEST_F(CryptoProviderTest, EncryptThenDecrypt_UnicodeAndEmojiPlaintext_RoundTrips) {
    const DerivedKey derived = crypto_.derive_key("password");
    const Bytes plaintext = to_bytes(u8"Café \xF0\x9F\x93\x9A \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");

    const EncryptedContent encrypted = crypto_.encrypt(plaintext, derived.key);
    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, derived.key);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

// Edge case: large data (10k+ bytes) round-trips correctly, not just small test strings.
TEST_F(CryptoProviderTest, EncryptThenDecrypt_LargePlaintext_RoundTrips) {
    const DerivedKey derived = crypto_.derive_key("password");
    std::string large_text;
    large_text.reserve(250000);
    for (int i = 0; i < 250000; ++i) large_text.push_back(static_cast<char>('a' + (i % 26)));
    const Bytes plaintext = to_bytes(large_text);

    const EncryptedContent encrypted = crypto_.encrypt(plaintext, derived.key);
    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, derived.key);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

TEST_F(CryptoProviderTest, Encrypt_TwoCallsWithSameKey_ProduceDifferentIvAndCiphertext) {
    const DerivedKey derived = crypto_.derive_key("password");
    const Bytes plaintext = to_bytes("identical plaintext");

    const EncryptedContent first = crypto_.encrypt(plaintext, derived.key);
    const EncryptedContent second = crypto_.encrypt(plaintext, derived.key);

    EXPECT_NE(first.iv, second.iv);
    EXPECT_NE(first.ciphertext, second.ciphertext);
}

TEST_F(CryptoProviderTest, Decrypt_WrongKey_ReturnsNulloptRatherThanGarbagePlaintext) {
    const DerivedKey correct = crypto_.derive_key("correct password");
    const DerivedKey wrong = crypto_.derive_key("wrong password");
    const EncryptedContent encrypted = crypto_.encrypt(to_bytes("secret content"), correct.key);

    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, wrong.key);

    EXPECT_FALSE(decrypted.has_value());
}

// Acceptance criterion 4: tampering with one byte of ciphertext must fail loudly (GCM
// auth-tag mismatch), never silently return corrupted plaintext.
TEST_F(CryptoProviderTest, Decrypt_OneByteTamperedCiphertext_FailsAuthenticationRatherThanReturningCorruptPlaintext) {
    const DerivedKey derived = crypto_.derive_key("password");
    EncryptedContent encrypted = crypto_.encrypt(to_bytes("untampered original content"), derived.key);
    ASSERT_FALSE(encrypted.ciphertext.empty());
    encrypted.ciphertext[0] ^= 0xFF;

    const std::optional<Bytes> decrypted = crypto_.decrypt(encrypted, derived.key);

    EXPECT_FALSE(decrypted.has_value());
}

// --- wrap_key/unwrap_key: RSA-OAEP ------------------------------------------------------------

TEST_F(CryptoProviderTest, WrapKeyThenUnwrapKey_RoundTrips_RecoversOriginalAesKey) {
    const RsaKeyPair keypair = CryptoProvider::generate_rsa_keypair();
    const DerivedKey derived = crypto_.derive_key("password");

    const Bytes wrapped = crypto_.wrap_key(derived.key, keypair.public_key_base64);
    const std::optional<Bytes> unwrapped = crypto_.unwrap_key(wrapped, keypair.private_key_pem);

    ASSERT_TRUE(unwrapped.has_value());
    EXPECT_EQ(*unwrapped, derived.key);
}

// Acceptance criterion 3: a reader's wrapped-key entry is only unwrappable with that reader's
// own matching private key.
TEST_F(CryptoProviderTest, UnwrapKey_WithAnotherReadersPrivateKey_ReturnsNullopt) {
    const RsaKeyPair alice = CryptoProvider::generate_rsa_keypair();
    const RsaKeyPair bob = CryptoProvider::generate_rsa_keypair();
    const DerivedKey derived = crypto_.derive_key("password");
    const Bytes wrapped_for_alice = crypto_.wrap_key(derived.key, alice.public_key_base64);

    const std::optional<Bytes> unwrapped_with_bobs_key =
        crypto_.unwrap_key(wrapped_for_alice, bob.private_key_pem);

    EXPECT_FALSE(unwrapped_with_bobs_key.has_value());
}

TEST_F(CryptoProviderTest, GenerateRsaKeypair_TwoCalls_ProduceDistinctKeypairs) {
    const RsaKeyPair first = CryptoProvider::generate_rsa_keypair();
    const RsaKeyPair second = CryptoProvider::generate_rsa_keypair();

    EXPECT_NE(first.public_key_base64, second.public_key_base64);
    EXPECT_NE(first.private_key_pem, second.private_key_pem);
}

// --- base64_encode/base64_decode --------------------------------------------------------------

TEST_F(CryptoProviderTest, Base64EncodeThenDecode_RoundTrips) {
    const Bytes original = to_bytes("arbitrary binary-looking content \x00\x01\xFF");

    const std::string encoded = CryptoProvider::base64_encode(original);
    const Bytes decoded = CryptoProvider::base64_decode(encoded);

    EXPECT_EQ(decoded, original);
}

TEST_F(CryptoProviderTest, Base64Encode_EmptyInput_ProducesEmptyString) {
    EXPECT_TRUE(CryptoProvider::base64_encode(Bytes{}).empty());
}

}  // namespace
