// The command line of a Thing server: a port of teta-wot-server's `cli`.
//
//     my-server -c config.json [--host 127.0.0.1] [--port 5000] [--fallback] [--debug]
//     my-server -j '{"things": {...}}'
//
// An application calls `serve_from_cli` from its `main`, with the registry of
// the Thing types its configuration files may name:
//
//     int main(int argc, char** argv) {
//         tw::ThingRegistry registry;
//         registry.add<Counter>("my_lab.counters:Counter");
//         return tw::cli::serve_from_cli(argc, argv, registry);
//     }
//
// Exit codes:
//
// | Code | When |
// |------|------|
// | 0    | The server stopped normally (or `--help`) |
// | 2    | The command line is wrong |
// | 3    | The configuration is invalid, a Thing failed to start, or the fallback server was served |
// | 1    | Anything else (no configuration, a missing file, a Thing that can't be built, a port in use) |
//
// With `--fallback`, a server that can't start serves an error page instead
// of exiting (`serve_fallback`), until it is asked to stop.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "teta/wot/registry.hpp"
#include "teta/wot/thing_server.hpp"

namespace teta::wot::cli {

/// The command-line options.
struct CliArgs {
    /// Path to the configuration file (`-c`, `--config`).
    std::optional<std::string> config;
    /// The configuration as JSON text (`-j`, `--json`).
    std::optional<std::string> json;
    /// Serve an error page instead of exiting, if the server can't start.
    bool fallback = false;
    /// The host to listen on.
    std::string host = "127.0.0.1";
    /// The port to listen on; 0 picks a free one.
    std::uint16_t port = 5000;
    /// Debug logging.
    bool debug = false;
};

/// Changes the server builder before it builds, for example to register
/// services.
using Customise = std::function<void(ThingServerBuilder&)>;

/// The process's arguments, the program's name first, as UTF-8: on Windows,
/// from the wide command line (`argv` is in the ANSI code page there).
std::vector<std::string> arguments(int argc, char** argv);

/// Parses the process's arguments (`arguments`) and serves until the
/// process is asked to stop (`shutdown_signal`). Returns the exit code.
int serve_from_cli(int argc, char** argv, const ThingRegistry& registry, const Customise& customise = {});

/// `serve_from_cli` with explicit arguments, the first being the program's
/// name.
int serve_from_args(const std::vector<std::string>& args, const ThingRegistry& registry,
                    const Customise& customise = {});

/// `serve_from_args`, stopping when `stop` is requested rather than when the
/// process is asked to: for tests, examples and services.
int serve_until(const std::vector<std::string>& args, const ThingRegistry& registry,
                const Customise& customise, std::stop_token stop);

}  // namespace teta::wot::cli
