#include "shared/ConsoleIO.h"

#include <cstdlib>
#include <io.h>
#include <iostream>

#include <conio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "shared/ConsoleStyle.h"

namespace buddyshare::shared {

namespace {
constexpr int kBackspace = 8;
constexpr int kCarriageReturn = 13;

// Separate from prompt_masked's _isatty(_fileno(stdin)) check -- different fd, different
// purpose (spec 005 Behavior item 3) -- must not be merged or share one flag.
bool stdout_is_tty() { return _isatty(_fileno(stdout)) != 0; }

// Per the no-color.org convention: any non-empty value disables color.
bool no_color_env_is_set() {
    const char* value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
}

// Enables ANSI escape processing for this console once at startup. Legacy cmd.exe/conhost that
// rejects ENABLE_VIRTUAL_TERMINAL_PROCESSING must never crash or error here -- just leave the
// console in plain mode, which stdout_is_tty()/should_colorize() work fine without.
void enable_vt_processing() {
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return;
    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
}  // namespace

ConsoleIO::ConsoleIO() { enable_vt_processing(); }

std::string ConsoleIO::prompt(const std::string& message) {
    std::cout << message << ' ';
    std::string answer;
    std::getline(std::cin, answer);
    return answer;
}

std::string ConsoleIO::prompt_masked(const std::string& message) {
    std::cout << message << ' ';

    // _getch() reads directly from the console and never sees redirected/piped stdin, which
    // would otherwise block forever waiting for a keypress that can't arrive. Fall back to a
    // plain (unmasked) read in that case so the program still runs when stdin isn't a real
    // console (e.g. piped input).
    if (!_isatty(_fileno(stdin))) {
        std::string answer;
        std::getline(std::cin, answer);
        return answer;
    }

    std::string answer;
    for (;;) {
        const int ch = _getch();
        if (ch == kCarriageReturn || ch == '\n') break;
        if (ch == kBackspace) {
            if (!answer.empty()) {
                answer.pop_back();
                std::cout << "\b \b";
            }
            continue;
        }
        answer.push_back(static_cast<char>(ch));
        std::cout << '*';
    }
    std::cout << '\n';
    return answer;
}

void ConsoleIO::print(const std::string& message, MessageStyle style) {
    const bool colorize = ConsoleStyle::should_colorize(stdout_is_tty(), no_color_env_is_set());
    std::cout << ConsoleStyle::apply_style(message, style, colorize) << '\n';
}

}  // namespace buddyshare::shared
