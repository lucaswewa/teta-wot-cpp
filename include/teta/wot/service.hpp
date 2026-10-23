// Running as a Windows service: a port of teta-wot-server's `service.rs`.
//
// `run_as_service` connects the process to the Service Control Manager (SCM)
// and runs the server there: the SCM's *stop* and *shutdown* requests become
// the server's stop signal, so a service stops the way Ctrl-C stops a
// console server:
//
// 1. the SCM starts the process; it reports *start pending*, then *running*
//    once `serve` is called;
// 2. on *stop* or *shutdown*, it reports *stop pending*, with a wait hint
//    that covers the grace period, and the stop signal is raised;
// 3. when `serve` returns, it reports *stopped*, with its exit code.
//
// If the SCM didn't start the process, `run_as_service` throws `NotAService`
// at once, so the same binary can run in a console. `install` and
// `uninstall` register the binary with the SCM, which needs an
// administrator.
//
// On other platforms `run_as_service` throws `NotAService` and the rest
// throws `ServiceError`: a Linux server runs under systemd instead
// (`systemd.hpp`).

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace teta::wot::service {

/// Running as a service, or registering one, failed: for example, the SCM
/// refused, because installing needs an administrator.
class ServiceError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// The process wasn't started by the Service Control Manager: run it in a
/// console instead.
class NotAService : public ServiceError {
  public:
    NotAService() : ServiceError("the process wasn't started by the Service Control Manager") {}
};

/// `run_as_service` was called a second time: Windows takes one service
/// dispatcher per process, even after an attempt that threw `NotAService`.
class AlreadyRunning : public ServiceError {
  public:
    AlreadyRunning() : ServiceError("the process has already run, or tried to run, as a service") {}
};

/// What the service runs: given the service's start arguments (UTF-8) and its
/// stop signal, it serves until the signal, and returns the exit code.
using Serve = std::function<int(std::vector<std::string> arguments, std::stop_token stop)>;

/// Runs the process as the Windows service `name`, calling `serve` on the
/// SCM's thread. `stop_hint` tells the SCM how long stopping may take: the
/// shutdown grace period, plus the time to stop the Things.
///
/// Returns once the service has stopped. Throws `NotAService` at once if the
/// SCM didn't start the process, `AlreadyRunning` if it was called before in
/// this process, or `ServiceError`.
///
///     const auto args = tw::cli::arguments(argc, argv);
///     try {
///         tw::service::run_as_service("microscope", 20s, [&](auto, std::stop_token stop) {
///             return tw::cli::serve_until(args, registry(), {}, stop);
///         });
///     } catch (const tw::service::NotAService&) {
///         return 2;  // run in a console instead
///     }
void run_as_service(const std::string& name, std::chrono::nanoseconds stop_hint, Serve serve);

/// A service to register with the SCM.
struct InstallOptions {
    /// The service's name, as `sc` and `Start-Service` know it.
    std::string name;
    /// The name shown in the Services console.
    std::string display_name;
    /// The description shown in the Services console.
    std::string description;
    /// The executable, which must call `run_as_service` with `name`.
    std::filesystem::path executable;
    /// The arguments the SCM starts it with.
    std::vector<std::string> arguments;
    /// Whether it starts with Windows (otherwise on demand).
    bool automatic = false;
};

/// Registers a service with the SCM, running as the LocalSystem account.
/// Needs an administrator. Throws ServiceError.
void install(const InstallOptions& options);

/// Stops a service if it is running, and removes it from the SCM (once it
/// has stopped). Needs an administrator. Throws ServiceError.
void uninstall(const std::string& name);

/// The command line the SCM starts: the executable in quotes, then each
/// argument quoted as `CommandLineToArgvW` and the C runtime read it back.
std::string command_line(const std::filesystem::path& executable, const std::vector<std::string>& arguments);

}  // namespace teta::wot::service
