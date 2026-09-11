#ifndef BUDDYSHARE_SHARED_CHAPTERFILE_H
#define BUDDYSHARE_SHARED_CHAPTERFILE_H

#include <optional>
#include <string>
#include <unordered_map>

namespace buddyshare::shared {

// The one definition of the `.enc` JSON envelope, written by spec 002's
// ChapterEncryptionService and read by spec 004's reader-side decryption (see
// specs/002-chapter-encryption.md, Data Model / API / CLI Surface). Every byte field
// (salt/iv/ciphertext/each wrapped key) is base64 text in the JSON -- ChapterFile only ever
// carries already-base64-encoded strings; CryptoProvider is what produces/consumes the raw
// bytes those strings encode.
struct ChapterFile {
    std::string salt_base64;
    std::string iv_base64;
    std::string ciphertext_base64;
    std::unordered_map<std::string, std::string> wrapped_keys_base64;  // reader name -> wrapped key.

    bool operator==(const ChapterFile& other) const;

    // Serializes to the JSON text written to chapters/chapter-<NN>.enc. An empty
    // wrapped_keys_base64 map serializes to an empty JSON object ("{}"), not an omitted key --
    // a zero-reader chapter is valid output (spec 002 Edge Cases: writer may proceed with an
    // empty access.txt after being explicitly warned).
    std::string to_json() const;

    // Parses JSON text (as read from a .enc file) back into a ChapterFile. Returns
    // std::nullopt -- never a partially-populated object -- for malformed JSON or JSON
    // missing any of the four required keys.
    static std::optional<ChapterFile> from_json(const std::string& json_text);

    // Builds the chapters/chapter-<NN>.enc path (2-digit zero-padded chapter number, e.g.
    // chapter-01.enc, chapter-12.enc) under repo_root -- the exact naming convention spec
    // 004's reader relies on.
    static std::string file_path_for_chapter(const std::string& repo_root, int chapter_number);
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_CHAPTERFILE_H
