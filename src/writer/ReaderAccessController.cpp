#include "writer/ReaderAccessController.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace buddyshare::writer {

namespace {

enum class ChapterListError { none, empty, non_numeric, duplicate };

struct ChapterListParse {
    std::unordered_set<int> chapters;
    ChapterListError error{ChapterListError::none};
};

bool is_all_digits(const std::string& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Turns one chapter-number token into an int, or std::nullopt if it isn't numeric, or it's
// numeric but too large for int (e.g. "99999999999999999999") -- both cases are treated as the
// same "non-numeric" failure by the caller.
std::optional<int> parse_chapter_number(const std::string& token) {
    if (!is_all_digits(token)) return std::nullopt;
    try {
        return std::stoi(token);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// Splits a comma-separated chapter list, rejecting (rather than silently collapsing) a
// duplicate chapter number -- unlike AccessRegistry's lenient load-from-file parsing, direct
// writer input gets a clear re-prompt instead (spec 003 Edge Cases).
ChapterListParse parse_chapter_list(const std::string& text) {
    ChapterListParse result;
    if (text.empty()) {
        result.error = ChapterListError::empty;
        return result;
    }

    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        const std::string token =
            (comma == std::string::npos) ? text.substr(start) : text.substr(start, comma - start);
        const std::optional<int> chapter_number = parse_chapter_number(token);
        if (!chapter_number.has_value()) {
            result.error = ChapterListError::non_numeric;
            return result;
        }
        if (!result.chapters.insert(*chapter_number).second) {
            result.error = ChapterListError::duplicate;
            return result;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

// Prompts until a valid chapter list is entered: non-empty, all-numeric, duplicate-free, and
// (when exactly_one is set, for Novice) exactly one chapter number.
std::unordered_set<int> prompt_for_chapters(shared::IConsole& console, bool exactly_one) {
    const std::string message =
        exactly_one ? "Chapter number for this Novice reader (exactly one, e.g. 1):"
                    : "Chapter numbers for this Apprentice reader, comma-separated (e.g. 2,3):";

    for (;;) {
        const ChapterListParse parsed = parse_chapter_list(console.prompt(message));
        switch (parsed.error) {
            case ChapterListError::empty:
                console.print("At least one chapter number is required.",
                              shared::MessageStyle::warning);
                continue;
            case ChapterListError::non_numeric:
                console.print("Chapter numbers must be numeric.", shared::MessageStyle::warning);
                continue;
            case ChapterListError::duplicate:
                console.print(
                    "Duplicate chapter number entered; each chapter may be listed only once.",
                    shared::MessageStyle::warning);
                continue;
            case ChapterListError::none:
                break;
        }
        if (exactly_one && parsed.chapters.size() != 1) {
            console.print("Novice access requires exactly one chapter number.",
                          shared::MessageStyle::warning);
            continue;
        }
        return parsed.chapters;
    }
}

}  // namespace

ReaderAccessController::ReaderAccessController(shared::AccessRegistry& registry,
                                                 shared::IConsole& console,
                                                 IGitProcess& git_process)
    : registry_(registry), console_(console), git_process_(git_process) {}

RegistrationDetails ReaderAccessController::collect_registration_details() {
    return prompt_for_details();
}

bool ReaderAccessController::edit_existing_reader(const std::string& name) {
    if (!registry_.find_by_name(name).has_value()) return false;

    const RegistrationDetails details = prompt_for_details();
    if (!registry_.update_reader(name, details.level, details.chapters)) return false;

    git_process_.run({"add", "access.txt"});
    git_process_.run(
        {"commit", "-m", "chore: update access for " + name + " to " +
                              shared::to_string(details.level)});
    const GitResult push_result = git_process_.run({"push"});
    if (push_result.exit_code != 0) {
        console_.print("Access for \"" + name +
                            "\" updated locally, but the push failed; run `git push` manually so "
                            "readers can see the update.",
                        shared::MessageStyle::warning);
    }
    return true;
}

bool ReaderAccessController::is_authorized_for_chapter(const shared::ReaderEntry& entry,
                                                         int chapter_number) {
    if (entry.level == shared::AccessLevel::master) return true;
    return entry.chapters.count(chapter_number) > 0;
}

RegistrationDetails ReaderAccessController::prompt_for_details() {
    RegistrationDetails details;

    for (;;) {
        const std::optional<shared::AccessLevel> level = shared::parse_access_level(console_.prompt(
            "Access level -- Master sees every chapter, Apprentice sees a chosen list, Novice "
            "sees exactly one. Enter Master, Apprentice, or Novice:"));
        if (level.has_value()) {
            details.level = *level;
            break;
        }
        console_.print("Unrecognized access level. Enter Master, Apprentice, or Novice.",
                       shared::MessageStyle::warning);
    }

    switch (details.level) {
        case shared::AccessLevel::master:
            break;
        case shared::AccessLevel::apprentice:
            details.chapters = prompt_for_chapters(console_, /*exactly_one=*/false);
            break;
        case shared::AccessLevel::novice:
            details.chapters = prompt_for_chapters(console_, /*exactly_one=*/true);
            break;
    }

    return details;
}

}  // namespace buddyshare::writer
