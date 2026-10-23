// Running under systemd, the Linux counterpart of the Windows service
// (`service.hpp`); teta-wot has none.
//
// A `Type=notify` unit tells systemd when the server is ready: `ThingServer`
// sends `READY=1` once it listens, and `STOPPING=1` when it starts shutting
// down, if systemd started the process with a notification socket
// (`$NOTIFY_SOCKET`). systemd's *stop* is SIGTERM, which stops a server as
// Ctrl-C does (`shutdown_signal`). There is no libsystemd dependency: the
// protocol is a datagram on a Unix socket.

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace teta::wot::systemd {

/// Sends `state` (`READY=1`, `STOPPING=1`, `STATUS=...`, one per line) to
/// systemd, as `sd_notify` does. Returns whether it was sent: false if the
/// process has no notification socket (it wasn't started by systemd with
/// one), on Windows, or if sending failed.
bool notify(std::string_view state);

/// `notify("READY=1")`: the service has started.
bool notify_ready();

/// A `Type=notify` service unit.
struct UnitOptions {
    /// `Description=`.
    std::string description;
    /// The executable, absolute.
    std::filesystem::path executable;
    /// Its arguments.
    std::vector<std::string> arguments;
    /// The user it runs as (`User=`), or root.
    std::optional<std::string> user;
    /// How long systemd waits for it to stop before killing it
    /// (`TimeoutStopSec=`): the grace period, plus the time to stop the
    /// Things.
    std::chrono::seconds stop_timeout{20};
};

/// The unit file's text: started after the network is up, `Type=notify`,
/// wanted by `multi-user.target`.
std::string unit_file(const UnitOptions& options);

/// One word of an `ExecStart=` line, quoted and escaped as systemd reads it
/// back (`%` as `%%`, `$` as `$$`).
std::string exec_word(std::string_view word);

namespace detail {

/// The bytes of `$NOTIFY_SOCKET`'s address as `sockaddr_un::sun_path` takes
/// them, which is also the address's length: a path with its terminating NUL,
/// or an abstract socket (`@name`) with the `@` as a NUL. `nullopt` for
/// anything else, or for an address longer than `sun_path` (108 bytes).
std::optional<std::string> notify_address(std::string_view variable);

}  // namespace detail

}  // namespace teta::wot::systemd
