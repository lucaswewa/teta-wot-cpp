// Codecs: how a C++ type is described (its JSON Schema, and so its TD
// DataSchema) and written as JSON (ADR-0007).
//
// `Codec<T>` is the customisation point. It gives:
//
// - `static json::Value json_schema()`: the type's JSON Schema, shaped as
//   schemars shapes the corresponding Rust type's (draft 2020-12, inlined:
//   no `$defs`; the conversion to a DataSchema doesn't depend on that);
// - `static constexpr bool referenced`: whether schemars would refer to the
//   schema (`$ref`) rather than inline it, which changes how an optional is
//   described (structs and enums are referenced);
// - `static json::Value encode(const T&)`: the value as serde writes it.
//
// Built in: `bool`, integers, `float` and `double`, `std::string`,
// `std::monostate` (Rust's `()`), `json::Value` (any value),
// `std::optional`, `std::vector`, `std::array`, `std::pair` and
// `std::tuple`, maps from strings (`std::map`, `std::unordered_map`,
// `OrderedMap`), enums described with `Describe`, and aggregate structs,
// whose fields are found with Boost.PFR and described with `describe`.
//
// `for_type<T>()` is the type's DataSchema: its JSON Schema, converted.

#pragma once

#include <algorithm>
#include <array>
#include <boost/pfr.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/convert.hpp"

namespace teta::wot {

/// A map from strings that keeps insertion order (Rust's `IndexMap`).
template <typename V>
using OrderedMap = nlohmann::ordered_map<std::string, V>;

/// The primary template has no members: a type without a codec can't be a
/// property, an input or an output, and says so at compile time.
template <typename T, typename = void>
struct Codec;

/// Types that have a codec.
template <typename T>
concept Encodable = requires(const T& value) {
    { Codec<T>::json_schema() } -> std::same_as<json::Value>;
    { Codec<T>::encode(value) } -> std::same_as<json::Value>;
    { Codec<T>::referenced } -> std::convertible_to<bool>;
};

namespace codec {

/// The schema being generated on this thread, so that a struct that contains
/// itself refers to itself (`$ref`) instead of expanding forever. Only
/// recursive structs end up in its `$defs`; the conversion then stops at its
/// recursion limit, as teta-wot's does for a recursive type.
struct Generator {
    json::Value defs = json::Value::object();
    std::vector<std::string> in_progress;
    std::vector<std::string> recursive;
};

inline thread_local Generator* current_generator = nullptr;

}  // namespace codec

/// A type's JSON Schema, with the `$defs` of its recursive structs.
template <Encodable T>
json::Value json_schema_for() {
    codec::Generator generator;
    codec::Generator* previous = codec::current_generator;
    codec::current_generator = &generator;
    json::Value schema;
    try {
        schema = Codec<T>::json_schema();
    } catch (...) {
        codec::current_generator = previous;
        throw;
    }
    codec::current_generator = previous;
    if (!generator.defs.empty() && schema.is_object()) {
        schema["$defs"] = std::move(generator.defs);
    }
    return schema;
}

/// The DataSchema of a type: its JSON Schema, converted (teta-wot's
/// `DataSchema::for_type`).
template <Encodable T>
td::DataSchema for_type() {
    return td::from_json_schema(json_schema_for<T>());
}

/// A value as JSON, as serde writes it.
template <Encodable T>
json::Value encode(const T& value) {
    return Codec<T>::encode(value);
}

// ---- Schema helpers -------------------------------------------------------------

namespace codec {

/// Makes a schema accept `null` too, exactly as schemars does for `Option<T>`
/// (`allow_null`): a referenced schema, or one with `anyOf`, `oneOf`,
/// `allOf`, `if` or `$ref`, becomes `anyOf: [schema, {"type": "null"}]`;
/// otherwise `null` joins its `type` (and its `enum`).
json::Value allow_null(json::Value schema, bool referenced);

/// One member of an object schema, as a struct field or an action parameter.
struct Field {
    std::string name;
    json::Value schema;
    /// Whether the member must be present (not `serde(default)`, not an
    /// optional).
    bool required = true;
    std::optional<std::string> description;
    /// The default written in the schema (serde's `default`).
    std::optional<json::Value> default_value;
};

/// The JSON Schema schemars makes for a struct with these fields.
json::Value object_schema(const std::vector<Field>& fields, const std::optional<std::string>& description,
                          bool deny_unknown_fields);

}  // namespace codec

// ---- Describing structs and enums ---------------------------------------------------

/// What a struct's `describe` (or `Describe<T>::describe`) says about it:
/// what schemars takes from doc comments and serde attributes.
template <typename T>
class StructInfo {
  public:
    /// Per-field options.
    class FieldInfo {
      public:
        /// The field's description (a doc comment in Rust).
        FieldInfo& description(std::string text) {
            description_ = std::move(text);
            return *this;
        }
        /// The field may be left out, taking its value in a default-constructed
        /// struct (`#[serde(default)]`).
        FieldInfo& defaulted() {
            defaulted_ = true;
            return *this;
        }

      private:
        friend class StructInfo;
        std::string name_;
        std::optional<std::string> description_;
        bool defaulted_ = false;
    };

    /// The struct's description (a doc comment in Rust).
    StructInfo& description(std::string text) {
        description_ = std::move(text);
        return *this;
    }
    /// Refuse members that aren't fields (`#[serde(deny_unknown_fields)]`).
    StructInfo& deny_unknown_fields() {
        deny_unknown_fields_ = true;
        return *this;
    }
    /// The field named `name` (its C++ name).
    FieldInfo& field(std::string_view name) {
        for (auto& f : fields_) {
            if (f.name_ == name) {
                return f;
            }
        }
        fields_.emplace_back();
        fields_.back().name_ = std::string(name);
        return fields_.back();
    }

    const std::optional<std::string>& description_text() const { return description_; }
    bool denies_unknown_fields() const { return deny_unknown_fields_; }
    const FieldInfo* find(std::string_view name) const {
        for (const auto& f : fields_) {
            if (f.name_ == name) {
                return &f;
            }
        }
        return nullptr;
    }
    static const std::optional<std::string>& description_of(const FieldInfo& f) { return f.description_; }
    static bool is_defaulted(const FieldInfo& f) { return f.defaulted_; }

  private:
    std::optional<std::string> description_;
    bool deny_unknown_fields_ = false;
    std::vector<FieldInfo> fields_;
};

/// What an enum's `Describe<E>::describe` says: its values' names (as serde
/// writes them) and its description.
template <typename E>
class EnumInfo {
  public:
    /// A value and its name; with a description (a variant's doc comment in
    /// Rust), as schemars describes documented variants.
    EnumInfo& value(E value, std::string name, std::optional<std::string> description = std::nullopt) {
        values_.emplace_back(value, std::move(name));
        descriptions_.push_back(std::move(description));
        return *this;
    }
    EnumInfo& description(std::string text) {
        description_ = std::move(text);
        return *this;
    }

    const std::vector<std::pair<E, std::string>>& values() const { return values_; }
    const std::vector<std::optional<std::string>>& value_descriptions() const { return descriptions_; }
    const std::optional<std::string>& description_text() const { return description_; }

  private:
    std::vector<std::pair<E, std::string>> values_;
    std::vector<std::optional<std::string>> descriptions_;
    std::optional<std::string> description_;
};

/// Describes a type the codecs can't see into: specialise it with
/// `static void describe(StructInfo<T>&)` or `static void describe(EnumInfo<E>&)`.
/// A struct can instead have a static member function `describe`.
template <typename T>
struct Describe;

namespace codec {

template <typename T>
concept HasDescribeSpecialisation = requires(StructInfo<T>& s) { Describe<T>::describe(s); };

template <typename T>
concept HasMemberDescribe = requires(StructInfo<T>& s) { T::describe(s); };

template <typename T>
StructInfo<T> struct_info() {
    StructInfo<T> info;
    if constexpr (HasDescribeSpecialisation<T>) {
        Describe<T>::describe(info);
    } else if constexpr (HasMemberDescribe<T>) {
        T::describe(info);
    }
    return info;
}

template <typename E>
EnumInfo<E> enum_info() {
    EnumInfo<E> info;
    Describe<E>::describe(info);
    return info;
}

template <typename T>
inline constexpr bool is_optional = false;
template <typename T>
inline constexpr bool is_optional<std::optional<T>> = true;

template <typename T>
concept DescribedEnum = std::is_enum_v<T> && requires(EnumInfo<T>& e) { Describe<T>::describe(e); };

/// Aggregates the codecs describe through Boost.PFR: plain structs that
/// aren't one of the built-in kinds.
template <typename T>
concept ReflectedStruct = std::is_class_v<T> && std::is_aggregate_v<T> && !std::is_same_v<T, std::monostate>;

}  // namespace codec

// ---- Scalars ------------------------------------------------------------------------

template <>
struct Codec<bool> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "boolean"}}; }
    static json::Value encode(bool value) { return value; }
};

/// Integers, as schemars describes Rust's of the same size: a `format`
/// (`int8` ... `uint64`), and bounds for unsigned and 8- and 16-bit ones.
template <std::integral T>
    requires(!std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> &&
             !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>)
struct Codec<T> {
    static constexpr bool referenced = false;
    static json::Value json_schema() {
        constexpr bool is_signed = std::is_signed_v<T>;
        constexpr int bits = static_cast<int>(sizeof(T)) * 8;
        json::Value schema = {{"type", "integer"},
                              {"format", std::string(is_signed ? "int" : "uint") + std::to_string(bits)}};
        if constexpr (is_signed) {
            if constexpr (bits <= 16) {
                schema["minimum"] = static_cast<std::int64_t>(std::numeric_limits<T>::min());
                schema["maximum"] = static_cast<std::int64_t>(std::numeric_limits<T>::max());
            }
        } else {
            schema["minimum"] = 0;
            if constexpr (bits <= 16) {
                schema["maximum"] = static_cast<std::uint64_t>(std::numeric_limits<T>::max());
            }
        }
        return schema;
    }
    static json::Value encode(T value) {
        if constexpr (std::is_signed_v<T>) {
            return static_cast<std::int64_t>(value);
        } else {
            return static_cast<std::uint64_t>(value);
        }
    }
};

template <>
struct Codec<double> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "number"}, {"format", "double"}}; }
    static json::Value encode(double value) { return value; }
};

template <>
struct Codec<float> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "number"}, {"format", "float"}}; }
    /// The shortest decimal that reads back as the same `float`, as serde
    /// writes an `f32` (so 0.1f is 0.1, not 0.10000000149011612).
    static json::Value encode(float value);
};

template <>
struct Codec<std::string> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "string"}}; }
    static json::Value encode(const std::string& value) { return value; }
};

/// Rust's `()`: `null`.
template <>
struct Codec<std::monostate> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "null"}}; }
    static json::Value encode(std::monostate) { return nullptr; }
};

/// Any JSON value (Rust's `serde_json::Value`): the schema `true`.
template <>
struct Codec<json::Value> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return true; }
    static json::Value encode(const json::Value& value) { return value; }
};

// ---- Containers ---------------------------------------------------------------------

template <Encodable T>
struct Codec<std::optional<T>> {
    static constexpr bool referenced = false;
    static json::Value json_schema() {
        return codec::allow_null(Codec<T>::json_schema(), Codec<T>::referenced);
    }
    static json::Value encode(const std::optional<T>& value) {
        return value ? Codec<T>::encode(*value) : json::Value(nullptr);
    }
};

template <Encodable T>
struct Codec<std::vector<T>> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return {{"type", "array"}, {"items", Codec<T>::json_schema()}}; }
    static json::Value encode(const std::vector<T>& values) {
        json::Value out = json::Value::array();
        for (const auto& value : values) {
            out.push_back(Codec<T>::encode(value));
        }
        return out;
    }
};

template <Encodable T, std::size_t N>
struct Codec<std::array<T, N>> {
    static constexpr bool referenced = false;
    static json::Value json_schema() {
        return {{"type", "array"}, {"items", Codec<T>::json_schema()}, {"minItems", N}, {"maxItems", N}};
    }
    static json::Value encode(const std::array<T, N>& values) {
        json::Value out = json::Value::array();
        for (const auto& value : values) {
            out.push_back(Codec<T>::encode(value));
        }
        return out;
    }
};

/// Tuples (and pairs) are fixed-length arrays with a schema per position.
template <Encodable... Ts>
struct Codec<std::tuple<Ts...>> {
    static constexpr bool referenced = false;
    static json::Value json_schema() {
        return {{"type", "array"},
                {"prefixItems", json::Value::array({Codec<Ts>::json_schema()...})},
                {"minItems", sizeof...(Ts)},
                {"maxItems", sizeof...(Ts)}};
    }
    static json::Value encode(const std::tuple<Ts...>& values) {
        return std::apply([](const auto&... v) { return json::Value::array({Codec<Ts>::encode(v)...}); },
                          values);
    }
};

template <Encodable A, Encodable B>
struct Codec<std::pair<A, B>> {
    static constexpr bool referenced = false;
    static json::Value json_schema() { return Codec<std::tuple<A, B>>::json_schema(); }
    static json::Value encode(const std::pair<A, B>& value) {
        return json::Value::array({Codec<A>::encode(value.first), Codec<B>::encode(value.second)});
    }
};

namespace codec {

template <typename Map>
struct StringMapCodec {
    using V = typename Map::mapped_type;
    static constexpr bool referenced = false;
    static json::Value json_schema() {
        return {{"type", "object"}, {"additionalProperties", Codec<V>::json_schema()}};
    }
    static json::Value encode(const Map& map) {
        json::Value out = json::Value::object();
        for (const auto& [key, value] : map) {
            out[key] = Codec<V>::encode(value);
        }
        return out;
    }
};

}  // namespace codec

template <Encodable V>
struct Codec<std::map<std::string, V>> : codec::StringMapCodec<std::map<std::string, V>> {};

template <Encodable V>
struct Codec<std::unordered_map<std::string, V>> : codec::StringMapCodec<std::unordered_map<std::string, V>> {
};

template <Encodable V>
struct Codec<OrderedMap<V>> : codec::StringMapCodec<OrderedMap<V>> {};

// ---- Enums --------------------------------------------------------------------------

/// Enums whose values are described (`Describe<E>`): a string, one of their
/// names, as serde writes unit-only enums. As schemars does, the schema is an
/// `enum` of the names, or, when a value has a description, a `oneOf` of
/// `const`s.
template <codec::DescribedEnum E>
struct Codec<E> {
    static constexpr bool referenced = true;
    static json::Value json_schema() {
        const auto info = codec::enum_info<E>();
        const auto& descriptions = info.value_descriptions();
        const bool documented =
            std::ranges::any_of(descriptions, [](const auto& d) { return d.has_value(); });
        json::Value schema;
        if (documented) {
            json::Value branches = json::Value::array();
            for (std::size_t i = 0; i < info.values().size(); ++i) {
                json::Value branch = {{"type", "string"}, {"const", info.values()[i].second}};
                if (descriptions[i]) {
                    branch["description"] = *descriptions[i];
                }
                branches.push_back(std::move(branch));
            }
            schema = {{"oneOf", std::move(branches)}};
        } else {
            json::Value names = json::Value::array();
            for (const auto& [value, name] : info.values()) {
                names.push_back(name);
            }
            schema = {{"type", "string"}, {"enum", std::move(names)}};
        }
        if (info.description_text()) {
            schema["description"] = *info.description_text();
        }
        return schema;
    }
    static json::Value encode(E value) {
        const auto info = codec::enum_info<E>();
        for (const auto& [v, name] : info.values()) {
            if (v == value) {
                return name;
            }
        }
        throw std::invalid_argument("an enum value with no name");
    }
};

// ---- Structs ------------------------------------------------------------------------

/// Aggregate structs, through Boost.PFR: an object with a member per field,
/// in order. A field is required unless it is an optional or `defaulted`.
template <codec::ReflectedStruct T>
struct Codec<T> {
    static constexpr bool referenced = true;

    static json::Value json_schema() {
        codec::Generator* generator = codec::current_generator;
        const std::string key = typeid(T).name();
        if (generator) {
            if (std::ranges::find(generator->in_progress, key) != generator->in_progress.end()) {
                // The struct contains itself: refer to it, as schemars does.
                if (std::ranges::find(generator->recursive, key) == generator->recursive.end()) {
                    generator->recursive.push_back(key);
                }
                return {{"$ref", "#/$defs/" + key}};
            }
            generator->in_progress.push_back(key);
        }
        json::Value schema = schema_of_fields();
        if (generator) {
            generator->in_progress.pop_back();
            if (std::ranges::find(generator->recursive, key) != generator->recursive.end()) {
                generator->defs[key] = schema;
            }
        }
        return schema;
    }

    static json::Value encode(const T& value) {
        json::Value out = json::Value::object();
        constexpr auto names = boost::pfr::names_as_array<T>();
        boost::pfr::for_each_field(value, [&](const auto& member, std::size_t index) {
            using M = std::remove_cvref_t<decltype(member)>;
            out[std::string(names[index])] = Codec<M>::encode(member);
        });
        return out;
    }

  private:
    static json::Value schema_of_fields() {
        const auto info = codec::struct_info<T>();
        const T defaults{};
        std::vector<codec::Field> fields;
        constexpr auto names = boost::pfr::names_as_array<T>();
        boost::pfr::for_each_field(defaults, [&](const auto& member, std::size_t index) {
            using M = std::remove_cvref_t<decltype(member)>;
            static_assert(Encodable<M>, "every field of a struct needs a codec");
            codec::Field field;
            field.name = std::string(names[index]);
            field.schema = Codec<M>::json_schema();
            const auto* field_info = info.find(names[index]);
            const bool defaulted = field_info && StructInfo<T>::is_defaulted(*field_info);
            field.required = !defaulted && !codec::is_optional<M>;
            if (field_info) {
                field.description = StructInfo<T>::description_of(*field_info);
            }
            if (defaulted) {
                field.default_value = Codec<M>::encode(member);
            }
            fields.push_back(std::move(field));
        });
        return codec::object_schema(fields, info.description_text(), info.denies_unknown_fields());
    }
};

// ---- NoInput ------------------------------------------------------------------------

/// The input of an action that takes no parameters: a missing body, `null` or
/// `{}`, and nothing else. Written back as it came (`null` or `{}`).
struct NoInput {
    bool was_object = false;
    bool operator==(const NoInput&) const = default;
};

template <>
struct Codec<NoInput> {
    static constexpr bool referenced = true;
    static json::Value json_schema();
    static json::Value encode(const NoInput& value) {
        return value.was_object ? json::Value::object() : json::Value(nullptr);
    }
};

}  // namespace teta::wot
