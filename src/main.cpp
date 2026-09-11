#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/RoleManager.h"
#include "core/RoleStore.h"
#include "reader/ChapterFetcher.h"
#include "reader/DpapiPrivateKeyStore.h"
#include "reader/ReaderMenu.h"
#include "reader/ReaderViewerService.h"
#include "shared/AccessRegistry.h"
#include "shared/ChapterFile.h"
#include "shared/ConsoleIO.h"
#include "shared/ConsoleStyle.h"
#include "shared/CryptoProvider.h"
#include "shared/HttpGitHubClient.h"
#include "writer/ChapterEncryptionService.h"
#include "writer/GitBootstrapService.h"
#include "writer/ReaderAccessController.h"
#include "writer/SystemGitProcess.h"
#include "writer/WriterMenu.h"

namespace {

using buddyshare::shared::ConsoleStyle;
using buddyshare::shared::IConsole;
using buddyshare::shared::MessageStyle;

constexpr const char* kGitHubRateLimitMessage =
    "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to reset, "
    "or connect to a VPN for a new IP address.";

bool is_all_digits(const std::string& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
}

// std::stoi throws std::out_of_range for a numeric string too large for int (e.g. a 20-digit
// chapter choice) -- never let that escape as an unhandled crash.
std::optional<int> parse_int(const std::string& text) {
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Runs the Writer role's menu loop: offers "Initialize repo" (spec 001) until the one-time
// git bootstrap has run, then just offers Exit. Later specs (002/003) add the
// chapter/access-management items into this same loop.
void run_writer_menu(IConsole& console) {
    const std::string working_directory = std::filesystem::current_path().string();
    buddyshare::shared::HttpGitHubClient github_client;
    buddyshare::writer::SystemGitProcess git_process(working_directory);
    buddyshare::writer::GitBootstrapService bootstrap(working_directory, github_client,
                                                       git_process, console);
    buddyshare::writer::WriterMenu menu(bootstrap);
    buddyshare::shared::AccessRegistry access_registry(working_directory + "/access.txt",
                                                         console);
    access_registry.load_from_file();
    buddyshare::writer::ReaderAccessController reader_access(access_registry, console,
                                                               git_process);
    buddyshare::shared::CryptoProvider crypto;
    buddyshare::writer::ChapterEncryptionService chapter_encryption(
        working_directory, access_registry, reader_access, crypto, git_process, console);

    for (;;) {
        const auto commands = menu.available_commands();
        bool offers_initialize = false;
        bool offers_manage_access = false;
        bool offers_encrypt = false;
        console.print("\n" + ConsoleStyle::make_banner("Writer Menu"), MessageStyle::header);
        for (const auto command : commands) {
            if (command == buddyshare::writer::WriterCommand::initialize_repo) {
                console.print("1) Initialize repo");
                offers_initialize = true;
            } else if (command == buddyshare::writer::WriterCommand::manage_reader_access) {
                console.print("2) Manage reader access");
                offers_manage_access = true;
            } else if (command == buddyshare::writer::WriterCommand::encrypt_chapter) {
                console.print("3) Encrypt a chapter");
                offers_encrypt = true;
            }
        }
        console.print("0) Exit");
        const std::string choice = console.prompt("Choose an option:");

        if (offers_initialize && choice == "1") {
            bootstrap.run();
        } else if (offers_manage_access && choice == "2") {
            const std::string name = console.prompt("Reader name to edit:");
            if (!reader_access.edit_existing_reader(name)) {
                console.print("No reader named \"" + name + "\" is registered yet.",
                              MessageStyle::warning);
            }
        } else if (offers_encrypt && choice == "3") {
            const std::string chapter_text = console.prompt("Chapter number (e.g. 1):");
            if (!is_all_digits(chapter_text)) {
                console.print("Chapter number must be numeric.", MessageStyle::warning);
                continue;
            }
            const std::string plaintext_path = console.prompt(
                "Path to the plaintext chapter file (e.g. chapters-source/chapter-01.txt):");
            try {
                chapter_encryption.encrypt_chapter(std::stoi(chapter_text), plaintext_path);
            } catch (const std::out_of_range&) {
                console.print("Chapter number is too large.", MessageStyle::warning);
            }
        } else if (choice == "0") {
            return;
        } else {
            console.print("Unrecognized option.", MessageStyle::warning);
        }
    }
}

// Runs the Reader role's whole loop (spec 004): launch prompts -> keypair check -> fetch
// access.txt -> build the chapter menu -> decrypt/display on selection. Every error path that
// spec 004's Behavior section calls "return to launch prompts" does exactly that (the outer
// for (;;)); "0) Exit" is the only way out, mirroring run_writer_menu's "0) Exit".
void run_reader_menu(IConsole& console) {
    buddyshare::shared::HttpGitHubClient github_client;
    buddyshare::shared::CryptoProvider crypto;
    buddyshare::reader::DpapiPrivateKeyStore key_store;
    buddyshare::reader::ReaderViewerService viewer(key_store, crypto, console);

    for (;;) {
        console.print(
            "Enter the writer's GitHub details and the username you registered (or want to "
            "register) with them:",
            MessageStyle::info);
        const std::string writer_username = console.prompt("Writer's GitHub username:");
        const std::string repo_name = console.prompt("Repository name:");
        const std::string reader_username = console.prompt("Your username:");

        // Step 2: reuses/generates the local keypair regardless of what follows -- the writer
        // may already have this reader's key from a prior run.
        const buddyshare::reader::KeypairInfo keypair = viewer.ensure_local_keypair(reader_username);

        buddyshare::reader::ChapterFetcher fetcher(github_client, writer_username, repo_name);
        const auto access_result = fetcher.fetch_access_txt();
        if (access_result.outcome == buddyshare::reader::FetchAccessTxtOutcome::not_found) {
            console.print("Could not find that writer's repository, or it has no access.txt yet.",
                          MessageStyle::error);
            continue;
        }
        if (access_result.outcome == buddyshare::reader::FetchAccessTxtOutcome::network_error) {
            console.print(access_result.error_message, MessageStyle::error);
            continue;
        }

        // access.txt is fetched over the network here instead of read from a local file, but
        // AccessRegistry (spec 002/003) remains the single parser for both roles; file_path is
        // unused since this role only ever reads (never persists) the registry.
        buddyshare::shared::AccessRegistry registry("", console);
        registry.load_from_text(access_result.raw_content);

        const auto entry = registry.find_by_name(reader_username);
        if (!entry.has_value()) {
            console.print(
                "You're not registered with this writer yet -- send them this public key:",
                MessageStyle::info);
            console.print(keypair.public_key_base64);
            continue;
        }

        std::vector<int> candidates;
        if (entry->level == buddyshare::shared::AccessLevel::master) {
            const auto listing = fetcher.list_master_candidates();
            if (listing.rate_limited) {
                console.print(kGitHubRateLimitMessage, MessageStyle::error);
                continue;
            }
            candidates = listing.candidates;
        } else {
            candidates.assign(entry->chapters.begin(), entry->chapters.end());
            std::sort(candidates.begin(), candidates.end());
        }

        console.print("Fetching chapter availability from GitHub...", MessageStyle::info);
        std::vector<buddyshare::reader::ChapterMenuEntry> menu_entries;
        std::unordered_map<int, buddyshare::shared::ChapterFile> readable_files;
        bool candidate_fetch_rate_limited = false;
        for (const int chapter_number : candidates) {
            const auto fetch_result = fetcher.try_fetch_chapter(chapter_number, reader_username);
            if (fetch_result.rate_limited) {
                candidate_fetch_rate_limited = true;
                break;
            }
            menu_entries.push_back({chapter_number, fetch_result.availability});
            if (fetch_result.file.has_value()) {
                readable_files[chapter_number] = *fetch_result.file;
            }
        }
        if (candidate_fetch_rate_limited) {
            console.print(kGitHubRateLimitMessage, MessageStyle::error);
            continue;
        }
        const buddyshare::reader::ReaderMenu menu(menu_entries);

        for (;;) {
            console.print("\n" + ConsoleStyle::make_banner("Reader Menu"), MessageStyle::header);
            for (const auto& menu_entry : menu.entries()) {
                const bool readable =
                    menu_entry.availability == buddyshare::reader::ChapterAvailability::readable;
                console.print("Chapter " + std::to_string(menu_entry.chapter_number) + " (" +
                               (readable ? "Readable" : "Pending -- not yet available") + ")");
            }
            console.print("0) Exit");
            const std::string choice = console.prompt("Choose a chapter number, or 0 to exit:");

            if (choice == "0") return;
            const std::optional<int> chapter_choice = parse_int(choice);
            if (!chapter_choice.has_value() || !menu.is_selectable(*chapter_choice)) {
                console.print("That chapter isn't available to read yet.", MessageStyle::error);
                continue;
            }

            const auto decrypt_result =
                viewer.decrypt_chapter(reader_username, readable_files.at(*chapter_choice));
            switch (decrypt_result.outcome) {
                case buddyshare::reader::DecryptChapterOutcome::success:
                    console.print("\n" + decrypt_result.plaintext + "\n");
                    break;
                case buddyshare::reader::DecryptChapterOutcome::reader_not_wrapped:
                    console.print("This chapter doesn't include your wrapped key yet.",
                                  MessageStyle::error);
                    break;
                case buddyshare::reader::DecryptChapterOutcome::key_unwrap_failed:
                    console.print(
                        "Could not unlock this chapter with your local key -- it may no longer "
                        "match what the writer has on file.",
                        MessageStyle::error);
                    break;
                case buddyshare::reader::DecryptChapterOutcome::decryption_failed:
                    console.print(
                        "Decryption failed -- the chapter data may be corrupted or tampered with.",
                        MessageStyle::error);
                    break;
            }
        }
    }
}

}  // namespace

int main() {
    buddyshare::shared::ConsoleIO console;
    buddyshare::core::RoleStore role_store;
    buddyshare::core::RoleManager role_manager(role_store, console);

    const buddyshare::core::Role role = role_manager.resolve_role();
    if (role == buddyshare::core::Role::writer) {
        run_writer_menu(console);
    } else {
        run_reader_menu(console);
    }
    return 0;
}
