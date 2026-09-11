#ifndef BUDDYSHARE_SHARED_ACCESSREGISTRY_H
#define BUDDYSHARE_SHARED_ACCESSREGISTRY_H

#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "shared/Console.h"

namespace buddyshare::shared {

// Spec 003's three reader access levels.
enum class AccessLevel { master, apprentice, novice };

// One line of access.txt: <name>:<base64-public-key>:<level>:<chapters>.
// chapters is empty for Master, exactly one entry for Novice, and one-or-more for Apprentice.
struct ReaderEntry {
    std::string name;
    std::string public_key_base64;
    AccessLevel level{AccessLevel::master};
    std::unordered_set<int> chapters;
};

// Level <-> access.txt field-3 string conversions, shared by AccessRegistry (file format) and
// ReaderAccessController (prompt validation).
std::optional<AccessLevel> parse_access_level(const std::string& text);
std::string to_string(AccessLevel level);

// The single parser/writer for access.txt (repo root), spec 003's 4-field format. Backed by an
// std::unordered_map<std::string, ReaderEntry> keyed by name for O(1) lookup, per spec 002's
// Architecture Context.
class AccessRegistry {
public:
    // file_path: the access.txt path on disk. Warnings about malformed lines (skipped rather
    // than aborting the whole load) are printed via console.
    AccessRegistry(std::string file_path, IConsole& console);

    // Reads file_path from disk, replacing any previously loaded state. A missing file is
    // treated as zero readers, not an error. Each malformed line (wrong field count, unknown
    // level string, non-numeric chapter number, empty chapter list for Apprentice, anything
    // other than exactly one chapter for Novice, or a non-empty chapter list for Master) is
    // skipped with a console warning naming the offending line; the rest of the file still
    // loads.
    void load_from_file();

    // Same parsing/warning rules as load_from_file(), but from already-fetched text instead of
    // file_path_ -- spec 004's reader role fetches access.txt over the network (via
    // ChapterFetcher) rather than reading a local file, and reuses this same parser rather than
    // duplicating its rules. Replaces any previously loaded state, exactly like load_from_file().
    void load_from_text(const std::string& text);

    // O(1) lookup by name. Returns std::nullopt if no such reader is currently loaded.
    std::optional<ReaderEntry> find_by_name(const std::string& name) const;

    // All currently loaded readers, in file order.
    std::vector<ReaderEntry> all_readers() const;

    // Appends a new 4-field line for entry and persists it to file_path immediately. Refuses
    // (returns false, prints a console warning) and leaves the file untouched if entry.name is
    // already registered -- never silently overwrites an existing reader.
    bool append_reader(const ReaderEntry& entry);

    // Rewrites the existing line for `name` in place with the given level/chapters; the name
    // and public key already on file are preserved untouched. Returns false (no line created,
    // no file change) if `name` isn't already registered -- editing is not an implicit
    // registration.
    bool update_reader(const std::string& name, AccessLevel level,
                        const std::unordered_set<int>& chapters);

private:
    std::string file_path_;
    IConsole& console_;
    std::unordered_map<std::string, ReaderEntry> readers_;
    std::vector<std::string> order_;

    // The parsing loop shared by load_from_file() and load_from_text().
    void load_from_stream(std::istream& in);

    void persist() const;
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_ACCESSREGISTRY_H
