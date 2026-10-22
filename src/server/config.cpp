#include "teta/wot/config.hpp"

#include <algorithm>
#include <cstdlib>

namespace teta::wot {

namespace {

using Errors = std::vector<std::pair<std::string, std::string>>;

std::string config_text(const Errors& errors) {
    const auto count = errors.size();
    std::string text =
        std::to_string(count) + " validation error" + (count == 1 ? "" : "s") + " for ThingServerConfig";
    for (const auto& [location, message] : errors) {
        if (location.empty()) {
            text += "\n  " + message;
        } else {
            text += "\n" + location + "\n  " + message;
        }
    }
    return text;
}

// pydantic's rule for Thing names, `^[a-zA-Z0-9\-_]+$`, and the reserved
// names.
std::optional<std::string> thing_name_problem(std::string_view name) {
    if (name.empty()) {
        return "String should have at least 1 character";
    }
    const bool usable = std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
               c == '_';
    });
    if (!usable) {
        return "String should match pattern '^([a-zA-Z0-9\\-_]+)$'";
    }
    if (std::ranges::find(reserved_config_thing_names, name) != reserved_config_thing_names.end()) {
        return "Value error, " + std::string(name) + " is not allowed as the name for a Thing.";
    }
    return std::nullopt;
}

// pydantic's lax booleans from JSON.
std::optional<bool> lax_bool(const json::Value& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        if (value.is_number_unsigned() ? value.get<std::uint64_t>() == 0 : value.get<std::int64_t>() == 0) {
            return false;
        }
        if (value.is_number_unsigned() ? value.get<std::uint64_t>() == 1 : value.get<std::int64_t>() == 1) {
            return true;
        }
        return std::nullopt;
    }
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        std::ranges::transform(text, text.begin(), [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        });
        for (std::string_view no : {"0", "off", "f", "false", "n", "no"}) {
            if (text == no) {
                return false;
            }
        }
        for (std::string_view yes : {"1", "on", "t", "true", "y", "yes"}) {
            if (text == yes) {
                return true;
            }
        }
    }
    return std::nullopt;
}

std::optional<ThingConfig> thing_config(const json::Value& entry, const std::string& location,
                                        Errors& errors) {
    if (entry.is_string()) {
        ThingConfig thing;
        thing.cls = entry.get<std::string>();
        return thing;
    }
    if (!entry.is_object()) {
        errors.emplace_back(location,
                            "Input should be a Thing class (an import string) or a Thing configuration");
        return std::nullopt;
    }
    bool ok = true;
    auto fail = [&](const std::string& field, const std::string& message) {
        errors.emplace_back(location + "." + field, message);
        ok = false;
    };
    ThingConfig thing;
    auto cls = entry.find("cls");
    if (cls == entry.end()) {
        cls = entry.find("class");
    }
    if (cls == entry.end()) {
        fail("cls", "Field required");
    } else if (!cls->is_string()) {
        fail("cls", "Input should be a valid string");
    } else {
        thing.cls = cls->get<std::string>();
    }
    if (const auto args = entry.find("args"); args != entry.end()) {
        if (args->is_array()) {
            thing.args.assign(args->begin(), args->end());
        } else {
            fail("args", "Input should be a valid list");
        }
    }
    if (const auto kwargs = entry.find("kwargs"); kwargs != entry.end()) {
        if (kwargs->is_object()) {
            thing.kwargs = *kwargs;
        } else {
            fail("kwargs", "Input should be a valid dictionary");
        }
    }
    if (const auto slots = entry.find("thing_slots"); slots != entry.end()) {
        if (slots->is_object()) {
            for (const auto& [slot, target] : slots->items()) {
                std::optional<SlotSelection> selection;
                if (target.is_null()) {
                    selection = SlotSelection::nothing();
                } else if (target.is_string()) {
                    selection = SlotSelection::name(target.get<std::string>());
                } else if (target.is_array() && std::ranges::all_of(target, [](const json::Value& name) {
                               return name.is_string();
                           })) {
                    std::vector<std::string> names;
                    for (const auto& name : target) {
                        names.push_back(name.get<std::string>());
                    }
                    selection = SlotSelection::names(std::move(names));
                }
                if (selection) {
                    thing.thing_slots.emplace_back(slot, std::move(*selection));
                } else {
                    fail("thing_slots." + slot, "Input should be a Thing name, a list of names, or null");
                }
            }
        } else {
            fail("thing_slots", "Input should be a valid dictionary");
        }
    }
    if (!ok) {
        return std::nullopt;
    }
    return thing;
}

std::optional<SecurityConfig> security_config(const json::Value& value, Errors& errors) {
    if (!value.is_object()) {
        errors.emplace_back("security", "Input should be a valid dictionary");
        return std::nullopt;
    }
    auto string = [&](const std::string& key) -> std::optional<std::string> {
        const auto it = value.find(key);
        if (it == value.end()) {
            errors.emplace_back("security." + key, "Field required");
            return std::nullopt;
        }
        if (!it->is_string() || it->get<std::string>().empty()) {
            errors.emplace_back("security." + key, "Input should be a non-empty string");
            return std::nullopt;
        }
        return it->get<std::string>();
    };
    const auto scheme = value.find("scheme");
    const std::string name = scheme != value.end() && scheme->is_string() ? scheme->get<std::string>() : "";
    if (name == "basic") {
        auto username = string("username");
        auto password_env = string("password_env");
        if (!username || !password_env) {
            return std::nullopt;
        }
        return SecurityConfig{
            SecurityConfig::Scheme::basic, std::move(*username), std::move(*password_env), {}};
    }
    if (name == "bearer") {
        auto token_env = string("token_env");
        if (!token_env) {
            return std::nullopt;
        }
        return SecurityConfig{SecurityConfig::Scheme::bearer, {}, {}, std::move(*token_env)};
    }
    errors.emplace_back("security.scheme", "Input should be 'basic' or 'bearer'");
    return std::nullopt;
}

}  // namespace

ConfigError::ConfigError(std::vector<std::pair<std::string, std::string>> errors)
    : errors_(std::move(errors)), text_(config_text(errors_)) {}

std::string SecurityConfig::secret() const {
    const bool basic = scheme == Scheme::basic;
    const std::string& variable = basic ? password_env : token_env;
    std::string value;
#ifdef _WIN32
    char* found = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&found, &length, variable.c_str()) == 0 && found) {
        value = found;
        std::free(found);
    }
#else
    if (const char* found = std::getenv(variable.c_str())) {
        value = found;
    }
#endif
    if (value.empty()) {
        throw ConfigError({{basic ? "security.password_env" : "security.token_env",
                            "The environment variable " + variable + " isn't set"}});
    }
    return value;
}

ServerConfig ServerConfig::from_json(std::string_view text) {
    json::Value value;
    try {
        value = json::parse(text);
    } catch (const json::ParseError& error) {
        throw ConfigError({{"", std::string("Invalid JSON: ") + error.what()}});
    }
    return from_value(value);
}

ServerConfig ServerConfig::from_value(const json::Value& value) {
    if (!value.is_object()) {
        throw ConfigError({{"", "Input should be a valid dictionary or instance of ThingServerConfig"}});
    }
    Errors errors;
    ServerConfig config;
    auto get = [&](std::string_view key) -> const json::Value* {
        const auto it = value.find(std::string(key));
        return it == value.end() ? nullptr : &*it;
    };

    if (const auto* things = get("things"); !things) {
        errors.emplace_back("things", "Field required");
    } else if (!things->is_object()) {
        errors.emplace_back("things", "Input should be a valid dictionary");
    } else {
        for (const auto& [name, entry] : things->items()) {
            const std::string location = "things." + name;
            if (auto problem = thing_name_problem(name)) {
                errors.emplace_back(location + ".[key]", std::move(*problem));
            }
            if (auto thing = thing_config(entry, location, errors)) {
                config.things.emplace_back(name, std::move(*thing));
            }
        }
    }

    if (const auto* folder = get("settings_folder"); folder && !folder->is_null()) {
        if (folder->is_string()) {
            config.settings_folder = folder->get<std::string>();
        } else {
            errors.emplace_back("settings_folder", "Input should be a valid string");
        }
    }
    if (const auto* prefix = get("api_prefix")) {
        if (!prefix->is_string()) {
            errors.emplace_back("api_prefix", "Input should be a valid string");
        } else if (!http::valid_api_prefix(prefix->get<std::string>())) {
            errors.emplace_back("api_prefix", "String should match pattern '^(\\/[\\w-]+)*$'");
        } else {
            config.api_prefix = prefix->get<std::string>();
        }
    }
    if (const auto* lock = get("enable_global_lock")) {
        if (const auto enabled = lax_bool(*lock)) {
            config.enable_global_lock = *enabled;
        } else {
            errors.emplace_back("enable_global_lock", "Input should be a valid boolean");
        }
    }
    if (const auto* level = get("global_lock_log_level")) {
        const std::string name = level->is_string() ? level->get<std::string>() : "";
        if (name == "DEBUG") {
            config.global_lock_log_level = log::Level::debug;
        } else if (name == "INFO") {
            config.global_lock_log_level = log::Level::info;
        } else if (name == "WARNING") {
            config.global_lock_log_level = log::Level::warn;
        } else if (name == "ERROR") {
            config.global_lock_log_level = log::Level::error;
        } else {
            errors.emplace_back("global_lock_log_level",
                                "Input should be 'DEBUG', 'INFO', 'WARNING' or 'ERROR'");
        }
    }
    if (const auto* application = get("application_config"); application && !application->is_null()) {
        if (application->is_object()) {
            config.application_config = *application;
        } else {
            errors.emplace_back("application_config", "Input should be a valid dictionary");
        }
    }
    if (const auto* id = get("server_id"); id && !id->is_null()) {
        if (id->is_string()) {
            config.server_id = id->get<std::string>();
        } else {
            errors.emplace_back("server_id", "Input should be a valid string");
        }
    }
    if (const auto* profile = get("wire_profile"); profile && !profile->is_null()) {
        std::optional<http::WireProfile> known;
        if (profile->is_string()) {
            known = http::wire_profile_from_name(profile->get<std::string>());
        }
        if (known) {
            config.wire_profile = known;
        } else {
            errors.emplace_back("wire_profile", "Input should be 'tetathing' or 'wot'");
        }
    }
    if (const auto* security = get("security"); security && !security->is_null()) {
        config.security = security_config(*security, errors);
    }
    if (const auto* mdns = get("mdns")) {
        if (const auto enabled = lax_bool(*mdns)) {
            config.mdns = *enabled;
        } else {
            errors.emplace_back("mdns", "Input should be a valid boolean");
        }
    }

    static constexpr std::array<std::string_view, 10> known = {"things",
                                                               "settings_folder",
                                                               "api_prefix",
                                                               "enable_global_lock",
                                                               "global_lock_log_level",
                                                               "application_config",
                                                               "server_id",
                                                               "wire_profile",
                                                               "security",
                                                               "mdns"};
    for (const auto& [key, _] : value.items()) {
        if (std::ranges::find(known, key) == known.end()) {
            config.ignored_keys.push_back(key);
        }
    }

    if (!errors.empty()) {
        throw ConfigError(std::move(errors));
    }
    return config;
}

}  // namespace teta::wot
