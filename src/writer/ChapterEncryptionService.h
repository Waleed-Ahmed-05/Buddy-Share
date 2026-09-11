#ifndef BUDDYSHARE_WRITER_CHAPTERENCRYPTIONSERVICE_H
#define BUDDYSHARE_WRITER_CHAPTERENCRYPTIONSERVICE_H

#include <string>

#include "shared/AccessRegistry.h"
#include "shared/Console.h"
#include "shared/CryptoProvider.h"
#include "writer/GitProcess.h"
#include "writer/ReaderAccessController.h"

namespace buddyshare::writer {

enum class EncryptChapterOutcome {
    not_bootstrapped,      // no .git, or no origin remote -- run spec 001's bootstrap first.
    plaintext_not_found,   // plaintext_path couldn't be read.
    success,
    some_readers_skipped,  // file was still written/committed/pushed, but one or more
                            // authorized readers' public keys were malformed and could not be
                            // wrapped -- see the wrap-loop comment in encrypt_chapter.
    push_failed,           // the .enc file was written and committed locally, but `git push`
                            // failed -- readers cannot see this chapter yet (bugfix: this must
                            // never be reported as success).
};

struct EncryptChapterResult {
    EncryptChapterOutcome outcome{EncryptChapterOutcome::not_bootstrapped};
    std::string message;
    int chapter_number{0};
    int reader_count{0};  // how many readers' keys were wrapped into the output file.
};

// Spec 002's "Encrypt a chapter" WriterMenu item, reachable only once spec 001's bootstrap has
// already completed in this folder (it performs no first-time git setup itself). Reads
// plaintext from disk, encrypts it (CryptoProvider), wraps the AES content-key once per
// reader in registry_ that ReaderAccessController::is_authorized_for_chapter authorizes for
// this chapter, writes chapters/chapter-<NN>.enc (ChapterFile), then commits and pushes it
// directly via git_process_ -- reusing the origin/tracking spec 001 already configured (see
// specs/002-chapter-encryption.md, Behavior items 1-8).
class ChapterEncryptionService {
public:
    ChapterEncryptionService(std::string working_directory, shared::AccessRegistry& registry,
                              ReaderAccessController& access_controller,
                              shared::CryptoProvider& crypto, IGitProcess& git_process,
                              shared::IConsole& console);

    // Behavior items 1-8: checks the bootstrap precondition; if registry_ has zero readers
    // (or unconditionally, per step 2) offers to register one now via register_reader();
    // prompts for the encryption password (masked, re-prompting on a blank answer); derives
    // the AES key with a random salt; reads plaintext_path and AES-256-GCM-encrypts it;
    // RSA-OAEP-wraps the AES key for every authorized reader; writes
    // chapters/chapter-<NN>.enc; then `git add`/`commit`/`push`s that one file. Plaintext is
    // never staged, committed, or pushed. A reader registered after a previous chapter was
    // encrypted does not retroactively affect that earlier chapter's file (Edge Cases). A
    // reader whose stored public key is malformed (fails to parse as RSA SPKI/DER) is skipped
    // with a console warning -- the same "skip + warn, don't abort the whole run" treatment
    // AccessRegistry gives a malformed access.txt line -- rather than letting the exception
    // escape and crash the process; the result's outcome reflects this via
    // some_readers_skipped instead of success.
    EncryptChapterResult encrypt_chapter(int chapter_number, const std::string& plaintext_path);

    // Behavior step 2: prompts for a new reader's display name and public key, delegates
    // level/chapter-list collection to access_controller_, then
    // registry_.append_reader(...); on success, `git add access.txt`, commits (e.g.
    // "chore: add reader <name> to access.txt"), and pushes. Returns false -- no git
    // operations run -- if the registry refused the append (e.g. duplicate name); the
    // registry itself already printed the reason via console_.
    bool register_reader();

private:
    std::string working_directory_;
    shared::AccessRegistry& registry_;
    ReaderAccessController& access_controller_;
    shared::CryptoProvider& crypto_;
    IGitProcess& git_process_;
    shared::IConsole& console_;

    // True only if a .git directory exists in working_directory_ AND an `origin` remote is
    // configured (spec 002 Edge Cases: either missing condition is the same "run bootstrap
    // first" error, not a crash, and not an attempt to perform first-time git setup itself).
    bool is_bootstrapped() const;

    // Behavior step 1/2 combined: warns if registry_.all_readers() is empty, then always asks
    // "Add a reader? (y/n)"; calls register_reader() on "y". Never silently proceeds with a
    // zero-reader registry without this explicit prompt (Acceptance Criterion 5).
    void maybe_register_reader();

    // Prompts (masked) until a non-blank password is entered (Edge Cases: blank password ->
    // re-prompt, never proceed with an empty password).
    std::string prompt_for_password() const;
};

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_CHAPTERENCRYPTIONSERVICE_H
