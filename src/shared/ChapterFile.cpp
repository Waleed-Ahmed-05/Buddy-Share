#include "shared/ChapterFile.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace buddyshare::shared {

namespace {

// This project hand-rolls its (small, fixed-shape) JSON rather than pulling in a library --
// the same approach HttpGitHubClient.cpp already takes for parsing GitHub's response.

std::string escape_json_string(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

void skip_whitespace(const std::string& text, std::size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
}

// Parses a JSON string starting at text[pos] == '"'. On success, advances pos past the closing
// quote and returns the unescaped value. Returns std::nullopt for any malformed input
// (unterminated string, invalid escape, etc.) -- from_json() never returns a
// partially-populated ChapterFile on such input.
std::optional<std::string> parse_json_string(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') return std::nullopt;
    ++pos;

    std::string result;
    while (pos < text.size() && text[pos] != '"') {
        const char c = text[pos];
        if (c != '\\') {
            result.push_back(c);
            ++pos;
            continue;
        }

        ++pos;
        if (pos >= text.size()) return std::nullopt;
        switch (text[pos]) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'u':
                // \uXXXX: this format never produces one (base64 text and reader names never
                // need it), so the four hex digits are skipped rather than decoded.
                if (pos + 4 >= text.size()) return std::nullopt;
                pos += 4;
                break;
            default:
                return std::nullopt;
        }
        ++pos;
    }
    if (pos >= text.size()) return std::nullopt;  // unterminated string.
    ++pos;                                        // closing quote.
    return result;
}

// Parses a flat JSON object of string -> string (the shape "wrappedKeys" is always in).
// Returns std::nullopt for anything else, including a missing/malformed opening brace.
std::optional<std::unordered_map<std::string, std::string>> parse_string_map(
    const std::string& text, std::size_t& pos) {
    skip_whitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{') return std::nullopt;
    ++pos;

    std::unordered_map<std::string, std::string> result;
    skip_whitespace(text, pos);
    if (pos < text.size() && text[pos] == '}') {
        ++pos;
        return result;
    }

    for (;;) {
        skip_whitespace(text, pos);
        const auto key = parse_json_string(text, pos);
        if (!key.has_value()) return std::nullopt;

        skip_whitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':') return std::nullopt;
        ++pos;

        skip_whitespace(text, pos);
        const auto value = parse_json_string(text, pos);
        if (!value.has_value()) return std::nullopt;
        result[*key] = *value;

        skip_whitespace(text, pos);
        if (pos >= text.size()) return std::nullopt;
        if (text[pos] == ',') {
            ++pos;
            continue;
        }
        if (text[pos] == '}') {
            ++pos;
            break;
        }
        return std::nullopt;
    }
    return result;
}

}  // namespace

bool ChapterFile::operator==(const ChapterFile& other) const {
    return salt_base64 == other.salt_base64 && iv_base64 == other.iv_base64 &&
           ciphertext_base64 == other.ciphertext_base64 &&
           wrapped_keys_base64 == other.wrapped_keys_base64;
}

std::string ChapterFile::to_json() const {
    std::string json = "{";
    json += "\"salt\":\"" + escape_json_string(salt_base64) + "\",";
    json += "\"iv\":\"" + escape_json_string(iv_base64) + "\",";
    json += "\"ciphertext\":\"" + escape_json_string(ciphertext_base64) + "\",";
    json += "\"wrappedKeys\":{";
    bool first = true;
    for (const auto& [reader_name, wrapped_key] : wrapped_keys_base64) {
        if (!first) json += ",";
        first = false;
        json += "\"" + escape_json_string(reader_name) + "\":\"" + escape_json_string(wrapped_key) +
                 "\"";
    }
    json += "}}";
    return json;
}

std::optional<ChapterFile> ChapterFile::from_json(const std::string& json_text) {
    std::size_t pos = 0;
    skip_whitespace(json_text, pos);
    if (pos >= json_text.size() || json_text[pos] != '{') return std::nullopt;
    ++pos;

    std::optional<std::string> salt;
    std::optional<std::string> iv;
    std::optional<std::string> ciphertext;
    std::optional<std::unordered_map<std::string, std::string>> wrapped_keys;

    skip_whitespace(json_text, pos);
    if (pos < json_text.size() && json_text[pos] == '}') {
        ++pos;  // empty object: all four required fields are absent, caught below.
    } else {
        for (;;) {
            skip_whitespace(json_text, pos);
            const auto key = parse_json_string(json_text, pos);
            if (!key.has_value()) return std::nullopt;

            skip_whitespace(json_text, pos);
            if (pos >= json_text.size() || json_text[pos] != ':') return std::nullopt;
            ++pos;
            skip_whitespace(json_text, pos);

            if (*key == "wrappedKeys") {
                wrapped_keys = parse_string_map(json_text, pos);
                if (!wrapped_keys.has_value()) return std::nullopt;
            } else {
                const auto value = parse_json_string(json_text, pos);
                if (!value.has_value()) return std::nullopt;
                if (*key == "salt") {
                    salt = value;
                } else if (*key == "iv") {
                    iv = value;
                } else if (*key == "ciphertext") {
                    ciphertext = value;
                }
                // Any other field name is ignored rather than rejected.
            }

            skip_whitespace(json_text, pos);
            if (pos >= json_text.size()) return std::nullopt;
            if (json_text[pos] == ',') {
                ++pos;
                continue;
            }
            if (json_text[pos] == '}') {
                ++pos;
                break;
            }
            return std::nullopt;
        }
    }

    if (!salt.has_value() || !iv.has_value() || !ciphertext.has_value() ||
        !wrapped_keys.has_value()) {
        return std::nullopt;
    }

    ChapterFile file;
    file.salt_base64 = std::move(*salt);
    file.iv_base64 = std::move(*iv);
    file.ciphertext_base64 = std::move(*ciphertext);
    file.wrapped_keys_base64 = std::move(*wrapped_keys);
    return file;
}

std::string ChapterFile::file_path_for_chapter(const std::string& repo_root, int chapter_number) {
    std::ostringstream file_name;
    file_name << "chapter-" << std::setfill('0') << std::setw(2) << chapter_number << ".enc";
    const std::filesystem::path path =
        std::filesystem::path(repo_root) / "chapters" / file_name.str();
    return path.string();
}

}  // namespace buddyshare::shared
