// The server: Things, the HTTP binding, and their lifecycle. A port of
// teta-wot-server's `ThingServer` (Phase 4: building and serving; the
// configuration model, registry and command line are Phase 5).
//
// `ThingServer::serve`:
//
// 1. starts the Things in order: their devices open, then their start hooks
//    run. A failure stops what has started and throws StartupError;
// 2. serves on Drogon until the stop token is requested (by default, until
//    the process is asked to stop: `shutdown_signal`);
// 3. shuts down: refuses new connections and lets requests in progress
//    finish, ends observation streams, cancels unfinished invocations, waits
//    up to the grace period for both, then stops the Things in reverse order
//    and closes their devices.
//
// Drogon's application is one per process, and runs once: a process can
// `serve` once. `TestClient` (teta/wot/testing.hpp) needs no server.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

#include "teta/wot/http/binding.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot {

/// The default time to wait for requests and invocations on shutdown.
inline constexpr std::chrono::seconds default_shutdown_grace{5};

/// Serving failed (the server couldn't listen, or Drogon failed).
class ServeError : public std::runtime_error {
  public:
    explicit ServeError(const std::string& message) : std::runtime_error("the server failed: " + message) {}
};

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
    /// Persists settings in `folder` (Phase 5).
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
    /// How long shutdown waits for requests and invocations (5 s by default).
    ThingServerBuilder& shutdown_grace(std::chrono::nanoseconds grace) {
        grace_ = grace;
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
};

/// Things served over HTTP.
class ThingServer {
  public:
    static ThingServerBuilder builder() { return {}; }

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
                std::chrono::nanoseconds grace)
        : runtime_(std::move(runtime)), binding_(std::move(binding)), grace_(grace) {}

    std::shared_ptr<Runtime> runtime_;
    std::shared_ptr<http::HttpBinding> binding_;
    std::chrono::nanoseconds grace_;
};

/// A token that is stopped when the process is asked to stop: Ctrl-C,
/// Ctrl-Break, the console window closing, logging off or shutting down on
/// Windows; SIGINT or SIGTERM elsewhere. The handlers are installed on the
/// first call.
std::stop_token shutdown_signal();

}  // namespace teta::wot
