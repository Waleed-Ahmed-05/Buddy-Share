#include <gtest/gtest.h>

#include <vector>

#include "reader/ReaderMenu.h"

using buddyshare::reader::ChapterAvailability;
using buddyshare::reader::ChapterMenuEntry;
using buddyshare::reader::ReaderMenu;

namespace {

// --- entries ----------------------------------------------------------------------------------

TEST(ReaderMenuTest, Entries_ReturnsExactlyWhatWasConstructedWith_InOrder) {
    const std::vector<ChapterMenuEntry> built = {
        {1, ChapterAvailability::readable},
        {2, ChapterAvailability::pending},
        {3, ChapterAvailability::readable},
    };
    ReaderMenu menu(built);

    EXPECT_EQ(menu.entries(), built);
}

// Edge case: zero chapters published/authorized yet -- an empty menu, not an error.
TEST(ReaderMenuTest, Entries_EmptyInput_ReturnsEmptyMenu) {
    ReaderMenu menu({});

    EXPECT_TRUE(menu.entries().empty());
    EXPECT_TRUE(menu.selectable_chapter_numbers().empty());
}

// --- selectable_chapter_numbers -----------------------------------------------------------
// Acceptance Criterion 4: exactly the readable chapters are selectable; pending ones are shown
// but not selectable, never silently omitted from entries().

TEST(ReaderMenuTest, SelectableChapterNumbers_ReturnsOnlyReadableOnes) {
    ReaderMenu menu({
        {1, ChapterAvailability::readable},
        {2, ChapterAvailability::pending},
        {3, ChapterAvailability::readable},
        {4, ChapterAvailability::pending},
    });

    EXPECT_EQ(menu.selectable_chapter_numbers(), (std::vector<int>{1, 3}));
}

TEST(ReaderMenuTest, SelectableChapterNumbers_AllPending_ReturnsEmptyButEntriesStillPresent) {
    ReaderMenu menu({
        {1, ChapterAvailability::pending},
        {2, ChapterAvailability::pending},
    });

    EXPECT_TRUE(menu.selectable_chapter_numbers().empty());
    EXPECT_EQ(menu.entries().size(), 2u)
        << "pending chapters must still appear in the menu, not be hidden";
}

// --- is_selectable --------------------------------------------------------------------------

TEST(ReaderMenuTest, IsSelectable_ReadableChapter_ReturnsTrue) {
    ReaderMenu menu({{5, ChapterAvailability::readable}});

    EXPECT_TRUE(menu.is_selectable(5));
}

TEST(ReaderMenuTest, IsSelectable_PendingChapter_ReturnsFalse) {
    ReaderMenu menu({{5, ChapterAvailability::pending}});

    EXPECT_FALSE(menu.is_selectable(5));
}

// Edge case: a chapter number not present in the menu at all (never fetched/candidate) is
// simply not selectable, not an out-of-bounds crash.
TEST(ReaderMenuTest, IsSelectable_UnknownChapterNumber_ReturnsFalse) {
    ReaderMenu menu({{5, ChapterAvailability::readable}});

    EXPECT_FALSE(menu.is_selectable(999));
}

}  // namespace
