cmake_minimum_required(VERSION 3.20)
project(BuddyShare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Spec 002's CryptoProvider is OpenSSL-backed (PBKDF2/AES-256-GCM/RSA-OAEP).
find_package(OpenSSL REQUIRED)

add_executable(BuddyShare
  src/main.cpp
  src/core/RoleManager.cpp
  src/core/RoleStore.cpp
  src/shared/AccessRegistry.cpp
  src/shared/ChapterFile.cpp
  src/shared/ConsoleIO.cpp
  src/shared/ConsoleStyle.cpp
  src/shared/CryptoProvider.cpp
  src/shared/HttpGitHubClient.cpp
  src/reader/ChapterFetcher.cpp
  src/reader/DpapiPrivateKeyStore.cpp
  src/reader/ReaderMenu.cpp
  src/reader/ReaderViewerService.cpp
  src/writer/ChapterEncryptionService.cpp
  src/writer/GitBootstrapService.cpp
  src/writer/ReaderAccessController.cpp
  src/writer/SystemGitProcess.cpp
  src/writer/WriterMenu.cpp
)

target_include_directories(BuddyShare PRIVATE src)
target_link_libraries(BuddyShare PRIVATE OpenSSL::Crypto)

if(WIN32)
  # winhttp: the shared GitHubClient's real HTTP implementation. crypt32: spec 004's
  # DpapiPrivateKeyStore (CryptProtectData/CryptUnprotectData).
  target_link_libraries(BuddyShare PRIVATE winhttp crypt32)
endif()

if(MINGW)
  # Statically link the MinGW runtime so the shipped BuddyShare.exe runs on
  # machines without MSYS2/MinGW DLLs installed (writers/readers won't have them).
  target_link_options(BuddyShare PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

enable_testing()
add_subdirectory(tests)
