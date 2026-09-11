#ifndef BUDDYSHARE_WRITER_WRITERMENU_H
#define BUDDYSHARE_WRITER_WRITERMENU_H

#include <vector>

#include "writer/GitBootstrapService.h"

namespace buddyshare::writer {

// The Writer role's menu loop. Spec 001 only owns the "Initialize repo" entry; later specs
// (002/003) add the chapter/access-management entries into the same dispatch table. No
// command in this table ever changes the locked-in role (enforced structurally: there is no
// such WriterCommand value, not by a runtime check).
//
// manage_reader_access (spec 003) and encrypt_chapter (spec 002) are both reachable once the
// bootstrap has already completed -- mirrors initialize_repo's condition in reverse, and both
// are offered together (not just one or the other) once that precondition is met.
enum class WriterCommand { initialize_repo, manage_reader_access, encrypt_chapter };

class WriterMenu {
public:
    explicit WriterMenu(GitBootstrapService& bootstrap);

    // "Initialize repo" is present only when the bootstrap hasn't run yet.
    std::vector<WriterCommand> available_commands() const;

private:
    GitBootstrapService& bootstrap_;
};

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_WRITERMENU_H
