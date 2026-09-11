#ifndef BUDDYSHARE_WRITER_GITBOOTSTRAPSERVICE_H
#define BUDDYSHARE_WRITER_GITBOOTSTRAPSERVICE_H

#include <string>

#include "shared/Console.h"
#include "shared/GitHubClient.h"
#include "writer/GitProcess.h"

namespace buddyshare::writer {

enum class BootstrapOutcome {
    already_bootstrapped,
    git_unavailable,
    repo_not_found,
    unauthorized,
    network_error,
    remote_not_empty,
    push_failed,
    success,
};

struct BootstrapResult {
    BootstrapOutcome outcome{BootstrapOutcome::already_bootstrapped};
    std::string message;   // human-readable; never contains a token, even on failure.
    std::string repo_url;  // populated on success (token-free HTTPS URL).
    std::string branch;    // populated on success (the remote's actual default branch).
};

// One-time git bootstrap for the Writer role (spec 001, sections "1-7").
class GitBootstrapService {
public:
    GitBootstrapService(std::string working_directory, shared::IGitHubClient& github_client,
                         IGitProcess& git_process, shared::IConsole& console);

    // Step 1: true if a .git directory already exists in working_directory.
    bool is_already_bootstrapped() const;

    // Steps 2-7. Callers should check is_already_bootstrapped() first (WriterMenu hides the
    // menu item in that case); calling run() when already bootstrapped still no-ops safely.
    BootstrapResult run();

private:
    std::string working_directory_;
    shared::IGitHubClient& github_client_;
    IGitProcess& git_process_;
    shared::IConsole& console_;
};

// Normalizes a user-supplied repo name: strips a trailing ".git" suffix and, if a full URL
// was pasted instead of a bare name, reduces it to the bare repo name.
std::string normalize_repo_name(const std::string& input);

}  // namespace buddyshare::writer

#endif  // BUDDYSHARE_WRITER_GITBOOTSTRAPSERVICE_H
