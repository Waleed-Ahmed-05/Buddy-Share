#ifndef BUDDYSHARE_WRITER_GITPROCESS_H
#define BUDDYSHARE_WRITER_GITPROCESS_H

#include <string>
#include <vector>

namespace buddyshare::writer {

struct GitResult {
    int exit_code{0};
    std::string std_out;
    std::string std_err;
};

// Abstraction over invoking the system `git` binary as a child process, so
// GitBootstrapService can be unit-tested without a real git install or network.
class IGitProcess {
public:
    virtual ~IGitProcess() = default;

    virtual bool is_git_available() = 0;

    // Runs `winget install --id Git.Git -e --source winget`. Returns whether the install
    // appeared to succeed; the caller re-checks is_git_available() afterward regardless.
    virtual bool install_git_via_winget() = 0;

    // Runs `git <args...>` in the configured working directory.
    virtual GitResult run(const std::vector<std::string>& args) = 0;
};

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_GITPROCESS_H
