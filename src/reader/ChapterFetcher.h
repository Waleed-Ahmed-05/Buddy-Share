#ifndef BUDDYSHARE_READER_CHAPTERFETCHER_H
#define BUDDYSHARE_READER_CHAPTERFETCHER_H

#include <optional>
#include <string>
#include <vector>

#include "shared/ChapterFile.h"
#include "shared/GitHubClient.h"

namespace buddyshare::reader {

// Outcome of ChapterFetcher::fetch_access_txt (spec 004 Behavior step 3).
enum class FetchAccessTxtOutcome { success, not_found, network_error };

struct FetchAccessTxtResult {
    FetchAccessTxtOutcome outcome{FetchAccessTxtOutcome::network_error};
    std::string raw_content;    // decoded access.txt text; only meaningful when outcome == success.
    std::string error_message;  // only meaningful when outcome == network_error.
};

// Whether a given chapter is actually decryptable by this reader right now (spec 004 Behavior
// step 6) -- distinct from whether access.txt nominally permits it (specs 002/003's "no
// retroactive re-wrap" rule can put these two out of sync).
enum class ChapterAvailability { readable, pending };

struct ChapterFetchResult {
    ChapterAvailability availability{ChapterAvailability::pending};
    std::optional<shared::ChapterFile> file;  // populated only when availability == readable.
    bool rate_limited{false};  // true on a 403 -- never folded into the ordinary "pending" case.
};

// Result of list_master_candidates(): distinguishes a rate-limited directory listing (403) from
// a genuinely empty/missing chapters/ folder, both of which otherwise produce zero candidates.
struct MasterCandidatesResult {
    std::vector<int> candidates;
    bool rate_limited{false};
};

// Fetch/list/verify-decryptability logic (spec 004 Behavior steps 3, 5, 6), via the shared
// IGitHubClient's Contents API only -- no git, no token (spec 004 Scope: the target repo must
// be public; a private repo's 404s are indistinguishable from "doesn't exist").
class ChapterFetcher {
public:
    ChapterFetcher(shared::IGitHubClient& github_client, std::string owner, std::string repo);

    // Step 3: GET contents/access.txt. Never parses the content itself -- AccessRegistry (spec
    // 002/003) remains the single parser for both roles; the caller feeds raw_content into it.
    FetchAccessTxtResult fetch_access_txt();

    // Step 5, Master path: lists the chapters/ folder; every entry matching chapter-<NN>.enc
    // (spec 002's pinned naming convention) becomes a candidate chapter number, returned sorted
    // ascending (reused as the menu's iteration order). A missing/empty folder is zero
    // candidates, not an error -- there is no 404 case for this path once listed. A 403
    // (rate_limited) is reported via the result's rate_limited flag, distinguishable from a
    // genuinely empty/missing folder even though both yield zero candidates.
    MasterCandidatesResult list_master_candidates();

    // Step 6: fetches chapters/chapter-<NN>.enc (2-digit zero-padded) and reports whether
    // reader_name's wrapped key is actually present in it. A 404 (not yet published) and a 200
    // whose wrappedKeys is missing reader_name are both reported as pending, never an error --
    // see spec 004 Behavior item 6. Malformed/unparseable file content and network errors are
    // also folded into pending rather than surfaced as a hard error, so one bad chapter never
    // crashes the whole menu build. A 403 sets rate_limited=true instead, and is never folded
    // into the ordinary pending case.
    ChapterFetchResult try_fetch_chapter(int chapter_number, const std::string& reader_name);

private:
    shared::IGitHubClient& github_client_;
    std::string owner_;
    std::string repo_;
};

}  // namespace buddyshare::reader

#endif  // BUDDYSHARE_READER_CHAPTERFETCHER_H
