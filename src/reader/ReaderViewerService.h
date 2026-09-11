#ifndef BUDDYSHARE_READER_READERVIEWERSERVICE_H
#define BUDDYSHARE_READER_READERVIEWERSERVICE_H

#include <optional>
#include <string>

#include "shared/ChapterFile.h"
#include "shared/Console.h"
#include "shared/CryptoProvider.h"

namespace buddyshare::reader {

// Abstraction over the local, DPAPI-protected private-key file
// (%APPDATA%\BuddyShare\keys\<username>_private.pem), so ReaderViewerService can be
// unit-tested without touching the real filesystem/DPAPI.
class IPrivateKeyStore {
public:
    virtual ~IPrivateKeyStore() = default;

    // Returns the stored PEM private key for username, or std::nullopt if none is stored yet.
    virtual std::optional<std::string> read_private_key_pem(const std::string& username) const = 0;

    // Stores private_key_pem for username, overwriting any previous value.
    virtual void write_private_key_pem(const std::string& username,
                                        const std::string& private_key_pem) = 0;
};

// Outcome of ReaderViewerService::ensure_local_keypair (spec 004 Behavior step 2).
struct KeypairInfo {
    bool newly_generated{false};
    std::string public_key_base64;
};

// Outcome of ReaderViewerService::decrypt_chapter (spec 004 Behavior step 8).
enum class DecryptChapterOutcome {
    success,
    reader_not_wrapped,   // file has no wrappedKeys entry for this reader at all.
    key_unwrap_failed,    // RSA-OAEP unwrap failed -- no local key, or a mismatched keypair.
    decryption_failed,    // AES-256-GCM auth-tag failure -- tampered/corrupted ciphertext.
};

struct DecryptChapterResult {
    DecryptChapterOutcome outcome{DecryptChapterOutcome::decryption_failed};
    std::string plaintext;  // only meaningful when outcome == success; empty otherwise.
};

// Keypair management and decryption (spec 004 Behavior steps 1, 2, 8). Never prints or
// otherwise exposes private key material, the recovered AES key, or plaintext to the console --
// the only thing this class ever intentionally displays is the reader's own public key.
class ReaderViewerService {
public:
    ReaderViewerService(IPrivateKeyStore& key_store, shared::CryptoProvider& crypto,
                         shared::IConsole& console);

    // Step 2: reuses username's stored private key if one exists; otherwise generates a fresh
    // RSA keypair, stores the private half via key_store_, and prints the public half with
    // send-to-writer instructions. Always returns the reader's current public key either way,
    // so the caller can redisplay it later (e.g. spec 004 step 4's "not registered yet"
    // message) without needing to have just generated it.
    KeypairInfo ensure_local_keypair(const std::string& username);

    // Step 8: RSA-OAEP-unwraps username's wrappedKeys entry in file using their locally stored
    // private key, then AES-256-GCM-decrypts file's ciphertext under the recovered key and iv.
    // Never returns partial/garbage plaintext -- every non-success outcome leaves plaintext
    // empty.
    DecryptChapterResult decrypt_chapter(const std::string& username,
                                          const shared::ChapterFile& file) const;

private:
    IPrivateKeyStore& key_store_;
    shared::CryptoProvider& crypto_;
    shared::IConsole& console_;
};

}  // namespace buddyshare::reader

#endif  // BUDDYSHARE_READER_READERVIEWERSERVICE_H
