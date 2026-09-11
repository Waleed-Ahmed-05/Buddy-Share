#include "core/RoleStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace buddyshare::core {

namespace {

namespace fs = std::filesystem;

fs::path role_file_path() {
    const char* appdata = std::getenv("APPDATA");
    const fs::path base = appdata ? fs::path(appdata) : fs::temp_directory_path();
    return base / "BuddyShare" / ".role";
}

// Trims surrounding whitespace so a stray trailing newline in the file doesn't make an
// otherwise-valid "writer"/"reader" contents fail to parse.
std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

RoleStore::RoleStore() : path_(role_file_path().string()) {}

std::optional<std::string> RoleStore::read() const {
    std::ifstream in(path_);
    if (!in) return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return trim(buffer.str());
}

void RoleStore::write(const std::string& role_text) {
    const fs::path path(path_);
    fs::create_directories(path.parent_path());

    // The hidden attribute (set below) prevents overwriting via a normal ofstream, so clear
    // it first when the file already exists (e.g. rewriting a corrupted role file).
    if (fs::exists(path)) {
        SetFileAttributesW(path.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
    }

    {
        std::ofstream out(path_, std::ios::trunc);
        out << role_text;
    }

    SetFileAttributesW(path.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
}

}  // namespace buddyshare::core
