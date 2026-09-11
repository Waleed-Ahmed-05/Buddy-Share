#include "writer/ChapterEncryptionService.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include "shared/ChapterFile.h"

namespace buddyshare::writer {

namespace fs = std::filesystem;

namespace {

// The chapters/chapter-<NN>.enc path relative to the repo root -- what gets passed to `git
// add` (a real filesystem path would embed the writer's local folder layout into the repo).
std::string chapter_relative_path(int chapter_number) {
    std::ostringstream file_name;
    file_name << "chapters/chapter-" << std::setfill('0') << std::setw(2) << chapter_number
              << ".enc";
    return file_name.str();
}

}  // namespace

ChapterEncryptionService::ChapterEncryptionService(std::string working_directory,
                                                     shared::AccessRegistry& registry,
                                                     ReaderAccessController& access_controller,
                                                     shared::CryptoProvider& crypto,
                                                     IGitProcess& git_process,
                                                     shared::IConsole& console)
    : working_directory_(std::move(working_directory)),
      registry_(registry),
      access_controller_(access_controller),
      crypto_(crypto),
      git_process_(git_process),
      console_(console) {}

bool ChapterEncryptionService::is_bootstrapped() const {
    if (!fs::exists(fs::path(working_directory_) / ".git")) return false;
    const GitResult remote_result = git_process_.run({"remote", "get-url", "origin"});
    return remote_result.exit_code == 0;
}

EncryptChapterResult ChapterEncryptionService::encrypt_chapter(int chapter_number,
                                                                 const std::string& plaintext_path) {
    if (!is_bootstrapped()) {
        return EncryptChapterResult{
            EncryptChapterOutcome::not_bootstrapped,
            "Run \"Initialize repo\" first -- this folder hasn't been bootstrapped yet.",
            chapter_number, 0};
    }

    // Read plaintext before any prompt: a missing source file should fail fast, not after
    // the writer has already answered "add a reader?" and typed a password.
    std::ifstream plaintext_in(plaintext_path, std::ios::binary);
    if (!plaintext_in.is_open()) {
        return EncryptChapterResult{EncryptChapterOutcome::plaintext_not_found,
                                     "Could not read plaintext file: " + plaintext_path,
                                     chapter_number, 0};
    }
    std::ostringstream plaintext_buffer;
    plaintext_buffer << plaintext_in.rdbuf();
    const std::string plaintext_text = plaintext_buffer.str();

    maybe_register_reader();
    const std::string password = prompt_for_password();

    const shared::DerivedKey derived = crypto_.derive_key(password);
    const shared::Bytes plaintext_bytes(plaintext_text.begin(), plaintext_text.end());
    const shared::EncryptedContent encrypted = crypto_.encrypt(plaintext_bytes, derived.key);

    shared::ChapterFile file;
    file.salt_base64 = shared::CryptoProvider::base64_encode(derived.salt);
    file.iv_base64 = shared::CryptoProvider::base64_encode(encrypted.iv);
    file.ciphertext_base64 = shared::CryptoProvider::base64_encode(encrypted.ciphertext);

    int reader_count = 0;
    bool any_reader_skipped = false;
    for (const auto& reader : registry_.all_readers()) {
        if (!ReaderAccessController::is_authorized_for_chapter(reader, chapter_number)) continue;
        // A malformed stored public key (e.g. a copy/paste mistake at registration time)
        // makes CryptoProvider::wrap_key throw std::runtime_error. Skip just this reader
        // rather than letting the exception escape and take down the whole process --
        // mirrors AccessRegistry's malformed-line handling. Every other authorized reader
        // still gets their wrapped key, and the chapter is still written/committed/pushed.
        try {
            const shared::Bytes wrapped = crypto_.wrap_key(derived.key, reader.public_key_base64);
            file.wrapped_keys_base64[reader.name] = shared::CryptoProvider::base64_encode(wrapped);
            ++reader_count;
        } catch (const std::exception& error) {
            any_reader_skipped = true;
            console_.print("Skipping reader \"" + reader.name +
                                "\": stored public key is invalid (" + error.what() + ").",
                            shared::MessageStyle::warning);
        }
    }

    const std::string absolute_path =
        shared::ChapterFile::file_path_for_chapter(working_directory_, chapter_number);
    fs::create_directories(fs::path(absolute_path).parent_path());
    std::ofstream chapter_out(absolute_path, std::ios::trunc | std::ios::binary);
    chapter_out << file.to_json();
    chapter_out.close();

    const std::string relative_path = chapter_relative_path(chapter_number);
    git_process_.run({"add", relative_path});
    git_process_.run({"commit", "-m", "chore: encrypt chapter " + std::to_string(chapter_number)});
    console_.print("Pushing chapter " + std::to_string(chapter_number) + " to GitHub...",
                    shared::MessageStyle::info);
    const GitResult push_result = git_process_.run({"push"});

    if (push_result.exit_code != 0) {
        const std::string push_failed_message =
            "Chapter " + std::to_string(chapter_number) +
            " was encrypted locally but NOT pushed. Check your git connection before assuming "
            "readers can see it.";
        console_.print(push_failed_message, shared::MessageStyle::error);
        return EncryptChapterResult{EncryptChapterOutcome::push_failed, push_failed_message,
                                     chapter_number, reader_count};
    }

    std::string message = "Encrypted chapter " + std::to_string(chapter_number) + " for " +
                           std::to_string(reader_count) +
                           " reader(s). Plaintext remains only in your local working folder.";
    const EncryptChapterOutcome outcome = any_reader_skipped
                                               ? EncryptChapterOutcome::some_readers_skipped
                                               : EncryptChapterOutcome::success;
    if (any_reader_skipped) {
        message += " One or more readers were skipped due to an invalid stored public key.";
    }
    console_.print(message, shared::MessageStyle::success);

    return EncryptChapterResult{outcome, message, chapter_number, reader_count};
}

bool ChapterEncryptionService::register_reader() {
    const std::string name = console_.prompt("New reader's display name:");
    const std::string public_key = console_.prompt(
        "New reader's public key (they get this from their own first Reader run -- paste it "
        "exactly):");
    const RegistrationDetails details = access_controller_.collect_registration_details();

    shared::ReaderEntry entry;
    entry.name = name;
    entry.public_key_base64 = public_key;
    entry.level = details.level;
    entry.chapters = details.chapters;

    if (!registry_.append_reader(entry)) return false;

    git_process_.run({"add", "access.txt"});
    git_process_.run({"commit", "-m", "chore: add reader " + name + " to access.txt"});
    console_.print("Pushing access.txt to GitHub...", shared::MessageStyle::info);
    const GitResult push_result = git_process_.run({"push"});
    if (push_result.exit_code != 0) {
        console_.print("Reader \"" + name +
                            "\" registered locally, but the push failed; run `git push` manually "
                            "so readers can see the update.",
                        shared::MessageStyle::warning);
    }
    return true;
}

void ChapterEncryptionService::maybe_register_reader() {
    if (registry_.all_readers().empty()) {
        console_.print(
            "No readers are registered yet in access.txt; proceeding now would encrypt this "
            "chapter with zero wrapped keys.",
            shared::MessageStyle::warning);
    }

    const std::string answer = console_.prompt("Add a reader? (y/n)");
    if (answer == "y" || answer == "Y") register_reader();
}

std::string ChapterEncryptionService::prompt_for_password() const {
    for (;;) {
        const std::string password = console_.prompt_masked("Encryption password:");
        if (!password.empty()) return password;
        console_.print("Password cannot be blank.", shared::MessageStyle::warning);
    }
}

}  // namespace buddyshare::writer
