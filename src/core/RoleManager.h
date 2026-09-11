#ifndef BUDDYSHARE_CORE_ROLEMANAGER_H
#define BUDDYSHARE_CORE_ROLEMANAGER_H

#include <optional>
#include <string>

#include "shared/Console.h"

namespace buddyshare::core {

enum class Role { writer, reader };

// Abstraction over the hidden role-lock file (%APPDATA%\BuddyShare\.role), so RoleManager can
// be unit-tested without touching the real filesystem.
class IRoleStore {
public:
    virtual ~IRoleStore() = default;

    // Returns the raw file contents, or std::nullopt if the file is missing/unreadable.
    virtual std::optional<std::string> read() const = 0;

    // Writes role_text ("writer" or "reader") to the file and sets the Windows hidden
    // attribute on it.
    virtual void write(const std::string& role_text) = 0;
};

// First-run role prompt and permanent role lock (spec 001, section "0. Role selection").
class RoleManager {
public:
    RoleManager(IRoleStore& store, shared::IConsole& console);

    // Reads the role-lock file; if missing or unparseable, prompts (re-prompting on any input
    // other than W/R, case-insensitively) until a valid answer is given, persists it, then
    // returns it. If the file already holds a valid role, returns it with no prompt.
    Role resolve_role();

private:
    IRoleStore& store_;
    shared::IConsole& console_;
};

}  // namespace buddyshare::core

#endif  // BUDDYSHARE_CORE_ROLEMANAGER_H
