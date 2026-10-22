// The server as a Thing sees it: a port of teta-wot-core's `server`.
//
// A Server handle reaches the other Things, the shared services, the
// application configuration and the settings folder. It is cheap to copy.
// Actions get it from `ActionCtx::server`, lifecycle hooks from
// `ThingCtx::server`, and an action parameter of type `Server` or `Dep<S>`
// receives it (or one service) directly.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "teta/wot/codec.hpp"
#include "teta/wot/json.hpp"

namespace teta::wot {

class ThingHandle;
template <typename T>
class ThingRef;

/// A handle to the server that runs a Thing.
class Server {
  public:
    /// A server with no Things, services or configuration, for running Thing
    /// code in tests.
    static Server detached();

    /// The names of the Things on the server, in configuration order.
    std::vector<std::string> thing_names() const;
    /// The Thing named `name`, if it is a `T` (defined in runtime.hpp).
    template <typename T>
    std::optional<ThingRef<T>> thing_ref(std::string_view name) const;
    /// Every Thing that is a `T` (defined in runtime.hpp).
    template <typename T>
    std::vector<ThingRef<T>> things_of_type() const;
    /// A shared service registered with the server builder.
    template <typename S>
    std::shared_ptr<S> service() const {
        const auto it = shared_->services.find(std::type_index(typeid(S)));
        if (it == shared_->services.end()) {
            return nullptr;
        }
        return std::static_pointer_cast<S>(it->second.value);
    }
    /// The `application_config` of the configuration file.
    std::optional<json::Value> application_config() const { return shared_->application_config; }
    /// What each Thing's `thing_state` returns, or `{}`, by name.
    OrderedMap<json::Value> thing_states() const;
    /// The settings folder, if settings are persisted.
    const std::optional<std::filesystem::path>& settings_folder() const { return shared_->settings_folder; }

    /// What the Things of one runtime share.
    struct Service {
        std::shared_ptr<void> value;
        std::string type_name;
    };
    struct Shared {
        /// Filled in once every Thing is built. Weak: the runtime alone keeps
        /// the Things alive.
        std::vector<std::pair<std::string, std::weak_ptr<ThingHandle>>> things;
        std::unordered_map<std::type_index, Service> services;
        std::optional<json::Value> application_config;
        std::optional<std::filesystem::path> settings_folder;
    };
    explicit Server(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    Shared& shared() const { return *shared_; }

  private:
    std::vector<std::pair<std::string, std::shared_ptr<ThingHandle>>> handles() const;
    std::shared_ptr<Shared> shared_;
};

/// A shared service, given to an action parameter of type `Dep<S>`.
/// Services are registered once, with the server builder, and live as long
/// as the server.
template <typename S>
class Dep {
  public:
    using service_type = S;
    explicit Dep(std::shared_ptr<S> service) : service_(std::move(service)) {}
    S& operator*() const { return *service_; }
    S* operator->() const { return service_.get(); }
    const std::shared_ptr<S>& get() const { return service_; }

  private:
    std::shared_ptr<S> service_;
};

}  // namespace teta::wot
