// The simulated microscope as a service: a Windows service, or on Linux a
// systemd unit.
//
//     example-microscope-service install [--port 5000]    register the service (administrator; root on
//                                                         Linux, where `--user USER` sets who it runs as)
//     example-microscope-service uninstall                stop and remove it (administrator, root)
//     example-microscope-service run -c CONFIG [--port N] [--log FILE]
//                                                         what the Service Control Manager (or systemd)
//                                                         starts
//     example-microscope-service console -c CONFIG [--port N]
//                                                         the same server in a console, until Ctrl-C
//     example-microscope-service                          a demonstration of the stop path, then exit
//
// A service stops the way a console server does: the Service Control
// Manager's *stop* request (systemd's SIGTERM) raises the server's stop
// signal, so invocations are cancelled, the Things stop in reverse order,
// and their devices close. Meanwhile the service reports *stop pending*
// with a wait hint that covers the grace period.
//
// `install` writes the service's configuration next to the executable, with
// absolute paths (a Windows service starts in `C:\Windows\System32`); the
// Windows service logs to `microscope-service.log` there, and the systemd
// one to the journal. `service_test.ps1` (`service_test.sh`) installs,
// starts, uses, stops and removes it. The C++ port of teta-wot's
// `microscope-service`, which is Windows-only.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <system_error>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

#include "../common/http_client.hpp"
#include "microscope.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tw = teta::wot;
using namespace std::chrono_literals;

namespace {

/// The service's name, as `sc`, `Start-Service` and `systemctl` know it.
constexpr const char* service_name = "teta-wot-microscope";

/// The grace period for requests and invocations when stopping.
constexpr auto grace = 5s;

/// What the Service Control Manager (systemd) is told stopping may take: the
/// grace period, and time to stop the Things.
constexpr auto stop_hint = 20s;

const char* const description =
    "A simulated stage, camera and autofocus, served as W3C Web of Things Things (the teta-wot-cpp "
    "microscope-service example).";

void customise(tw::ThingServerBuilder& builder) {
    builder.shutdown_grace(grace);
}

/// UTF-8 text as a path, and back.
std::filesystem::path path_of(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string text_of(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// The server's command line.
std::vector<std::string> cli_args(std::vector<std::string> args) {
    args.insert(args.begin(), "microscope-service");
    return args;
}

/// Takes `OPTION VALUE` out of `args`, and returns the value, if it is there.
std::optional<std::string> take(std::vector<std::string>& args, const std::string& option) {
    const auto at = std::find(args.begin(), args.end(), option);
    if (at == args.end()) {
        return std::nullopt;
    }
    if (at + 1 == args.end()) {
        throw std::runtime_error(option + " needs a value");
    }
    std::string value = *(at + 1);
    args.erase(at, at + 2);
    return value;
}

/// This executable, absolute.
std::filesystem::path current_executable() {
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "the executable");
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}

/// The microscope's configuration, with an absolute settings folder in
/// `folder`, written there as `microscope-service.json`.
std::filesystem::path write_config(const std::filesystem::path& folder) {
    auto config = tw::json::parse(microscope::config);
    config["settings_folder"] = text_of(folder / "settings" / "microscope-service");
    const auto path = folder / "microscope-service.json";
    std::ofstream(path, std::ios::binary) << tw::json::dump_pretty(config);
    return path;
}

/// Sends standard error (where the log goes) and standard output to `file`.
bool log_to(const std::string& file) {
    // A stream whose reopening fails is closed, and writing to a closed
    // stream ends the process: it is sent to the null device instead.
    const auto reopen = [path = path_of(file)](FILE* stream) {
#ifdef _WIN32
    // Not `_wfreopen_s`, which opens the file without sharing: the
    // second stream couldn't open it.
#pragma warning(suppress : 4996)
        if (_wfreopen(path.c_str(), L"a", stream)) {
            return true;
        }
#pragma warning(suppress : 4996)
        (void)_wfreopen(L"NUL", L"w", stream);
#else
        if (std::freopen(path.c_str(), "a", stream)) {
            return true;
        }
        (void)std::freopen("/dev/null", "w", stream);
#endif
        return false;
    };
    const bool reopened = reopen(stderr) && reopen(stdout);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return reopened;
}

#ifndef _WIN32
const std::filesystem::path unit_path =
    std::filesystem::path("/etc/systemd/system") / (std::string(service_name) + ".service");

void systemctl(const std::string& arguments, bool check) {
    const std::string command = "systemctl " + arguments;
    if (std::system(command.c_str()) != 0 && check) {
        throw std::runtime_error("`" + command + "` failed");
    }
}
#endif

/// What the Service Control Manager (or systemd) starts.
int run(std::vector<std::string> args) {
    // A Windows service has no console: it logs to a file.
    if (const auto file = take(args, "--log")) {
        if (!log_to(*file)) {
            std::cerr << "can't log to " << *file << "\n";
        }
    }
    tw::log::init(false);
    const auto cli = cli_args(args);
    tw::log::info("starting as the service `{}`", service_name);
#ifdef _WIN32
    try {
        tw::service::run_as_service(
            service_name, stop_hint, [cli](std::vector<std::string>, std::stop_token stop) {
                return tw::cli::serve_until(cli, microscope::registry(), customise, stop);
            });
        return EXIT_SUCCESS;
    } catch (const tw::service::NotAService&) {
        std::cerr << "`run` is for the Service Control Manager: use `console` to run in a console\n";
        return 2;
    } catch (const std::exception& error) {
        tw::log::error("{}", error.what());
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
#else
    // systemd's stop is SIGTERM, which stops the server as Ctrl-C does; the
    // server tells systemd when it is ready (`Type=notify`).
    return tw::cli::serve_until(cli, microscope::registry(), customise, tw::shutdown_signal());
#endif
}

/// The same server in a console, until Ctrl-C.
int console(const std::vector<std::string>& args) {
    return tw::cli::serve_until(cli_args(args), microscope::registry(), customise, tw::shutdown_signal());
}

/// Registers the service, with its configuration next to the executable.
void install(std::vector<std::string> args) {
    const auto port = take(args, "--port").value_or("5000");
    if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos ||
        std::stoul(port) > 65535) {
        throw std::runtime_error("--port needs a number");
    }
    const auto executable = current_executable();
    const auto folder = executable.parent_path();
    const auto config = write_config(folder);
#ifdef _WIN32
    const auto log = folder / "microscope-service.log";
    tw::service::install({service_name,
                          "teta-wot-cpp simulated microscope",
                          description,
                          executable,
                          {"run", "-c", text_of(config), "--port", port, "--log", text_of(log)},
                          false});
    std::cout << "installed the service `" << service_name << "`: " << text_of(executable) << "\n"
              << "  start it with `Start-Service " << service_name
              << "`, and it serves http://127.0.0.1:" << port << "/\n"
              << "  it logs to " << text_of(log) << "\n";
#else
    tw::systemd::UnitOptions unit;
    unit.description = description;
    unit.executable = executable;
    unit.arguments = {"run", "-c", text_of(config), "--port", port};
    unit.user = take(args, "--user");
    unit.stop_timeout = stop_hint;
    std::ofstream(unit_path, std::ios::binary) << tw::systemd::unit_file(unit);
    systemctl("daemon-reload", true);
    std::cout << "installed the service `" << service_name << "`: " << unit_path.string() << "\n"
              << "  start it with `systemctl start " << service_name
              << "`, and it serves http://127.0.0.1:" << port << "/\n"
              << "  it logs to the journal: `journalctl -u " << service_name << "`\n";
#endif
}

/// Stops the service if it is running, and removes it.
void uninstall() {
#ifdef _WIN32
    tw::service::uninstall(service_name);
#else
    systemctl(std::string("stop ") + service_name, false);
    if (!std::filesystem::remove(unit_path)) {
        throw std::runtime_error("there is no " + unit_path.string());
    }
    systemctl("daemon-reload", true);
#endif
    std::cout << "removed the service `" << service_name << "`\n";
}

/// Without the Service Control Manager (and without an administrator): the
/// server as the service runs it, stopped through the same stop signal that
/// the SCM's *stop* request raises.
void demonstrate() {
    const auto folder =
        std::filesystem::temp_directory_path() / ("wot-service-demo-" + tw::Uuid::v4().to_string());
    std::filesystem::create_directories(folder);
    const auto config = write_config(folder);
    const auto port = examples::http::free_port();
    const auto cli = cli_args({"-c", text_of(config), "--port", std::to_string(port)});
    std::stop_source stop;
    auto serving = std::async(std::launch::async, [&] {
        return tw::cli::serve_until(cli, microscope::registry(), customise, stop.get_token());
    });

    const auto deadline = std::chrono::steady_clock::now() + 30s;
    for (;;) {
        try {
            examples::http::request(port, "GET", "/stage/");
            break;
        } catch (const std::runtime_error&) {
            if (std::chrono::steady_clock::now() > deadline ||
                serving.wait_for(0s) == std::future_status::ready) {
                stop.request_stop();
                throw std::runtime_error("the server didn't start");
            }
            std::this_thread::sleep_for(100ms);
        }
    }
    std::cout << "serving the microscope on http://127.0.0.1:" << port << "/, as the service would\n";
    const auto invocation =
        examples::http::request(port, "POST", "/stage/move_to", R"({"x": 100000, "y": 0, "z": 0})");
    std::cout << "started a long move: " << invocation.head.substr(0, invocation.head.find("\r\n")) << "\n";

    std::cout << "stopping, as the Service Control Manager's stop request does\n";
    const auto asked = std::chrono::steady_clock::now();
    stop.request_stop();
    if (serving.wait_for(grace + 10s) != std::future_status::ready) {
        throw std::runtime_error("the server didn't stop");
    }
    const int code = serving.get();
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - asked;
    std::cout << std::format("stopped in {:.1f} s, with exit code {}\n", took.count(), code);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    if (code != 0) {
        throw std::runtime_error("the server didn't stop cleanly");
    }
    if (took >= grace) {
        throw std::runtime_error("the long move was waited for, not cancelled");
    }
    const auto program = text_of(current_executable().filename());
#ifdef _WIN32
    std::cout << "\nAs a service: `" << program << " install`, then `Start-Service " << service_name
              << "` (administrator).\n";
#else
    std::cout << "\nAs a service: `sudo " << program << " install`, then `sudo systemctl start "
              << service_name << "`.\n";
#endif
}

}  // namespace

int main(int argc, char** argv) {
    const auto all = tw::cli::arguments(argc, argv);
    std::vector<std::string> args(all.begin() + (all.empty() ? 0 : 1), all.end());
    try {
        if (args.empty()) {
            demonstrate();
            return EXIT_SUCCESS;
        }
        const std::string command = args.front();
        args.erase(args.begin());
        if (command == "install") {
            install(args);
        } else if (command == "uninstall") {
            uninstall();
        } else if (command == "run") {
            return run(args);
        } else if (command == "console") {
            return console(args);
        } else {
            throw std::runtime_error("unknown command `" + command +
                                     "`: use install, uninstall, run, console, or nothing");
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
