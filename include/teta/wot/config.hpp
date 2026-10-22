// The server configuration file: a port of teta-wot-server's `config`.
//
//     {
//         "things": {
//             "counter": "twins.counter:TestThing",
//             "stage": {"class": "sangaboard:Stage", "kwargs": {"port": "COM3"}},
//             "autofocus": {"cls": "autofocus:Autofocus", "thing_slots": {"camera": "cam2"}}
//         },
//         "settings_folder": "./settings",
//         "api_prefix": "/api/v1",
//         "enable_global_lock": true,
//         "global_lock_log_level": "WARNING",
//         "application_config": {"lab": "B12"}
//     }
//
// The file is read as LabThings reads it (pydantic's `ThingServerConfig`),
// and every problem is reported at once, located as pydantic locates it.
// teta-wot's own keys are `server_id`, `wire_profile`, `security` and `mdns`.

#pragma once

#include <array>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/http/binding.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/log.hpp"
#include "teta/wot/slots.hpp"

namespace teta::wot {

/// Thing names a configuration file may not use.
inline constexpr std::array<std::string_view, 2> reserved_config_thing_names = {"things", "cls"};

/// A configuration that can't be used: every problem, located as pydantic
/// locates them. Its text is pydantic's:
///
///     2 validation errors for ThingServerConfig
///     things.stage.kwargs
///       Input should be a valid dictionary
///     ...
class ConfigError : public std::exception {
  public:
    explicit ConfigError(std::vector<std::pair<std::string, std::string>> errors);

    /// `(location, message)` pairs, such as `("things.stage.kwargs", "Input
    /// should be a valid dictionary")`. The location may be empty.
    const std::vector<std::pair<std::string, std::string>>& errors() const { return errors_; }
    const char* what() const noexcept override { return text_.c_str(); }

  private:
    std::vector<std::pair<std::string, std::string>> errors_;
    std::string text_;
};

/// One Thing of a configuration.
struct ThingConfig {
    /// The Thing type, as registered (`ThingRegistry`).
    std::string cls;
    /// Positional arguments.
    std::vector<json::Value> args;
    /// Keyword arguments: an object.
    json::Value kwargs = json::Value::object();
    /// Where its slots connect, by slot name.
    std::vector<std::pair<std::string, SlotSelection>> thing_slots;

    bool operator==(const ThingConfig&) const = default;
};

/// The `security` key: the scheme, and the environment variable holding its
/// secret (never the secret itself, as configuration files are often shared).
///
///     {"scheme": "basic", "username": "lab", "password_env": "WOT_PASSWORD"}
///     {"scheme": "bearer", "token_env": "WOT_TOKEN"}
struct SecurityConfig {
    enum class Scheme { basic, bearer };
    Scheme scheme = Scheme::basic;
    /// For `basic`.
    std::string username;
    std::string password_env;
    /// For `bearer`.
    std::string token_env;

    /// The secret, read from its environment variable. Throws ConfigError if
    /// the variable isn't set (or is empty).
    std::string secret() const;

    bool operator==(const SecurityConfig&) const = default;
};

/// A server configuration.
struct ServerConfig {
    /// The Things, by name, in the file's order (which is the start order,
    /// apart from slot dependencies).
    std::vector<std::pair<std::string, ThingConfig>> things;
    /// Where settings are kept (`./settings` by default, which
    /// `settings_folder_or_default` applies).
    std::optional<std::string> settings_folder;
    /// A prefix for every route, such as `/api/v1`.
    std::string api_prefix;
    /// Whether actions and property writes take the global lock.
    bool enable_global_lock = false;
    /// The level of the "Global lock was busy" log line.
    log::Level global_lock_log_level = log::Level::info;
    /// Anything the application wants Things to read (an object).
    std::optional<json::Value> application_config;
    /// teta-wot: identifies the server in TD `id`s.
    std::optional<std::string> server_id;
    /// teta-wot: the wire profile.
    std::optional<http::WireProfile> wire_profile;
    /// teta-wot: the credentials interactions require.
    std::optional<SecurityConfig> security;
    /// teta-wot: whether to advertise the server with DNS-SD over mDNS.
    bool mdns = false;
    /// Keys that were ignored.
    std::vector<std::string> ignored_keys;

    /// Reads a configuration from JSON text. Throws ConfigError.
    static ServerConfig from_json(std::string_view text);
    /// Reads a configuration from a JSON value. Throws ConfigError.
    static ServerConfig from_value(const json::Value& value);

    /// The settings folder, `./settings` by default.
    std::string settings_folder_or_default() const { return settings_folder.value_or("./settings"); }
};

}  // namespace teta::wot
