#include "reader/ChapterFetcher.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace buddyshare::reader {

namespace {

constexpr const char* kAccessTxtPath = "access.txt";
constexpr const char* kChaptersDirPath = "chapters";
constexpr const char* kChapterPrefix = "chapter-";
constexpr const char* kChapterSuffix = ".enc";

std::string chapter_path(int chapter_number) {
    std::ostringstream path;
    path << kChaptersDirPath << '/' << kChapterPrefix << std::setfill('0') << std::setw(2)
         << chapter_number << kChapterSuffix;
    return path.str();
}

// Parses "chapter-<NN>.enc" into its chapter number; anything else (wrong prefix/suffix,
// non-numeric middle, a directory) returns std::nullopt so the caller skips it rather than
// misparsing it (spec 004 Behavior step 5's "ignores non-matching entries").
std::optional<int> parse_chapter_number(const std::string& file_name) {
    const std::string prefix(kChapterPrefix);
    const std::string suffix(kChapterSuffix);
    if (file_name.size() <= prefix.size() + suffix.size()) return std::nullopt;
    if (file_name.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    if (file_name.compare(file_name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }

    const std::string digits =
        file_name.substr(prefix.size(), file_name.size() - prefix.size() - suffix.size());
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }

    try {
        return std::stoi(digits);
    } catch (const std::exception&) {
        return std::nullopt;  // too large for int -- not a chapter this project supports.
    }
}

}  // namespace

ChapterFetcher::ChapterFetcher(shared::IGitHubClient& github_client, std::string owner,
                                 std::string repo)
    : github_client_(github_client), owner_(std::move(owner)), repo_(std::move(repo)) {}

FetchAccessTxtResult ChapterFetcher::fetch_access_txt() {
    const shared::ContentsResult response =
        github_client_.get_contents(owner_, repo_, kAccessTxtPath);

    FetchAccessTxtResult result;
    if (response.exists) {
        result.outcome = FetchAccessTxtOutcome::success;
        result.raw_content = response.decoded_content;
    } else if (response.not_found) {
        result.outcome = FetchAccessTxtOutcome::not_found;
    } else {
        result.outcome = FetchAccessTxtOutcome::network_error;
        result.error_message =
            response.error_message.empty() ? "Network error contacting GitHub." : response.error_message;
    }
    return result;
}

MasterCandidatesResult ChapterFetcher::list_master_candidates() {
    const shared::ContentsResult response =
        github_client_.get_contents(owner_, repo_, kChaptersDirPath);

    MasterCandidatesResult result;
    if (response.rate_limited) {
        result.rate_limited = true;
        return result;
    }
    if (!response.exists) return result;  // missing chapters/ folder: zero candidates.

    for (const auto& entry : response.entries) {
        if (entry.is_directory) continue;
        const std::optional<int> chapter_number = parse_chapter_number(entry.name);
        if (chapter_number.has_value()) result.candidates.push_back(*chapter_number);
    }
    std::sort(result.candidates.begin(), result.candidates.end());
    return result;
}

ChapterFetchResult ChapterFetcher::try_fetch_chapter(int chapter_number,
                                                       const std::string& reader_name) {
    ChapterFetchResult result;  // defaults to pending, no file.

    const shared::ContentsResult response =
        github_client_.get_contents(owner_, repo_, chapter_path(chapter_number));
    if (response.rate_limited) {
        result.rate_limited = true;
        return result;  // a 403 is never folded into the ordinary "pending" case.
    }
    if (!response.exists) return result;  // 404 or network error: not yet published.

    const std::optional<shared::ChapterFile> parsed =
        shared::ChapterFile::from_json(response.decoded_content);
    if (!parsed.has_value()) return result;  // malformed content: never crash, just pending.

    if (parsed->wrapped_keys_base64.count(reader_name) == 0) return result;  // no retroactive re-wrap.

    result.availability = ChapterAvailability::readable;
    result.file = parsed;
    return result;
}

}  // namespace buddyshare::reader
