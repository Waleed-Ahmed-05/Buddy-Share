#ifndef BUDDYSHARE_WRITER_READERACCESSCONTROLLER_H
#define BUDDYSHARE_WRITER_READERACCESSCONTROLLER_H

#include <unordered_set>

#include "shared/AccessRegistry.h"
#include "shared/Console.h"
#include "writer/GitProcess.h"

namespace buddyshare::writer {

// Validated result of a level/chapter-list prompt sequence (registration or edit).
struct RegistrationDetails {
    shared::AccessLevel level{shared::AccessLevel::master};
    std::unordered_set<int> chapters;  // empty for Master; exactly one entry for Novice.
};

// The one place in the codebase that knows spec 003's level/chapter-list rules. Reached from
// WriterMenu's "Manage reader access" item, and called into by ChapterEncryptionService (spec
// 002 steps 2 and 6) during registration and per-chapter wrap decisions.
class ReaderAccessController {
public:
    ReaderAccessController(shared::AccessRegistry& registry, shared::IConsole& console,
                            IGitProcess& git_process);

    // Always prompts for a level (re-prompting on anything other than Master/Apprentice/
    // Novice, case-insensitively). Apprentice then prompts for a comma-separated chapter list
    // (re-prompting until non-empty, all-numeric, and duplicate-free). Novice then prompts for
    // exactly one chapter number (re-prompting otherwise). Master requires no further prompt.
    // Never returns invalid data. Does not touch the registry -- the caller (spec 002's
    // ChapterEncryptionService) is responsible for AccessRegistry::append_reader.
    RegistrationDetails collect_registration_details();

    // Looks up `name` via registry_.find_by_name() first; if absent, returns false without
    // prompting (editing is not an implicit registration). Otherwise prompts using the same
    // validation as collect_registration_details() and applies the result via
    // registry_.update_reader(name, ...), leaving the reader's name/public key untouched. On a
    // successful update, `git add access.txt`, commits (e.g. "chore: update access for <name>
    // to <level>"), and pushes -- mirroring ChapterEncryptionService::register_reader (spec 003
    // Acceptance Criterion 3). No git operations run if the name doesn't exist.
    bool edit_existing_reader(const std::string& name);

    // True iff entry.level is Master, or entry.level is Apprentice and chapter_number is in
    // entry.chapters, or entry.level is Novice and chapter_number is entry's single chapter.
    // A chapter_number that doesn't correspond to a chapter written yet is not an error -- the
    // check is purely numeric membership.
    static bool is_authorized_for_chapter(const shared::ReaderEntry& entry, int chapter_number);

private:
    shared::AccessRegistry& registry_;
    shared::IConsole& console_;
    IGitProcess& git_process_;

    RegistrationDetails prompt_for_details();
};

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_READERACCESSCONTROLLER_H
