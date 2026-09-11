#include "reader/DpapiPrivateKeyStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wincrypt.h>

namespace buddyshare::reader {

namespace {

namespace fs = std::filesystem;

// Consolidated under the same %APPDATA%\BuddyShare\ app folder as spec 001's .role lock file
// (see specs/004-reader-viewer.md, Data Model).
fs::path keys_directory() {
    const char* appdata = std::getenv("APPDATA");
    const fs::path base = appdata ? fs::path(appdata) : fs::temp_directory_path();
    return base / "BuddyShare" / "keys";
}

fs::path key_file_path(const std::string& username) {
    return keys_directory() / (username + "_private.pem");
}

}  // namespace

std::optional<std::string> DpapiPrivateKeyStore::read_private_key_pem(
    const std::string& username) const {
    std::ifstream in(key_file_path(username), std::ios::binary);
    if (!in.is_open()) return std::nullopt;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string encrypted = buffer.str();
    if (encrypted.empty()) return std::nullopt;

    DATA_BLOB input_blob;
    input_blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.data()));
    input_blob.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB output_blob{};

    if (!CryptUnprotectData(&input_blob, nullptr, nullptr, nullptr, nullptr, 0, &output_blob)) {
        return std::nullopt;  // e.g. a different Windows user account -- never crash on this.
    }
    std::string decrypted(reinterpret_cast<char*>(output_blob.pbData), output_blob.cbData);
    LocalFree(output_blob.pbData);
    return decrypted;
}

void DpapiPrivateKeyStore::write_private_key_pem(const std::string& username,
                                                   const std::string& private_key_pem) {
    DATA_BLOB input_blob;
    input_blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(private_key_pem.data()));
    input_blob.cbData = static_cast<DWORD>(private_key_pem.size());
    DATA_BLOB output_blob{};

    if (!CryptProtectData(&input_blob, L"BuddyShare reader private key", nullptr, nullptr, nullptr,
                            0, &output_blob)) {
        throw std::runtime_error("Failed to protect the private key via DPAPI.");
    }

    const fs::path path = key_file_path(username);
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    out.write(reinterpret_cast<char*>(output_blob.pbData),
              static_cast<std::streamsize>(output_blob.cbData));
    LocalFree(output_blob.pbData);
}

}  // namespace buddyshare::reader
