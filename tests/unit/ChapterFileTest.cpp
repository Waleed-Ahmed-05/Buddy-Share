#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <unordered_map>

#include "shared/ChapterFile.h"

using buddyshare::shared::ChapterFile;

namespace {

ChapterFile make_sample() {
    ChapterFile file;
    file.salt_base64 = "c2FsdHNhbHQ=";
    file.iv_base64 = "aXZpdml2";
    file.ciphertext_base64 = "Y2lwaGVydGV4dGNpcGhlcnRleHQ=";
    file.wrapped_keys_base64 = {{"alice", "d3JhcHBlZC1hbGljZQ=="}, {"bob", "d3JhcHBlZC1ib2I="}};
    return file;
}

// --- to_json / from_json round trip ----------------------------------------------------------

TEST(ChapterFileTest, ToJsonThenFromJson_RoundTripsAllFields) {
    const ChapterFile original = make_sample();

    const std::string json = original.to_json();
    const std::optional<ChapterFile> parsed = ChapterFile::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, original);
}

TEST(ChapterFileTest, ToJson_ContainsExpectedKeysAndBase64Values) {
    const ChapterFile file = make_sample();

    const std::string json = file.to_json();

    EXPECT_NE(json.find("salt"), std::string::npos);
    EXPECT_NE(json.find("iv"), std::string::npos);
    EXPECT_NE(json.find("ciphertext"), std::string::npos);
    EXPECT_NE(json.find("wrappedKeys"), std::string::npos);
    EXPECT_NE(json.find(file.salt_base64), std::string::npos);
    EXPECT_NE(json.find(file.ciphertext_base64), std::string::npos);
    EXPECT_NE(json.find("alice"), std::string::npos);
    EXPECT_NE(json.find("bob"), std::string::npos);
}

// Edge case: a zero-reader chapter (writer proceeded despite an empty access.txt) is valid
// output, not an error -- wrappedKeys serializes as an empty object, not omitted.
TEST(ChapterFileTest, ToJsonThenFromJson_EmptyWrappedKeysMap_RoundTrips) {
    ChapterFile file = make_sample();
    file.wrapped_keys_base64.clear();

    const std::optional<ChapterFile> parsed = ChapterFile::from_json(file.to_json());

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->wrapped_keys_base64.empty());
}

// --- from_json: malformed input ---------------------------------------------------------------

TEST(ChapterFileTest, FromJson_NotJson_ReturnsNullopt) {
    EXPECT_FALSE(ChapterFile::from_json("this is not json at all").has_value());
}

TEST(ChapterFileTest, FromJson_EmptyString_ReturnsNullopt) {
    EXPECT_FALSE(ChapterFile::from_json("").has_value());
}

TEST(ChapterFileTest, FromJson_MissingCiphertextField_ReturnsNullopt) {
    const std::string json = R"({"salt":"c2FsdA==","iv":"aXY=","wrappedKeys":{}})";

    EXPECT_FALSE(ChapterFile::from_json(json).has_value());
}

TEST(ChapterFileTest, FromJson_MissingWrappedKeysField_ReturnsNullopt) {
    const std::string json = R"({"salt":"c2FsdA==","iv":"aXY=","ciphertext":"Y2lwaGVy"})";

    EXPECT_FALSE(ChapterFile::from_json(json).has_value());
}

// --- file_path_for_chapter ---------------------------------------------------------------------

TEST(ChapterFileTest, FilePathForChapter_SingleDigitChapter_ZeroPadsToTwoDigits) {
    const std::string path = ChapterFile::file_path_for_chapter("/repo", 1);

    EXPECT_NE(path.find("chapter-01.enc"), std::string::npos);
}

TEST(ChapterFileTest, FilePathForChapter_TwoDigitChapter_UsesExactNumber) {
    const std::string path = ChapterFile::file_path_for_chapter("/repo", 12);

    EXPECT_NE(path.find("chapter-12.enc"), std::string::npos);
}

TEST(ChapterFileTest, FilePathForChapter_LivesUnderChaptersSubfolderOfRepoRoot) {
    const std::string path = ChapterFile::file_path_for_chapter("/repo", 3);

    EXPECT_NE(path.find("chapters"), std::string::npos);
    EXPECT_NE(path.find("/repo"), std::string::npos);
}

}  // namespace
