// The server: Things, the HTTP binding, and their lifecycle. A port of
// teta-wot-server's `ThingServer`.
//
// `ThingServer::serve`:
//
// 1. loads the Things' settings, then starts the Things in order (the Things
//    a Thing's slots connect to first): their devices open, then their start
//    hooks run. A failure stops what has started and throws StartupError;
// 2. serves on Drogon until the stop token is requested (by default, until
//    the process is asked to stop: `shutdown_signal`);
// 3. shuts down: refuses new connections and lets requests in progress
//    finish, ends observation streams, cancels unfinished invocations, waits
//    up to the grace period for both, then stops the Things in reverse order
//    and closes their devices.
//
// `ThingServer::from_config` builds a server from a configuration file
// (config.hpp) with the Thing types of a registry (registry.hpp); cli.hpp
// adds teta-wot's command line, and `serve_fallback` its error page.
//
// Drogon's application is one per process, and runs once: a process can
// `serve` (or `serve_fallback`) once. `TestClient` (teta/wot/testing.hpp)
// needs no server.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/config.hpp"
#include "teta/wot/http/binding.hpp"
#include "teta/wot/http/fallback.hpp"
#include "teta/wot/registry.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot {

/// The default time to wait for requests and invocations on shutdown.
inline constexpr std::chrono::seconds default_shutdown_grace{5};

/// The DNS-SD service type of WoT Things and directories.
inline constexpr std::string_view wot_service_type = "_wot._tcp.local.";
/// The DNS-SD service type of a Thing Description Directory: `_wot._tcp`
/// with the `_directory` subtype.
inline constexpr std::string_view directory_service_type = "_directory._sub._wot._tcp.local.";
/// Whether servers can advertise themselves (the CMake option
/// TETA_WOT_MDNS).
bool mdns_supported();

/// Serving failed (the server couldn't listen, or Drogon failed).
class ServeError : public std::runtime_error {
  public:
    explicit ServeError(const std::string& message) : std::runtime_error("the server failed: " + message) {}
};

namespace detail {

/// Where a server listens: the IP address its host names, and the address
/// as text (`127.0.0.1:5000`, `[::1]:5000`).
struct ListenAddress {
    std::string ip;
    std::uint16_t port = 0;
    std::string text;
};

/// Checks that `host`:`port` can be listened on, before Drogon (which ends
/// the process when it can't) tries: resolves the host, then binds a socket
/// and closes it. Throws std::runtime_error with teta-wot's message (`can't
/// resolve ...`, `can't listen on ...`).
ListenAddress check_listen(const std::string& host, std::uint16_t port);

}  // namespace detail

class ThingServer;

/// Builds a ThingServer.
class ThingServerBuilder {
  public:
    /// Adds a Thing under `name`.
    template <ThingType T>
    ThingServerBuilder& thing(std::string name, std::shared_ptr<T> thing) {
        runtime_.thing(std::move(name), std::move(thing));
        return *this;
    }
    /// Adds a default-constructed Thing.
    template <ThingType T>
        requires std::default_initializable<T>
    ThingServerBuilder& thing(std::string name) {
        runtime_.thing<T>(std::move(name));
        return *this;
    }
    /// Adds a Thing built from its configuration (its `kwargs`), read into
    /// `T::Config` when the server is built.
    template <FromConfig T>
    ThingServerBuilder& thing_from_config(std::string name, const json::Value& kwargs) {
        runtime_.thing_from_config<T>(std::move(name), kwargs);
        return *this;
    }
    /// Adds a Thing built from a configuration file entry's `args` and
    /// `kwargs` (see RuntimeBuilder::thing_from_args).
    template <FromConfig T>
    ThingServerBuilder& thing_from_args(std::string name, std::vector<json::Value> args, json::Value kwargs) {
        runtime_.thing_from_args<T>(std::move(name), std::move(args), std::move(kwargs));
        return *this;
    }
    /// Overrides where a Thing's slots connect (the configuration's
    /// `thing_slots`).
    ThingServerBuilder& thing_slots(const std::string& thing,
                                    std::vector<std::pair<std::string, SlotSelection>> slots) {
        runtime_.thing_slots(thing, std::move(slots));
        return *this;
    }
    /// Registers a shared service, for `Dep<S>` action parameters.
    template <typename S>
    ThingServerBuilder& service(std::shared_ptr<S> service) {
        runtime_.service(std::move(service));
        return *this;
    }
    /// The application configuration that Things can read.
    ThingServerBuilder& application_config(json::Value config) {
        runtime_.application_config(std::move(config));
        return *this;
    }
    /// Persists settings in `folder`: each Thing gets `{folder}/{name}/`.
    ThingServerBuilder& settings_folder(std::filesystem::path folder) {
        runtime_.settings_folder(std::move(folder));
        return *this;
    }
    /// The level of the "Global lock was busy" log line.
    ThingServerBuilder& global_lock_log_level(log::Level level) {
        runtime_.global_lock_log_level(level);
        return *this;
    }
    /// Enables the global lock (off by default).
    ThingServerBuilder& global_lock(bool enabled) {
        runtime_.global_lock(enabled);
        return *this;
    }
    /// Serves every route under a prefix such as `/api/v1`.
    ThingServerBuilder& api_prefix(std::string prefix) {
        http_.api_prefix = std::move(prefix);
        return *this;
    }
    /// The server ID in TD `id`s (by default the computer's name).
    ThingServerBuilder& server_id(std::string id) {
        http_.server_id = std::move(id);
        return *this;
    }
    /// The API's title and version, in the OpenAPI document and the docs
    /// pages (`teta-wot` and `0.1.0` by default).
    ThingServerBuilder& api_info(std::string title, std::string version) {
        http_.api_title = std::move(title);
        http_.api_version = std::move(version);
        return *this;
    }
    /// How the server behaves where teta-wot and the W3C differ.
    ThingServerBuilder& wire_profile(http::WireProfile profile) {
        http_.profile = profile;
        return *this;
    }
    /// Requires credentials for every interaction (TDs, discovery, the OpenAPI
    /// document and the docs stay public), and describes them in the TDs.
    ThingServerBuilder& security(http::Security security) {
        http_.security = std::move(security);
        return *this;
    }
    /// How long shutdown waits for requests and invocations (5 s by default).
    ThingServerBuilder& shutdown_grace(std::chrono::nanoseconds grace) {
        grace_ = grace;
        return *this;
    }
    /// Advertises the server with DNS-SD over mDNS, as a Thing Description
    /// Directory (`directory_service_type`) named after the server ID, once it
    /// listens. Needs the CMake option TETA_WOT_MDNS; without it, a warning is
    /// logged when the server starts.
    ThingServerBuilder& mdns(bool enabled) {
        mdns_ = enabled;
        return *this;
    }
    /// The runtime's builder, for its other options (lock timeout, worker
    /// cap, clock).
    RuntimeBuilder& runtime() { return runtime_; }

    /// Builds the Things and the routes. Throws BuildError or
    /// http::RouteError.
    ThingServer build();

  private:
    RuntimeBuilder runtime_;
    http::HttpOptions http_;
    std::chrono::nanoseconds grace_ = default_shutdown_grace;
    bool mdns_ = false;
};

/// Things served over HTTP.
class ThingServer {
  public:
    static ThingServerBuilder builder() { return {}; }
    /// Starts building a server from a configuration: its Things (built by
    /// the registry from their `cls`, `args` and `kwargs`, with their
    /// `thing_slots`), settings folder (`./settings` by default), API prefix,
    /// global lock, application configuration, server ID and wire profile.
    /// The builder can still be changed, for example to add services, before
    /// `build`. Throws ConfigError, for example for a class the registry
    /// doesn't have.
    static ThingServerBuilder from_config(const ServerConfig& config, const ThingRegistry& registry);

    /// The runtime: Things, invocations, broker, lock.
    const std::shared_ptr<Runtime>& runtime() const { return runtime_; }
    /// The HTTP binding.
    const std::shared_ptr<http::HttpBinding>& binding() const { return binding_; }
    std::chrono::nanoseconds shutdown_grace() const { return grace_; }

    /// Starts the Things, serves on `host`:`port` (0 for any free port) until
    /// `stop` is requested, then shuts down (see above). `listening` is told
    /// the port once the server listens. Throws StartupError or ServeError.
    void serve(const std::string& host, std::uint16_t port, std::stop_token stop,
               std::function<void(std::uint16_t port)> listening = {});
    /// `serve`, until the process is asked to stop (`shutdown_signal`).
    void serve(const std::string& host, std::uint16_t port);

  private:
    friend class ThingServerBuilder;
    ThingServer(std::shared_ptr<Runtime> runtime, std::shared_ptr<http::HttpBinding> binding,
                std::chrono::nanoseconds grace, std::optional<std::string> advertise)
        : runtime_(std::move(runtime)),
          binding_(std::move(binding)),
          grace_(grace),
          advertise_(std::move(advertise)) {}

    std::shared_ptr<Runtime> runtime_;
    std::shared_ptr<http::HttpBinding> binding_;
    std::chrono::nanoseconds grace_;
    /// The DNS-SD instance name to advertise, if any.
    std::optional<std::string> advertise_;
};

/// Serves the fallback server's `page` on `host`:`port` until `stop` is
/// requested: what the command line serves with `--fallback` when the server
/// can't start. `listening` is told the port once it listens. Throws
/// ServeError.
void serve_fallback(const http::FallbackPage& page, const std::string& host, std::uint16_t port,
                    std::stop_token stop, std::function<void(std::uint16_t port)> listening = {});

/// A token that is stopped when the process is asked to stop: Ctrl-C,
/// Ctrl-Break, the console window closing, logging off or shutting down on
/// Windows; SIGINT or SIGTERM elsewhere. The handlers are installed on the
/// first call.
std::stop_token shutdown_signal();

}  // namespace teta::wot
