#include "writer/SystemGitProcess.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>

namespace buddyshare::writer {

namespace {

// Quotes a single argument the way CommandLineToArgvW expects, so an argument containing
// spaces or quotes (e.g. "Initial commit", or a username) round-trips as one argument
// rather than being split or used to inject extra ones.
std::wstring quote_argument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;

    std::wstring quoted = L"\"";
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(arg[i]);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring to_wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }

std::wstring build_command_line(const std::wstring& executable_name,
                                 const std::vector<std::string>& args) {
    std::wstring command_line = quote_argument(executable_name);
    for (const auto& arg : args) {
        command_line += L' ';
        command_line += quote_argument(to_wide(arg));
    }
    return command_line;
}

// Spawns executable_name (resolved via the system PATH, as CreateProcessW does when
// lpApplicationName is null) with args in working_directory, waits for it to exit, and
// returns its exit code plus everything it wrote to stdout/stderr, combined, as std_out.
GitResult spawn_and_capture(const std::wstring& executable_name,
                             const std::vector<std::string>& args,
                             const std::string& working_directory) {
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &security_attributes, 0)) {
        return GitResult{-1, "", "Failed to create a pipe for the child process output."};
    }
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = write_handle;
    startup_info.hStdError = write_handle;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::wstring command_line = build_command_line(executable_name, args);
    const std::wstring wide_working_directory = to_wide(working_directory);

    const BOOL created = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, /*bInheritHandles=*/TRUE,
        CREATE_NO_WINDOW, nullptr,
        wide_working_directory.empty() ? nullptr : wide_working_directory.c_str(),
        &startup_info, &process_info);

    CloseHandle(write_handle);

    if (!created) {
        CloseHandle(read_handle);
        return GitResult{-1, "",
                          "Failed to start '" + std::string(executable_name.begin(),
                                                             executable_name.end()) +
                              "'. Is it installed and on PATH?"};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD bytes_read = 0;
    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                     nullptr) &&
           bytes_read > 0) {
        output.append(buffer.data(), bytes_read);
    }
    CloseHandle(read_handle);

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    return GitResult{static_cast<int>(exit_code), output, ""};
}

}  // namespace

SystemGitProcess::SystemGitProcess(std::string working_directory)
    : working_directory_(std::move(working_directory)) {}

bool SystemGitProcess::is_git_available() {
    return spawn_and_capture(L"git", {"--version"}, working_directory_).exit_code == 0;
}

bool SystemGitProcess::install_git_via_winget() {
    const GitResult result =
        spawn_and_capture(L"winget", {"install", "--id", "Git.Git", "-e", "--source", "winget"},
                           working_directory_);
    return result.exit_code == 0;
}

GitResult SystemGitProcess::run(const std::vector<std::string>& args) {
    return spawn_and_capture(L"git", args, working_directory_);
}

}  // namespace buddyshare::writer
