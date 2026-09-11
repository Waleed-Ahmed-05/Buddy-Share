#ifndef BUDDYSHARE_WRITER_SYSTEMGITPROCESS_H
#define BUDDYSHARE_WRITER_SYSTEMGITPROCESS_H

#include <string>

#include "writer/GitProcess.h"

namespace buddyshare::writer {

// Real IGitProcess: spawns the system `git` (and, if missing, `winget`) as a child process
// in working_directory, capturing its combined stdout/stderr as GitResult::std_out.
class SystemGitProcess : public IGitProcess {
public:
    explicit SystemGitProcess(std::string working_directory);

    bool is_git_available() override;
    bool install_git_via_winget() override;
    GitResult run(const std::vector<std::string>& args) override;

private:
    std::string working_directory_;
};

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_SYSTEMGITPROCESS_H
