#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "reader/ChapterFetcher.h"
#include "shared/ChapterFile.h"
#include "shared/GitHubClient.h"
#include "support/Fakes.h"

using buddyshare::reader::ChapterAvailability;
using buddyshare::reader::ChapterFetcher;
using buddyshare::reader::FetchAccessTxtOutcome;
using buddyshare::shared::ChapterFile;
using buddyshare::shared::ContentsEntry;
using buddyshare::shared::ContentsResult;

namespace {

class ChapterFetcherTest : public ::testing::Test {
protected:
    FakeGitHubClient github_client_;
};

// --- fetch_access_txt (spec 004 Behavior step 3) --------------------------------------------

TEST_F(ChapterFetcherTest, FetchAccessTxt_Success_ReturnsRawDecodedContent) {
    ContentsResult response;
    response.exists = true;
    response.decoded_content = "alice:MIIB_alice_key:Master:\n";
    github_client_.set_contents_response("access.txt", response);
    ChapterFetcher fetcher(github_client_, "some-writer", "some-repo");

    const auto result = fetcher.fetch_access_txt();

    EXPECT_EQ(result.outcome, FetchAccessTxtOutcome::success);
    EXPECT_EQ(result.raw_content, "alice:MIIB_alice_key:Master:\n");
}

TEST_F(ChapterFetcherTest, FetchAccessTxt_RequestsExpectedOwnerRepoAndPath) {
    ChapterFetcher fetcher(github_client_, "some-writer", "some-repo");

    fetcher.fetch_access_txt();

    EXPECT_EQ(github_client_.last_contents_owner(), "some-writer");
    EXPECT_EQ(github_client_.last_contents_repo(), "some-repo");
    ASSERT_EQ(github_client_.contents_paths_requested().size(), 1u);
    EXPECT_EQ(github_client_.contents_paths_requested()[0], "access.txt");
}

// Edge case: repo/file not found -> a clear outcome, not a crash (spec 004 Edge Cases).
TEST_F(ChapterFetcherTest, FetchAccessTxt_NotFound_ReturnsNotFoundOutcome) {
    ContentsResult response;
    response.not_found = true;
    github_client_.set_contents_response("access.txt", response);
    ChapterFetcher fetcher(github_client_, "ghost-writer", "ghost-repo");

    const auto result = fetcher.fetch_access_txt();

    EXPECT_EQ(result.outcome, FetchAccessTxtOutcome::not_found);
}

// Edge case: network failure -> a clear error message, not a crash (spec 004 Edge Cases).
TEST_F(ChapterFetcherTest, FetchAccessTxt_NetworkError_ReturnsNetworkErrorOutcomeWithMessage) {
    ContentsResult response;
    response.network_error = true;
    response.error_message = "Network error contacting GitHub.";
    github_client_.set_contents_response("access.txt", response);
    ChapterFetcher fetcher(github_client_, "some-writer", "some-repo");

    const auto result = fetcher.fetch_access_txt();

    EXPECT_EQ(result.outcome, FetchAccessTxtOutcome::network_error);
    EXPECT_FALSE(result.error_message.empty());
}

// --- list_master_candidates (spec 004 Behavior step 5, Master path) --------------------------

TEST_F(ChapterFetcherTest, ListMasterCandidates_ParsesChapterFilenames_SortedAscending) {
    ContentsResult response;
    response.exists = true;
    response.entries = {
        {"chapter-03.enc", false},
        {"chapter-01.enc", false},
        {"chapter-10.enc", false},
        {"chapter-02.enc", false},
    };
    github_client_.set_contents_response("chapters", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.list_master_candidates();

    EXPECT_EQ(result.candidates, (std::vector<int>{1, 2, 3, 10}));
    EXPECT_FALSE(result.rate_limited);
}

// Edge case: non-matching entries (wrong name shape, or directories) are ignored rather than
// misparsed.
TEST_F(ChapterFetcherTest, ListMasterCandidates_IgnoresNonMatchingEntriesAndDirectories) {
    ContentsResult response;
    response.exists = true;
    response.entries = {
        {"chapter-01.enc", false},
        {"README.md", false},
        {"notachapter.enc", false},
        {"subfolder", true},
        {"chapter-02.enc.bak", false},
    };
    github_client_.set_contents_response("chapters", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.list_master_candidates();

    EXPECT_EQ(result.candidates, (std::vector<int>{1}));
    EXPECT_FALSE(result.rate_limited);
}

// Edge case: a missing chapters/ folder (404) means zero candidates, not an error (spec 004
// Behavior step 5: "no chapters published yet").
TEST_F(ChapterFetcherTest, ListMasterCandidates_MissingFolder_ReturnsEmptyNotError) {
    ContentsResult response;
    response.not_found = true;
    github_client_.set_contents_response("chapters", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.list_master_candidates();

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_FALSE(result.rate_limited);
}

// Edge case: an existing but empty chapters/ folder is also zero candidates.
TEST_F(ChapterFetcherTest, ListMasterCandidates_EmptyFolder_ReturnsEmpty) {
    ContentsResult response;
    response.exists = true;
    github_client_.set_contents_response("chapters", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.list_master_candidates();

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_FALSE(result.rate_limited);
}

// Edge case: a 403 rate-limit response must be distinguishable from a genuinely empty/missing
// chapters/ folder -- both otherwise produce zero candidates, but only one should make the
// caller (main.cpp's run_reader_menu) bail out to the launch prompts with a rate-limit message
// instead of silently showing an empty menu.
TEST_F(ChapterFetcherTest,
       ListMasterCandidates_RateLimited_ReturnsEmptyCandidatesButRateLimitedTrue) {
    ContentsResult response;
    response.network_error = true;
    response.rate_limited = true;
    response.error_message =
        "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to "
        "reset, or connect to a VPN for a new IP address.";
    github_client_.set_contents_response("chapters", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.list_master_candidates();

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_TRUE(result.rate_limited);
}

// --- try_fetch_chapter (spec 004 Behavior step 6) --------------------------------------------

namespace {

std::string make_chapter_json(const std::string& reader_name_or_empty) {
    ChapterFile file;
    file.salt_base64 = "c2FsdA==";
    file.iv_base64 = "aXY=";
    file.ciphertext_base64 = "Y2lwaGVy";
    if (!reader_name_or_empty.empty()) {
        file.wrapped_keys_base64[reader_name_or_empty] = "d3JhcHBlZA==";
    }
    return file.to_json();
}

}  // namespace

TEST_F(ChapterFetcherTest, TryFetchChapter_ReaderKeyPresent_ReturnsReadableWithParsedFile) {
    ContentsResult response;
    response.exists = true;
    response.decoded_content = make_chapter_json("alice");
    github_client_.set_contents_response("chapters/chapter-01.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(1, "alice");

    EXPECT_EQ(result.availability, ChapterAvailability::readable);
    ASSERT_TRUE(result.file.has_value());
    EXPECT_EQ(result.file->wrapped_keys_base64.count("alice"), 1u);
}

TEST_F(ChapterFetcherTest, TryFetchChapter_ReaderKeyAbsent_ReturnsPendingNoFile) {
    ContentsResult response;
    response.exists = true;
    response.decoded_content = make_chapter_json("bob");  // "alice" not wrapped in.
    github_client_.set_contents_response("chapters/chapter-01.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(1, "alice");

    EXPECT_EQ(result.availability, ChapterAvailability::pending);
    EXPECT_FALSE(result.file.has_value());
}

TEST_F(ChapterFetcherTest, TryFetchChapter_NotYetPublished_ReturnsPendingNoFile) {
    ContentsResult response;
    response.not_found = true;
    github_client_.set_contents_response("chapters/chapter-05.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(5, "alice");

    EXPECT_EQ(result.availability, ChapterAvailability::pending);
    EXPECT_FALSE(result.file.has_value());
}

// Edge case: corrupted/malformed chapter JSON must not crash -- treated as pending.
TEST_F(ChapterFetcherTest, TryFetchChapter_MalformedContent_ReturnsPendingNoFile) {
    ContentsResult response;
    response.exists = true;
    response.decoded_content = "{ this is not valid chapter json";
    github_client_.set_contents_response("chapters/chapter-01.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(1, "alice");

    EXPECT_EQ(result.availability, ChapterAvailability::pending);
    EXPECT_FALSE(result.file.has_value());
}

// Edge case: a network error on a single chapter fetch must not crash the whole menu build.
TEST_F(ChapterFetcherTest, TryFetchChapter_NetworkError_ReturnsPendingNoFile) {
    ContentsResult response;
    response.network_error = true;
    response.error_message = "boom";
    github_client_.set_contents_response("chapters/chapter-01.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(1, "alice");

    EXPECT_EQ(result.availability, ChapterAvailability::pending);
    EXPECT_FALSE(result.file.has_value());
}

// Edge case: a 403 rate-limit response must be reported via rate_limited=true and never folded
// into the ordinary "pending" (not-yet-published) case -- the caller needs to tell those two
// situations apart (spec: "a 403 is never folded into pending").
TEST_F(ChapterFetcherTest, TryFetchChapter_RateLimited_SetsRateLimitedTrue_NotReadable) {
    ContentsResult response;
    response.network_error = true;
    response.rate_limited = true;
    response.error_message =
        "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to "
        "reset, or connect to a VPN for a new IP address.";
    github_client_.set_contents_response("chapters/chapter-01.enc", response);
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    const auto result = fetcher.try_fetch_chapter(1, "alice");

    EXPECT_TRUE(result.rate_limited);
    EXPECT_EQ(result.availability, ChapterAvailability::pending);
    EXPECT_FALSE(result.file.has_value());
}

TEST_F(ChapterFetcherTest, TryFetchChapter_RequestsTwoDigitZeroPaddedPath) {
    ChapterFetcher fetcher(github_client_, "writer", "repo");

    fetcher.try_fetch_chapter(3, "alice");
    fetcher.try_fetch_chapter(12, "alice");

    ASSERT_EQ(github_client_.contents_paths_requested().size(), 2u);
    EXPECT_EQ(github_client_.contents_paths_requested()[0], "chapters/chapter-03.enc");
    EXPECT_EQ(github_client_.contents_paths_requested()[1], "chapters/chapter-12.enc");
}

}  // namespace
