#include "shared/ConsoleStyle.h"

namespace buddyshare::shared {

namespace {

constexpr const char* kReset = "\x1B[0m";

// One ANSI SGR code per MessageStyle. success/error/warning/info stay visually distinct (and
// distinct from plain/header) per the spec's color-blind-accessibility guidance -- color is an
// accent on top of wording that already distinguishes them, never the only signal.
const char* ansi_code(MessageStyle style) {
    switch (style) {
        case MessageStyle::header:
            return "\x1B[1;36m";  // bold cyan
        case MessageStyle::success:
            return "\x1B[32m";  // green
        case MessageStyle::warning:
            return "\x1B[33m";  // yellow
        case MessageStyle::error:
            return "\x1B[31m";  // red
        case MessageStyle::info:
            return "\x1B[36m";  // cyan
        case MessageStyle::plain:
            return "\x1B[0m";  // no visual change, just wrapped for a uniform contract
    }
    return "\x1B[0m";
}

}  // namespace

bool ConsoleStyle::should_colorize(bool is_tty, bool no_color_env_set) {
    return is_tty && !no_color_env_set;
}

std::string ConsoleStyle::apply_style(const std::string& text, MessageStyle style, bool colorize) {
    if (!colorize) return text;
    return std::string(ansi_code(style)) + text + kReset;
}

std::string ConsoleStyle::make_banner(const std::string& title) {
    const std::string border(title.size() + 4, '-');
    return "+" + border + "+\n| " + title + " |\n+" + border + "+";
}

}  // namespace buddyshare::shared
