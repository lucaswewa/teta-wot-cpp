// Reading and writing the TD model's JSON: the helpers every model type uses,
// so they all follow serde's rules the same way (absent `Option`s skipped,
// `null` read as absent, strict types, unknown keys kept in `extra`).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/one_or_many.hpp"

namespace teta::wot::td::detail {

// ---- Writing ----------------------------------------------------------------

inline void put(json::Value& out, const char* key, const std::optional<std::string>& value) {
    if (value) {
        out[key] = *value;
    }
}

inline void put(json::Value& out, const char* key, const std::optional<bool>& value) {
    if (value) {
        out[key] = *value;
    }
}

inline void put(json::Value& out, const char* key, const std::optional<std::uint64_t>& value) {
    if (value) {
        out[key] = *value;
    }
}

/// A JSON value: skipped when absent or (for `default` and `const`) null.
inline void put_value(json::Value& out, const char* key, const std::optional<json::Value>& value,
                      bool skip_null) {
    if (value && !(skip_null && value->is_null())) {
        out[key] = *value;
    }
}

inline json::Value to_json(const OneOrMany<std::string>& value) {
    if (value.is_many()) {
        return json::Value(value.many());
    }
    return value.one();
}

inline void put(json::Value& out, const char* key, const std::optional<OneOrMany<std::string>>& value) {
    if (value) {
        out[key] = to_json(*value);
    }
}

inline json::Value pairs_to_json(const std::vector<std::pair<std::string, std::string>>& pairs) {
    json::Value out = json::Value::object();
    for (const auto& [k, v] : pairs) {
        out[k] = v;
    }
    return out;
}

/// Appends the `extra` terms after the typed ones.
inline void put_extra(json::Value& out, const json::Value& extra) {
    for (const auto& [key, value] : extra.items()) {
        out[key] = value;
    }
}

// ---- Reading ----------------------------------------------------------------

/// Thrown by the readers; each model's reader turns it into its own error.
struct ReadError {
    std::string message;
};

[[noreturn]] inline void fail(std::string_view key, std::string_view expected, const json::Value& found) {
    throw ReadError{"`" + std::string(key) + "`: expected " + std::string(expected) + ", found " +
                    json::dump(found)};
}

/// The members of an object, read once each; what isn't read is `extra`.
class ObjectReader {
  public:
    ObjectReader(const json::Value& object, std::string_view what) : object_(object) {
        if (!object.is_object()) {
            throw ReadError{std::string("expected ") + std::string(what) + " (an object), found " +
                            json::dump(object)};
        }
    }

    /// The member `key`, if present and not null (serde reads null as `None`).
    const json::Value* get(std::string_view key) {
        read_.emplace_back(key);
        auto it = object_.find(key);
        if (it == object_.end() || it->is_null()) {
            return nullptr;
        }
        return &*it;
    }

    /// The member `key` even if null; for required members.
    const json::Value* get_required(std::string_view key, std::string_view what) {
        read_.emplace_back(key);
        auto it = object_.find(key);
        if (it == object_.end()) {
            throw ReadError{"missing `" + std::string(key) + "` in " + std::string(what)};
        }
        return &*it;
    }

    std::optional<std::string> string(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (!v->is_string()) {
            fail(key, "a string", *v);
        }
        return v->get<std::string>();
    }

    std::optional<bool> boolean(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (!v->is_boolean()) {
            fail(key, "a boolean", *v);
        }
        return v->get<bool>();
    }

    std::optional<std::uint64_t> unsigned_integer(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (v->is_number_unsigned()) {
            return v->get<std::uint64_t>();
        }
        if (v->is_number_integer() && v->get<std::int64_t>() >= 0) {
            return static_cast<std::uint64_t>(v->get<std::int64_t>());
        }
        fail(key, "a non-negative integer", *v);
    }

    std::optional<json::Value> number(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (!v->is_number()) {
            fail(key, "a number", *v);
        }
        return *v;
    }

    std::optional<json::Value> any(std::string_view key) {
        const auto* v = get(key);
        return v ? std::optional<json::Value>(*v) : std::nullopt;
    }

    std::optional<std::vector<std::string>> strings(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (!v->is_array()) {
            fail(key, "an array of strings", *v);
        }
        std::vector<std::string> out;
        for (const auto& item : *v) {
            if (!item.is_string()) {
                fail(key, "an array of strings", *v);
            }
            out.push_back(item.get<std::string>());
        }
        return out;
    }

    std::optional<OneOrMany<std::string>> one_or_many_strings(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (v->is_string()) {
            return OneOrMany<std::string>(v->get<std::string>());
        }
        read_.pop_back();
        auto many = strings(key);
        return OneOrMany<std::string>(std::move(*many));
    }

    std::optional<std::vector<std::pair<std::string, std::string>>> string_map(std::string_view key) {
        const auto* v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (!v->is_object()) {
            fail(key, "an object of strings", *v);
        }
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& [k, item] : v->items()) {
            if (!item.is_string()) {
                fail(key, "an object of strings", *v);
            }
            out.emplace_back(k, item.get<std::string>());
        }
        return out;
    }

    /// The members not read, in their order: serde's `flatten`ed map.
    json::Value rest() const {
        json::Value out = json::Value::object();
        for (const auto& [key, value] : object_.items()) {
            bool known = false;
            for (const auto& r : read_) {
                if (r == key) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                out[key] = value;
            }
        }
        return out;
    }

  private:
    const json::Value& object_;
    std::vector<std::string> read_;
};

}  // namespace teta::wot::td::detail
