#include "reader/ReaderViewerService.h"

namespace buddyshare::reader {

namespace {
constexpr const char* kNewKeypairMessage =
    "No local key found -- generated a new one for you. Send this public key to the writer so "
    "they can grant you access:";
}  // namespace

ReaderViewerService::ReaderViewerService(IPrivateKeyStore& key_store, shared::CryptoProvider& crypto,
                                           shared::IConsole& console)
    : key_store_(key_store), crypto_(crypto), console_(console) {}

KeypairInfo ReaderViewerService::ensure_local_keypair(const std::string& username) {
    const std::optional<std::string> existing = key_store_.read_private_key_pem(username);
    if (existing.has_value()) {
        KeypairInfo info;
        info.newly_generated = false;
        info.public_key_base64 =
            shared::CryptoProvider::public_key_from_private(*existing).value_or("");
        return info;
    }

    const shared::RsaKeyPair generated = shared::CryptoProvider::generate_rsa_keypair();
    key_store_.write_private_key_pem(username, generated.private_key_pem);

    KeypairInfo info;
    info.newly_generated = true;
    info.public_key_base64 = generated.public_key_base64;
    console_.print(kNewKeypairMessage, shared::MessageStyle::info);
    console_.print(info.public_key_base64);
    return info;
}

DecryptChapterResult ReaderViewerService::decrypt_chapter(const std::string& username,
                                                             const shared::ChapterFile& file) const {
    DecryptChapterResult result;

    const auto wrapped_it = file.wrapped_keys_base64.find(username);
    if (wrapped_it == file.wrapped_keys_base64.end()) {
        result.outcome = DecryptChapterOutcome::reader_not_wrapped;
        return result;
    }

    const std::optional<std::string> private_key_pem = key_store_.read_private_key_pem(username);
    if (!private_key_pem.has_value()) {
        result.outcome = DecryptChapterOutcome::key_unwrap_failed;
        return result;
    }

    const shared::Bytes wrapped_key = shared::CryptoProvider::base64_decode(wrapped_it->second);
    const std::optional<shared::Bytes> aes_key = crypto_.unwrap_key(wrapped_key, *private_key_pem);
    if (!aes_key.has_value()) {
        result.outcome = DecryptChapterOutcome::key_unwrap_failed;
        return result;
    }

    shared::EncryptedContent encrypted;
    encrypted.iv = shared::CryptoProvider::base64_decode(file.iv_base64);
    encrypted.ciphertext = shared::CryptoProvider::base64_decode(file.ciphertext_base64);

    const std::optional<shared::Bytes> plaintext_bytes = crypto_.decrypt(encrypted, *aes_key);
    if (!plaintext_bytes.has_value()) {
        result.outcome = DecryptChapterOutcome::decryption_failed;
        return result;
    }

    result.outcome = DecryptChapterOutcome::success;
    result.plaintext.assign(plaintext_bytes->begin(), plaintext_bytes->end());
    return result;
}

}  // namespace buddyshare::reader
