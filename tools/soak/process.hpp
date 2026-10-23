// The server under soak, as a process of its own: started with its output
// in a log file, interrupted as Ctrl-C does, killed, waited for, and
// measured (memory and handles).

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "teta/wot/service.hpp"

#ifdef _WIN32
#include <windows.h>
// After windows.h.
#include <psapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

extern char** environ;
#endif

namespace soak {

/// The server's memory and handles.
struct Memory {
    /// The working set (resident memory), in bytes: Windows trims it, so it
    /// jumps.
    std::uint64_t working_set = 0;
    /// The private bytes (Windows' committed private memory; Linux's
    /// anonymous resident memory): what the process has allocated. It only
    /// grows if something is kept, so it is what the leak check looks at.
    std::uint64_t private_bytes = 0;
    /// The open handles (sockets, files, threads, events), or descriptors.
    std::uint64_t handles = 0;
};

class Process {
  public:
    /// Starts `executable` with `arguments`, its output in `log`.
    Process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
            const std::filesystem::path& log) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        const HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("can't write " + log.string());
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output;
        startup.hStdError = output;
        const auto line = teta::wot::service::command_line(executable, arguments);
        std::wstring wide(
            static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
        PROCESS_INFORMATION info{};
        // Its own process group, so that Ctrl-Break reaches it alone.
        const bool started = CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE,
                                            CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &info) != 0;
        CloseHandle(output);
        if (!started) {
            throw std::runtime_error("can't start " + executable.string());
        }
        CloseHandle(info.hThread);
        process_ = info.hProcess;
        pid_ = info.dwProcessId;
#else
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, 1, 2);
        std::vector<std::string> args = {executable.string()};
        args.insert(args.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        const int failed = posix_spawn(&pid_, executable.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (failed != 0) {
            throw std::runtime_error("can't start " + executable.string());
        }
#endif
    }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process() {
        if (!exit_code_) {
            kill();
            (void)wait(std::chrono::seconds(10));
        }
#ifdef _WIN32
        CloseHandle(process_);
#endif
    }

    std::uint64_t pid() const { return static_cast<std::uint64_t>(pid_); }

    /// The exit code, if the process has exited.
    std::optional<int> exited() { return wait(std::chrono::milliseconds(0)); }

    /// Waits up to `timeout` for the process to exit: its exit code, or
    /// nullopt if it is still running.
    std::optional<int> wait(std::chrono::milliseconds timeout) {
        if (exit_code_) {
            return exit_code_;
        }
#ifdef _WIN32
        if (WaitForSingleObject(process_, static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process_, &code);
            exit_code_ = static_cast<int>(code);
        }
#else
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            int status = 0;
            if (waitpid(pid_, &status, WNOHANG) == pid_) {
                exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            usleep(20'000);
        }
#endif
        return exit_code_;
    }

    /// Asks the process to stop, as Ctrl-C does (Ctrl-Break on Windows,
    /// SIGINT elsewhere).
    void interrupt() {
#ifdef _WIN32
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid_);
#else
        ::kill(pid_, SIGINT);
#endif
    }

    void kill() {
#ifdef _WIN32
        TerminateProcess(process_, 1);
#else
        ::kill(pid_, SIGKILL);
#endif
    }

    /// The process's memory and handles, if it can be measured.
    std::optional<Memory> memory() const {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX counters{};
        DWORD handles = 0;
        if (!GetProcessMemoryInfo(process_, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof counters) ||
            !GetProcessHandleCount(process_, &handles)) {
            return std::nullopt;
        }
        return Memory{counters.WorkingSetSize, counters.PrivateUsage, handles};
#else
        std::ifstream status("/proc/" + std::to_string(pid_) + "/status");
        Memory memory;
        for (std::string line; std::getline(status, line);) {
            std::istringstream fields(line);
            std::string name;
            std::uint64_t kilobytes = 0;
            fields >> name >> kilobytes;
            if (name == "VmRSS:") {
                memory.working_set = kilobytes * 1024;
            } else if (name == "RssAnon:") {
                memory.private_bytes = kilobytes * 1024;
            }
        }
        std::error_code error;
        for (std::filesystem::directory_iterator fd("/proc/" + std::to_string(pid_) + "/fd", error), end;
             !error && fd != end; fd.increment(error)) {
            ++memory.handles;
        }
        if (memory.working_set == 0) {
            return std::nullopt;
        }
        return memory;
#endif
    }

  private:
#ifdef _WIN32
    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
#else
    pid_t pid_ = 0;
#endif
    std::optional<int> exit_code_;
};

}  // namespace soak
