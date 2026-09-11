#include "reader/ReaderMenu.h"

#include <algorithm>
#include <utility>

namespace buddyshare::reader {

ReaderMenu::ReaderMenu(std::vector<ChapterMenuEntry> entries) : entries_(std::move(entries)) {}

const std::vector<ChapterMenuEntry>& ReaderMenu::entries() const { return entries_; }

std::vector<int> ReaderMenu::selectable_chapter_numbers() const {
    std::vector<int> numbers;
    for (const auto& entry : entries_) {
        if (entry.availability == ChapterAvailability::readable) {
            numbers.push_back(entry.chapter_number);
        }
    }
    return numbers;
}

bool ReaderMenu::is_selectable(int chapter_number) const {
    return std::any_of(entries_.begin(), entries_.end(), [chapter_number](const ChapterMenuEntry& entry) {
        return entry.chapter_number == chapter_number && entry.availability == ChapterAvailability::readable;
    });
}

}  // namespace buddyshare::reader
