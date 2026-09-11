#include "writer/WriterMenu.h"

namespace buddyshare::writer {

WriterMenu::WriterMenu(GitBootstrapService& bootstrap) : bootstrap_(bootstrap) {}

std::vector<WriterCommand> WriterMenu::available_commands() const {
    std::vector<WriterCommand> commands;
    if (!bootstrap_.is_already_bootstrapped()) {
        commands.push_back(WriterCommand::initialize_repo);
    } else {
        commands.push_back(WriterCommand::manage_reader_access);
        commands.push_back(WriterCommand::encrypt_chapter);
    }
    return commands;
}

}  // namespace buddyshare::writer
