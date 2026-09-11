#ifndef BUDDYSHARE_READER_READERMENU_H
#define BUDDYSHARE_READER_READERMENU_H

#include <vector>

#include "reader/ChapterFetcher.h"

namespace buddyshare::reader {

// One row of the reader's chapter menu (spec 004 Behavior step 7): a chapter number plus
// whether it's actually readable right now. Pending chapters are shown, not hidden, but can't
// be selected (spec 004 Acceptance Criterion 4).
struct ChapterMenuEntry {
    int chapter_number{0};
    ChapterAvailability availability{ChapterAvailability::pending};

    bool operator==(const ChapterMenuEntry& other) const {
        return chapter_number == other.chapter_number && availability == other.availability;
    }
};

// The Reader role's menu display logic (spec 004 Behavior step 7) -- mirrors WriterMenu's role:
// a pure computation over already-fetched data, no I/O of its own.
class ReaderMenu {
public:
    explicit ReaderMenu(std::vector<ChapterMenuEntry> entries);

    // All entries, in the order given at construction (ChapterFetcher returns Master
    // candidates pre-sorted; callers are expected to pass entries in the desired display
    // order).
    const std::vector<ChapterMenuEntry>& entries() const;

    // Only the chapter numbers whose availability is readable -- what the reader may actually
    // select. Pending chapters remain visible via entries() but are never selectable.
    std::vector<int> selectable_chapter_numbers() const;

    // True iff chapter_number appears in entries() with availability == readable.
    bool is_selectable(int chapter_number) const;

private:
    std::vector<ChapterMenuEntry> entries_;
};

}  // namespace buddyshare::reader

#endif  // BUDDYSHARE_READER_READERMENU_H
