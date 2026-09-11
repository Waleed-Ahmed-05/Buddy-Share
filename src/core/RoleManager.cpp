#include "core/RoleManager.h"

#include <cctype>

namespace buddyshare::core {

namespace {

constexpr const char* kRolePrompt = "Are you the Writer or a Reader? [W/R]";

std::optional<Role> parse_role(const std::string& text) {
    if (text == "writer") return Role::writer;
    if (text == "reader") return Role::reader;
    return std::nullopt;
}

}  // namespace

RoleManager::RoleManager(IRoleStore& store, shared::IConsole& console)
    : store_(store), console_(console) {}

Role RoleManager::resolve_role() {
    const std::optional<std::string> stored = store_.read();
    if (stored.has_value()) {
        const std::optional<Role> parsed = parse_role(*stored);
        if (parsed.has_value()) return *parsed;
    }

    // Missing or unparseable (corrupted) role file: re-prompt until a valid W/R answer.
    while (true) {
        const std::string answer = console_.prompt(kRolePrompt);
        if (answer.size() == 1) {
            const char letter =
                static_cast<char>(std::tolower(static_cast<unsigned char>(answer[0])));
            if (letter == 'w') {
                store_.write("writer");
                return Role::writer;
            }
            if (letter == 'r') {
                store_.write("reader");
                return Role::reader;
            }
        }
    }
}

}  // namespace buddyshare::core
