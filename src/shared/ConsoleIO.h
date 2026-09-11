#ifndef BUDDYSHARE_SHARED_CONSOLEIO_H
#define BUDDYSHARE_SHARED_CONSOLEIO_H

#include <string>

#include "shared/Console.h"

namespace buddyshare::shared {

// Real IConsole implementation: reads from stdin and writes to stdout, masking
// prompt_masked() input (used for the GitHub token) so it is never echoed to the terminal.
class ConsoleIO : public IConsole {
public:
    ConsoleIO();

    std::string prompt(const std::string& message) override;
    std::string prompt_masked(const std::string& message) override;
    void print(const std::string& message, MessageStyle style = MessageStyle::plain) override;
};

}  // namespace buddyshare::shared

#endif  // BUDDYSHARE_SHARED_CONSOLEIO_H
