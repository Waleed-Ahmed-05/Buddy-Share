# Test target for spec 001 (RoleManager + GitBootstrapService + WriterMenu), spec 002
# (CryptoProvider + ChapterFile + ChapterEncryptionService), and spec 004 (ChapterFetcher +
# ReaderViewerService + ReaderMenu).

# Spec 002's CryptoProvider is OpenSSL-backed (PBKDF2/AES-256-GCM/RSA-OAEP).
find_package(OpenSSL REQUIRED)

include(FetchContent)
set(GTEST_VERSION v1.17.0)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/${GTEST_VERSION}.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(buddyshare_tests
  unit/RoleManagerTest.cpp
  unit/GitBootstrapServiceTest.cpp
  unit/WriterMenuTest.cpp
  unit/IndependentEdgeCaseTest.cpp
  unit/AccessRegistryTest.cpp
  unit/ReaderAccessControllerTest.cpp
  unit/CryptoProviderTest.cpp
  unit/ChapterFileTest.cpp
  unit/ChapterEncryptionServiceTest.cpp
  unit/ChapterFetcherTest.cpp
  unit/ReaderViewerServiceTest.cpp
  unit/ReaderMenuTest.cpp
  unit/ConsoleStyleTest.cpp
  ${CMAKE_SOURCE_DIR}/src/core/RoleManager.cpp
  ${CMAKE_SOURCE_DIR}/src/writer/GitBootstrapService.cpp
  ${CMAKE_SOURCE_DIR}/src/writer/WriterMenu.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/AccessRegistry.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/ConsoleStyle.cpp
  ${CMAKE_SOURCE_DIR}/src/writer/ReaderAccessController.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/CryptoProvider.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/ChapterFile.cpp
  ${CMAKE_SOURCE_DIR}/src/writer/ChapterEncryptionService.cpp
  ${CMAKE_SOURCE_DIR}/src/reader/ChapterFetcher.cpp
  ${CMAKE_SOURCE_DIR}/src/reader/ReaderViewerService.cpp
  ${CMAKE_SOURCE_DIR}/src/reader/ReaderMenu.cpp
)

target_include_directories(buddyshare_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(buddyshare_tests PRIVATE GTest::gtest GTest::gmock GTest::gtest_main
                                                 OpenSSL::Crypto)

if(MINGW)
  # Match the main target: static runtime so the test binary doesn't depend on
  # MinGW DLLs being resolvable on PATH when ctest spawns it.
  target_link_options(buddyshare_tests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

include(GoogleTest)
gtest_discover_tests(buddyshare_tests)
