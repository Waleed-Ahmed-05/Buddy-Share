#ifndef BUDDYSHARE_SHARED_CONSOLE_H
#define BUDDYSHARE_SHARED_CONSOLE_H

#include <string>

namespace buddyshare::shared {

// Spec 005: categorizes a print() call by what it represents, so ConsoleIO can render it
// distinctly (color/banner) without any caller needing to know how that rendering works.
enum class MessageStyle {
    plain,
    header,
    success,
    warning,
    error,
    info,
};

// Abstraction over console input/output so callers (RoleManager, GitBootstrapService) can be
// unit-tested without touching real stdin/stdout, and so token input can be masked.
class IConsole {
public:
    virtual ~IConsole() = default;

    // Prints message, reads a line of input, returns it (not masked, not trimmed).
    virtual std::string prompt(const std::string& message) = 0;

    // Same as prompt(), but input is not echoed to the terminal (used for tokens).
    virtual std::string prompt_masked(const std::string& message) = 0;

    // Prints a line of output with no input requested. style defaults to plain so every
    // pre-005 call site remains valid unchanged (spec 005 Behavior item 1).
    virtual void print(const std::string& message, MessageStyle style = MessageStyle::plain) = 0;
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_CONSOLE_H
