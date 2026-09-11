#ifndef BUDDYSHARE_SHARED_CONSOLESTYLE_H
#define BUDDYSHARE_SHARED_CONSOLESTYLE_H

#include <string>

#include "shared/Console.h"

namespace buddyshare::shared {

// Spec 005: pure string-formatting helpers with no OS/console dependency, so they're unit
// -testable without a real terminal. ConsoleIO is the only caller that decides *whether* to
// colorize (it owns the tty/NO_COLOR checks); these functions just do the formatting.
class ConsoleStyle {
public:
    // True only when stdout is a real interactive console and NO_COLOR isn't set.
    static bool should_colorize(bool is_tty, bool no_color_env_set);

    // Returns text unchanged when colorize is false; otherwise wraps it in the ANSI code for
    // style, followed by a reset code.
    static std::string apply_style(const std::string& text, MessageStyle style, bool colorize);

    // Returns a boxed/bordered plain-text rendering of title. Styling (e.g. MessageStyle::header)
    // is applied separately via apply_style when the banner is printed.
    static std::string make_banner(const std::string& title);
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_CONSOLESTYLE_H
