#ifndef BUDDYSHARE_SHARED_GITHUBCLIENT_H
#define BUDDYSHARE_SHARED_GITHUBCLIENT_H

#include <string>
#include <vector>

namespace buddyshare::shared {

// Outcome of GET /repos/{owner}/{repo}. Exactly one of the status flags is true.
struct RepoInfo {
    bool exists{false};              // 200: repo found; check has_commits before proceeding.
    bool ambiguous_not_found{false}; // 404: missing OR private-with-no-token (indistinguishable).
    bool unauthorized{false};        // 401: token present but rejected.
    bool network_error{false};       // any transport-level failure.
    bool rate_limited{false};        // 403: GitHub's request limit was hit (network_error is
                                      // also set so existing error_message-only callers work).
    std::string error_message;       // populated when network_error is true.
    std::string default_branch;      // only meaningful when exists is true.
    bool has_commits{false};         // only meaningful when exists is true.
};

// One entry in a Contents API directory listing (GitHub's `type` field is "dir" or "file").
struct ContentsEntry {
    std::string name;
    bool is_directory{false};
};

// Outcome of GET /repos/{owner}/{repo}/contents/{path} (spec 004's reader-side fetches --
// access.txt, the chapters/ directory listing, and individual chapter-<NN>.enc files). Exactly
// one of exists/not_found/network_error is true.
struct ContentsResult {
    bool exists{false};         // 200.
    bool not_found{false};      // 404 -- a missing file/folder, not necessarily an error; see
                                 // each caller's own handling (e.g. spec 004 step 5's empty
                                 // chapters/ folder).
    bool network_error{false};
    bool rate_limited{false};    // 403: GitHub's request limit was hit (network_error is also
                                  // set so existing error_message-only callers work).
    std::string error_message;  // populated when network_error is true.

    // Populated when path pointed at a single file: its content, already base64-decoded --
    // GitHub's Contents API returns file content as base64 in the JSON response, but callers
    // here only ever want the decoded text.
    std::string decoded_content;

    // Populated when path pointed at a directory: its entries, in the order GitHub returned
    // them (unsorted -- callers needing a sorted order, e.g. spec 004's Master candidate list,
    // sort it themselves).
    std::vector<ContentsEntry> entries;
};

// Shared GitHub REST API client (also used by spec 004's Contents API calls).
class IGitHubClient {
public:
    virtual ~IGitHubClient() = default;

    // GET https://api.github.com/repos/{owner}/{repo}. Sends
    // "Authorization: token {token}" only when token is non-empty.
    virtual RepoInfo get_repo(const std::string& owner, const std::string& repo,
                               const std::string& token) = 0;

    // GET https://api.github.com/repos/{owner}/{repo}/contents/{path}. Never sends an
    // Authorization header -- spec 004's reader role has no token support at all; a private
    // target repo is indistinguishable from a missing one (404) by design (see
    // specs/004-reader-viewer.md Scope).
    virtual ContentsResult get_contents(const std::string& owner, const std::string& repo,
                                          const std::string& path) = 0;
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_GITHUBCLIENT_H
