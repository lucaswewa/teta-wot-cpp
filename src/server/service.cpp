#include "teta/wot/service.hpp"

#include <string_view>

#include "teta/wot/log.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mutex>
#include <optional>
#include <system_error>
#include <utility>
#endif

namespace teta::wot::service {

namespace {

// One argument as CommandLineToArgvW reads it back: in quotes if it has
// spaces, tabs or quotes (or is empty), with backslashes doubled before a
// quote.
std::string quote(std::string_view argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return std::string(argument);
    }
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless a quote follows them.
        out.append(c == '"' ? 2 * backslashes + 1 : backslashes, '\\');
        out += c;
        backslashes = 0;
    }
    // The closing quote follows them: double them.
    out.append(2 * backslashes, '\\');
    out += '"';
    return out;
}

}  // namespace

std::string command_line(const std::filesystem::path& executable, const std::vector<std::string>& arguments) {
    // A path can't hold a quote: the executable is quoted as it is.
    const auto text = executable.u8string();
    std::string line = "\"" + std::string(text.begin(), text.end()) + "\"";
    for (const auto& argument : arguments) {
        line += " " + quote(argument);
    }
    return line;
}

#ifdef _WIN32

namespace {

/// How long the SCM is told a start takes at most.
constexpr DWORD start_hint_ms = 30'000;

std::wstring wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length > 0 ? length - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    return out;
}

/// The SCM's refusal, with Windows' message for `code`.
ServiceError refused(DWORD code) {
    return ServiceError(
        "the Service Control Manager refused: " + std::system_category().message(static_cast<int>(code)) +
        " (os error " + std::to_string(code) + ")");
}

/// A service or SCM handle, closed when destroyed.
class Handle {
  public:
    explicit Handle(SC_HANDLE handle) : handle_(handle) {
        if (!handle_) {
            throw refused(GetLastError());
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { CloseServiceHandle(handle_); }
    SC_HANDLE get() const { return handle_; }

  private:
    SC_HANDLE handle_;
};

/// What the SCM's thread runs: the name, the stop wait hint, and `serve`.
struct Service {
    std::string name;
    DWORD stop_hint_ms = 0;
    Serve serve;
};

std::mutex mutex;
/// Set by `run_as_service`, taken by `service_main`.
std::optional<Service> pending;
/// Whether `run_as_service` has been called: Windows takes one service
/// dispatcher per process, even after a failed attempt.
bool dispatched = false;

/// The running service, shared with its control handler.
struct Running {
    SERVICE_STATUS_HANDLE status = nullptr;
    DWORD stop_hint_ms = 0;
    std::stop_source stop;
    std::mutex report_mutex;

    void report(DWORD state, DWORD wait_hint_ms, DWORD exit) {
        SERVICE_STATUS status_now{};
        status_now.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        status_now.dwCurrentState = state;
        status_now.dwControlsAccepted =
            state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
        if (exit == 0) {
            status_now.dwWin32ExitCode = NO_ERROR;
        } else {
            status_now.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            status_now.dwServiceSpecificExitCode = exit;
        }
        status_now.dwWaitHint = wait_hint_ms;
        std::lock_guard lock(report_mutex);
        if (!SetServiceStatus(status, &status_now)) {
            log::warn("the service's status couldn't be reported: {}", refused(GetLastError()).what());
        }
    }
};

DWORD WINAPI control(DWORD request, DWORD, LPVOID, LPVOID context) {
    auto& running = *static_cast<Running*>(context);
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            log::info("the Service Control Manager asked the service to stop");
            // Before the stop signal, so that *stopped* comes after.
            running.report(SERVICE_STOP_PENDING, running.stop_hint_ms, 0);
            running.stop.request_stop();
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void run(Service service, std::vector<std::string> arguments) {
    // Static: the control handler may be called until the process ends, and
    // a process runs as a service only once.
    static Running running;
    running.stop_hint_ms = service.stop_hint_ms;
    running.status = RegisterServiceCtrlHandlerExW(wide(service.name).c_str(), control, &running);
    if (!running.status) {
        throw refused(GetLastError());
    }
    running.report(SERVICE_START_PENDING, start_hint_ms, 0);
    running.report(SERVICE_RUNNING, 0, 0);
    int code = 1;
    try {
        code = service.serve(std::move(arguments), running.stop.get_token());
    } catch (const std::exception& error) {
        log::error("the service failed: {}", error.what());
    }
    const DWORD exit = code >= 0 && code <= 255 ? static_cast<DWORD>(code) : 1;
    log::info("the service stopped with exit code {}", exit);
    running.report(SERVICE_STOPPED, 0, exit);
}

/// The service's entry point, on the SCM's thread.
void WINAPI service_main(DWORD argc, LPWSTR* argv) {
    std::optional<Service> service;
    {
        std::lock_guard lock(mutex);
        service = std::exchange(pending, std::nullopt);
    }
    if (!service) {
        return;
    }
    std::vector<std::string> arguments;
    for (DWORD i = 0; i < argc; ++i) {
        arguments.push_back(narrow(argv[i]));
    }
    try {
        run(std::move(*service), std::move(arguments));
    } catch (const std::exception& error) {
        log::error("the service failed: {}", error.what());
    }
}

DWORD milliseconds(std::chrono::nanoseconds duration) {
    const auto ms = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
    return ms <= 0 ? 0 : ms >= MAXDWORD ? MAXDWORD : static_cast<DWORD>(ms);
}

}  // namespace

void run_as_service(const std::string& name, std::chrono::nanoseconds stop_hint, Serve serve) {
    {
        std::lock_guard lock(mutex);
        if (dispatched) {
            throw AlreadyRunning();
        }
        dispatched = true;
        pending = Service{name, milliseconds(stop_hint), std::move(serve)};
    }
    // Blocks until the service has stopped; the SCM calls `service_main` on
    // a thread of its own.
    std::wstring service_name = wide(name);
    const SERVICE_TABLE_ENTRYW table[] = {{service_name.data(), service_main}, {nullptr, nullptr}};
    const bool ran = StartServiceCtrlDispatcherW(table) != 0;
    const DWORD error = ran ? 0 : GetLastError();
    {
        std::lock_guard lock(mutex);
        pending.reset();
    }
    if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        throw NotAService();
    }
    if (error == ERROR_SERVICE_ALREADY_RUNNING) {
        // The application started a dispatcher of its own.
        throw AlreadyRunning();
    }
    if (!ran) {
        throw refused(error);
    }
}

void install(const InstallOptions& options) {
    const Handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    const auto line = wide(command_line(options.executable, options.arguments));
    const Handle service(CreateServiceW(
        manager.get(), wide(options.name).c_str(), wide(options.display_name).c_str(), SERVICE_CHANGE_CONFIG,
        SERVICE_WIN32_OWN_PROCESS, options.automatic ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL, line.c_str(), nullptr, nullptr, nullptr,
        nullptr,  // LocalSystem
        nullptr));
    std::wstring text = wide(options.description);
    SERVICE_DESCRIPTIONW description{text.data()};
    if (!ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DESCRIPTION, &description)) {
        throw refused(GetLastError());
    }
}

void uninstall(const std::string& name) {
    const Handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    const Handle service(
        OpenServiceW(manager.get(), wide(name).c_str(), SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE));
    SERVICE_STATUS status{};
    if (!QueryServiceStatus(service.get(), &status)) {
        throw refused(GetLastError());
    }
    if (status.dwCurrentState != SERVICE_STOPPED) {
        // Deleting waits for the service to stop.
        (void)ControlService(service.get(), SERVICE_CONTROL_STOP, &status);
    }
    if (!DeleteService(service.get())) {
        throw refused(GetLastError());
    }
}

#else

void run_as_service(const std::string&, std::chrono::nanoseconds, Serve) {
    throw NotAService();
}

void install(const InstallOptions&) {
    throw ServiceError("Windows services exist only on Windows: use a systemd unit (systemd::unit_file)");
}

void uninstall(const std::string&) {
    throw ServiceError("Windows services exist only on Windows: use a systemd unit (systemd::unit_file)");
}

#endif

}  // namespace teta::wot::service
