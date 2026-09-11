#include "shared/AccessRegistry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace buddyshare::shared {

namespace {

// Splits text on delimiter, keeping empty fields (so a trailing "..." field, e.g. Master's
// empty chapter list, still produces its own field rather than being dropped).
std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = text.find(delimiter, start);
        if (pos == std::string::npos) {
            fields.push_back(text.substr(start));
            break;
        }
        fields.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

bool is_all_digits(const std::string& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Turns one chapter-number token into an int, or std::nullopt if it isn't numeric, or it's
// numeric but too large for int (e.g. "99999999999999999999") -- both cases are treated as the
// same "not a valid chapter number" failure.
std::optional<int> parse_chapter_number(const std::string& token) {
    if (!is_all_digits(token)) return std::nullopt;
    try {
        return std::stoi(token);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// Parses access.txt's chapters field leniently: non-numeric text is rejected, but a duplicate
// chapter number within the same line simply collapses via the unordered_set (spec 003's
// documented edge case), it isn't a load error.
std::optional<std::unordered_set<int>> parse_chapters_field(const std::string& field) {
    std::unordered_set<int> chapters;
    if (field.empty()) return chapters;
    for (const std::string& token : split(field, ',')) {
        const std::optional<int> chapter_number = parse_chapter_number(token);
        if (!chapter_number.has_value()) return std::nullopt;
        chapters.insert(*chapter_number);
    }
    return chapters;
}

// Master must carry no chapters (implicitly all of them); Apprentice must carry at least one;
// Novice must carry exactly one.
bool chapters_match_level(AccessLevel level, const std::unordered_set<int>& chapters) {
    switch (level) {
        case AccessLevel::master:
            return chapters.empty();
        case AccessLevel::apprentice:
            return !chapters.empty();
        case AccessLevel::novice:
            return chapters.size() == 1;
    }
    return false;
}

}  // namespace

std::optional<AccessLevel> parse_access_level(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char c : text) lower.push_back(static_cast<char>(std::tolower(c)));

    if (lower == "master") return AccessLevel::master;
    if (lower == "apprentice") return AccessLevel::apprentice;
    if (lower == "novice") return AccessLevel::novice;
    return std::nullopt;
}

std::string to_string(AccessLevel level) {
    switch (level) {
        case AccessLevel::master:
            return "Master";
        case AccessLevel::apprentice:
            return "Apprentice";
        case AccessLevel::novice:
            return "Novice";
    }
    return "Master";
}

AccessRegistry::AccessRegistry(std::string file_path, IConsole& console)
    : file_path_(std::move(file_path)), console_(console) {}

void AccessRegistry::load_from_file() {
    std::ifstream in(file_path_);
    if (!in.is_open()) {
        readers_.clear();
        order_.clear();
        return;  // missing file: zero readers, not an error.
    }
    load_from_stream(in);
}

void AccessRegistry::load_from_text(const std::string& text) {
    std::istringstream in(text);
    load_from_stream(in);
}

void AccessRegistry::load_from_stream(std::istream& in) {
    readers_.clear();
    order_.clear();

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Names why this line is being skipped, always with the same "Skipping..." prefix, so
        // the four checks below don't each repeat that wording.
        const auto skip_line = [&](const std::string& reason) {
            console_.print("Skipping malformed access.txt line (" + reason + "): " + line,
                            MessageStyle::warning);
        };

        const std::vector<std::string> fields = split(line, ':');
        if (fields.size() != 4) {
            skip_line("expected 4 fields");
            continue;
        }

        const std::optional<AccessLevel> level = parse_access_level(fields[2]);
        if (!level.has_value()) {
            skip_line("unknown access level");
            continue;
        }

        const std::optional<std::unordered_set<int>> chapters = parse_chapters_field(fields[3]);
        if (!chapters.has_value()) {
            skip_line("non-numeric chapter number");
            continue;
        }

        if (!chapters_match_level(*level, *chapters)) {
            skip_line("chapter list doesn't match " + to_string(*level));
            continue;
        }

        ReaderEntry entry;
        entry.name = fields[0];
        entry.public_key_base64 = fields[1];
        entry.level = *level;
        entry.chapters = *chapters;

        if (readers_.find(entry.name) == readers_.end()) order_.push_back(entry.name);
        readers_[entry.name] = std::move(entry);
    }
}

std::optional<ReaderEntry> AccessRegistry::find_by_name(const std::string& name) const {
    const auto it = readers_.find(name);
    if (it == readers_.end()) return std::nullopt;
    return it->second;
}

std::vector<ReaderEntry> AccessRegistry::all_readers() const {
    std::vector<ReaderEntry> result;
    result.reserve(order_.size());
    for (const std::string& name : order_) {
        const auto it = readers_.find(name);
        if (it != readers_.end()) result.push_back(it->second);
    }
    return result;
}

bool AccessRegistry::append_reader(const ReaderEntry& entry) {
    if (readers_.find(entry.name) != readers_.end()) {
        console_.print("Reader \"" + entry.name + "\" is already registered; not overwriting.",
                        MessageStyle::warning);
        return false;
    }

    order_.push_back(entry.name);
    readers_[entry.name] = entry;
    persist();
    return true;
}

bool AccessRegistry::update_reader(const std::string& name, AccessLevel level,
                                    const std::unordered_set<int>& chapters) {
    const auto it = readers_.find(name);
    if (it == readers_.end()) return false;

    it->second.level = level;
    it->second.chapters = chapters;
    persist();
    return true;
}

void AccessRegistry::persist() const {
    std::ofstream out(file_path_, std::ios::trunc);
    for (const std::string& name : order_) {
        const auto it = readers_.find(name);
        if (it == readers_.end()) continue;
        const ReaderEntry& entry = it->second;

        std::vector<int> sorted_chapters(entry.chapters.begin(), entry.chapters.end());
        std::sort(sorted_chapters.begin(), sorted_chapters.end());

        out << entry.name << ':' << entry.public_key_base64 << ':' << to_string(entry.level)
            << ':';
        for (std::size_t i = 0; i < sorted_chapters.size(); ++i) {
            if (i != 0) out << ',';
            out << sorted_chapters[i];
        }
        out << '\n';
    }
}

}  // namespace buddyshare::shared
