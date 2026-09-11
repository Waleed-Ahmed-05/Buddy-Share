#ifndef BUDDYSHARE_SHARED_HTTPGITHUBCLIENT_H
#define BUDDYSHARE_SHARED_HTTPGITHUBCLIENT_H

#include "shared/GitHubClient.h"

namespace buddyshare::shared {

// Real IGitHubClient: calls the live GitHub REST API over HTTPS via WinHTTP.
class HttpGitHubClient : public IGitHubClient {
public:
    RepoInfo get_repo(const std::string& owner, const std::string& repo,
                       const std::string& token) override;

    // Never sends an Authorization header -- spec 004's reader role has no token support at
    // all (see specs/004-reader-viewer.md Scope: a private target repo is unsupported).
    ContentsResult get_contents(const std::string& owner, const std::string& repo,
                                  const std::string& path) override;
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_HTTPGITHUBCLIENT_H
