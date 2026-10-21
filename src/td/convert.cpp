#include "teta/wot/td/convert.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <string_view>
#include <vector>

namespace teta::wot::td {

namespace {

using Kind = ConversionError::Kind;
using Object = json::Value;  // always an object here

ConversionError conflict(std::string_view message, std::string_view path) {
    return {Kind::conflict, std::string(message) + " (at " + std::string(path) + ")"};
}

// Escapes a key for a JSON pointer.
std::string escape(std::string_view key) {
    std::string out;
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

// Shortens a deep path to its first and last segments.
std::string abbreviate(const std::string& path) {
    constexpr std::size_t keep = 6;
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (true) {
        const auto slash = path.find('/', start);
        segments.push_back(path.substr(start, slash - start));
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    if (segments.size() <= 2 * keep) {
        return path;
    }
    auto join = [](auto first, auto last) {
        std::string out;
        for (auto it = first; it != last; ++it) {
            if (it != first) {
                out += '/';
            }
            out += *it;
        }
        return out;
    };
    return join(segments.begin(), segments.begin() + keep) + "/\xE2\x80\xA6/" +
           join(segments.end() - keep, segments.end());
}

// A schema as an object: `true` is `{}`, `false` is an error.
Object as_schema_object(const json::Value& schema, const std::string& path) {
    if (schema.is_object()) {
        return schema;
    }
    if (schema.is_boolean()) {
        if (schema.get<bool>()) {
            return Object::object();
        }
        throw ConversionError(Kind::false_schema,
                              "the schema `false` can't be expressed as a DataSchema (at " + path + ")");
    }
    throw ConversionError(Kind::not_a_schema, "expected a schema (an object or a boolean), found `" +
                                                  json::dump(schema) + "` (at " + path + ")");
}

// `base` with every key of `overrides` set on it; keys already in `base` keep
// their position.
Object merge(Object base, const Object& overrides) {
    for (const auto& [key, value] : overrides.items()) {
        base[key] = value;
    }
    return base;
}

const json::Value* find(const Object& obj, std::string_view key) {
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

// Removes `key`, returning its value if it was there.
std::optional<json::Value> take(Object& obj, std::string_view key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return std::nullopt;
    }
    json::Value value = std::move(*it);
    obj.erase(std::string(key));
    return value;
}

void sort_keys(Object& obj) {
    std::vector<std::pair<std::string, json::Value>> entries;
    for (auto& [key, value] : obj.items()) {
        entries.emplace_back(key, std::move(value));
    }
    std::ranges::stable_sort(entries, {}, &std::pair<std::string, json::Value>::first);
    Object sorted = Object::object();
    for (auto& [key, value] : entries) {
        sorted[key] = std::move(value);
    }
    obj = std::move(sorted);
}

bool is_numeric_format(std::string_view format) {
    static constexpr std::string_view formats[] = {"int",    "int8",    "int16", "int32",  "int64",
                                                   "int128", "uint",    "uint8", "uint16", "uint32",
                                                   "uint64", "uint128", "float", "double"};
    return std::ranges::find(formats, format) != std::end(formats);
}

// The keywords that only apply to values of type `t`.
std::span<const std::string_view> type_keywords(std::string_view t) {
    static constexpr std::string_view numeric[] = {"minimum",          "maximum",    "exclusiveMinimum",
                                                   "exclusiveMaximum", "multipleOf", "format"};
    static constexpr std::string_view string[] = {"minLength",    "maxLength",       "pattern",
                                                  "format",       "contentEncoding", "contentMediaType",
                                                  "contentSchema"};
    static constexpr std::string_view array[] = {"items",    "additionalItems", "unevaluatedItems",
                                                 "minItems", "maxItems",        "uniqueItems",
                                                 "contains", "minContains",     "maxContains"};
    static constexpr std::string_view object[] = {"properties",           "required",
                                                  "additionalProperties", "patternProperties",
                                                  "propertyNames",        "unevaluatedProperties",
                                                  "minProperties",        "maxProperties",
                                                  "dependentRequired",    "dependentSchemas"};
    if (t == "integer" || t == "number") {
        return numeric;
    }
    if (t == "string") {
        return string;
    }
    if (t == "array") {
        return array;
    }
    if (t == "object") {
        return object;
    }
    return {};
}

// The index of the type in `types` that `value` belongs to. Whole numbers
// prefer `integer` and fall back to `number`.
std::optional<std::size_t> branch_for(const std::vector<std::string>& types, const json::Value& value) {
    auto position = [&](std::string_view name) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (types[i] == name) {
                return i;
            }
        }
        return std::nullopt;
    };
    if (value.is_null()) {
        return position("null");
    }
    if (value.is_boolean()) {
        return position("boolean");
    }
    if (value.is_string()) {
        return position("string");
    }
    if (value.is_array()) {
        return position("array");
    }
    if (value.is_object()) {
        return position("object");
    }
    const bool whole = value.is_number_integer() || value.is_number_unsigned() ||
                       (value.is_number_float() && std::isfinite(value.get<double>()) &&
                        value.get<double>() == std::trunc(value.get<double>()));
    if (whole) {
        if (auto i = position("integer")) {
            return i;
        }
    }
    return position("number");
}

// Replaces a `type` array with a `oneOf` of single-type branches.
void split_type_array(Object& obj, const json::Value& type_array, const std::string& path) {
    std::vector<std::string> given;
    for (const auto& t : type_array) {
        if (!t.is_string()) {
            throw conflict("`type` must contain only strings", path);
        }
        given.push_back(t.get<std::string>());
    }
    // `number` already includes integers.
    const bool has_number = std::ranges::find(given, "number") != given.end();
    std::vector<std::string> types;
    for (const auto& t : given) {
        if ((t != "integer" || !has_number) && std::ranges::find(types, t) == types.end()) {
            types.push_back(t);
        }
    }
    if (types.empty()) {
        throw conflict("`type` is an empty array", path);
    }
    if (types.size() == 1) {
        obj["type"] = types[0];
        return;
    }
    if (obj.contains("oneOf")) {
        throw conflict("`type` is an array and `oneOf` is also present", path);
    }

    std::vector<Object> branches;
    for (const auto& t : types) {
        Object branch = Object::object();
        branch["type"] = t;
        for (const auto keyword : type_keywords(t)) {
            if (const auto* value = find(obj, keyword)) {
                branch[std::string(keyword)] = *value;
            }
        }
        branches.push_back(std::move(branch));
    }
    obj.erase("type");
    for (const auto& t : types) {
        for (const auto keyword : type_keywords(t)) {
            obj.erase(std::string(keyword));
        }
    }

    // Each `enum` value goes to the branch of its own type; a branch with no
    // values is dropped.
    std::vector<bool> keep(branches.size(), true);
    if (const auto* e = find(obj, "enum"); e && e->is_array()) {
        const json::Value values = *e;
        obj.erase("enum");
        std::vector<std::vector<json::Value>> per_branch(branches.size());
        for (const auto& value : values) {
            const auto i = branch_for(types, value);
            if (!i) {
                throw conflict("enum value " + json::dump(value) + " matches none of the types", path);
            }
            per_branch[*i].push_back(value);
        }
        for (std::size_t i = 0; i < per_branch.size(); ++i) {
            if (per_branch[i].empty()) {
                keep[i] = false;
            } else if (types[i] != "null") {
                branches[i]["enum"] = json::Value(per_branch[i]);
            }
        }
    }
    if (auto value = take(obj, "const")) {
        const auto i = branch_for(types, *value);
        if (!i) {
            throw conflict("const value " + json::dump(*value) + " matches none of the types", path);
        }
        for (std::size_t j = 0; j < keep.size(); ++j) {
            keep[j] = keep[j] && j == *i;
        }
        if (types[*i] != "null") {
            branches[*i]["const"] = *value;
        }
    }

    std::vector<Object> kept;
    for (std::size_t i = 0; i < branches.size(); ++i) {
        if (keep[i]) {
            kept.push_back(std::move(branches[i]));
        }
    }
    if (kept.empty()) {
        throw conflict("no type allows any of the enum values", path);
    }
    if (kept.size() == 1) {
        for (const auto& [key, value] : kept[0].items()) {
            obj[key] = value;
        }
        return;
    }
    json::Value one_of = json::Value::array();
    for (auto& branch : kept) {
        one_of.push_back(std::move(branch));
    }
    obj["oneOf"] = std::move(one_of);
}

class Converter {
  public:
    Converter(const json::Value& root, std::size_t limit) : root_(root), limit_(limit) {}

    // Converts a schema in a position where TD expects a DataSchema.
    Object data_schema(const json::Value& schema, std::size_t depth, const std::string& path) const {
        check_depth(depth, path);
        Object obj = as_schema_object(schema, path);

        // References and single-element `allOf`s, until neither is left.
        while (true) {
            if (auto reference = take(obj, "$ref")) {
                const json::Value& target = resolve(*reference, path);
                ++depth;
                check_depth(depth, path);
                obj = merge(as_schema_object(target, path), obj);
            } else if (const auto* all_of = find(obj, "allOf");
                       all_of && all_of->is_array() && all_of->size() == 1) {
                const json::Value element = (*all_of)[0];
                obj.erase("allOf");
                ++depth;
                check_depth(depth, path);
                obj = merge(as_schema_object(element, path), obj);
            } else {
                break;
            }
        }

        for (const char* keyword : {"$schema", "$id", "$defs", "definitions"}) {
            obj.erase(keyword);
        }
        for (const char* keyword : {"$dynamicRef", "$recursiveRef"}) {
            if (obj.contains(keyword)) {
                throw ConversionError(Kind::unsupported,
                                      std::string("`") + keyword + "` is not supported (at " + path + ")");
            }
        }

        if (auto any_of = take(obj, "anyOf")) {
            if (obj.contains("oneOf")) {
                throw conflict("both `anyOf` and `oneOf` are present", path);
            }
            obj["oneOf"] = std::move(*any_of);
        }

        if (const auto* d = find(obj, "default"); d && d->is_null()) {
            obj.erase("default");
        }
        if (auto prefix_items = take(obj, "prefixItems")) {
            if (auto items = take(obj, "items"); items && !(items->is_boolean() && !items->get<bool>())) {
                throw conflict("both `prefixItems` and `items` are present", path);
            }
            obj["items"] = std::move(*prefix_items);
        }

        if (const auto* type = find(obj, "type"); type && type->is_array()) {
            const json::Value types = *type;
            split_type_array(obj, types, path);
        }

        if (const auto* c = find(obj, "const"); c && c->is_null()) {
            const auto* type = find(obj, "type");
            const bool compatible = !type || (type->is_string() && type->get<std::string>() == "null");
            if (!compatible) {
                throw conflict("`const: null` conflicts with `type`", path);
            }
            obj.erase("const");
            if (!obj.contains("type")) {
                obj["type"] = "null";
            }
        }

        if (const auto* type = find(obj, "type");
            type && type->is_string() &&
            (type->get<std::string>() == "integer" || type->get<std::string>() == "number")) {
            if (const auto* format = find(obj, "format");
                format && format->is_string() && is_numeric_format(format->get<std::string>())) {
                obj.erase("format");
            }
        }

        children(obj, depth + 1, path);
        sort_keys(obj);
        return obj;
    }

  private:
    // Converts the schemas nested inside `obj`.
    void children(Object& obj, std::size_t depth, const std::string& path) const {
        if (auto it = obj.find("properties"); it != obj.end() && it->is_object()) {
            for (auto& [name, schema] : it->items()) {
                schema = data_schema(schema, depth, path + "/properties/" + escape(name));
            }
        }
        if (auto it = obj.find("items"); it != obj.end()) {
            if (it->is_array()) {
                std::size_t i = 0;
                for (auto& schema : *it) {
                    schema = data_schema(schema, depth, path + "/items/" + std::to_string(i++));
                }
            } else {
                *it = data_schema(*it, depth, path + "/items");
            }
        }
        if (auto it = obj.find("oneOf"); it != obj.end() && it->is_array()) {
            std::size_t i = 0;
            for (auto& schema : *it) {
                schema = data_schema(schema, depth, path + "/oneOf/" + std::to_string(i++));
            }
        }
        // Keywords TD doesn't define: convert schemas, keep booleans.
        for (const char* keyword :
             {"additionalProperties", "unevaluatedProperties", "additionalItems", "unevaluatedItems",
              "propertyNames", "contains", "not", "if", "then", "else", "contentSchema"}) {
            if (auto it = obj.find(keyword); it != obj.end()) {
                *it = extension_schema(*it, depth, path + "/" + keyword);
            }
        }
        for (const char* keyword : {"patternProperties", "dependentSchemas"}) {
            if (auto it = obj.find(keyword); it != obj.end() && it->is_object()) {
                for (auto& [name, schema] : it->items()) {
                    schema = extension_schema(schema, depth, path + "/" + keyword + "/" + escape(name));
                }
            }
        }
        if (auto it = obj.find("allOf"); it != obj.end() && it->is_array()) {
            std::size_t i = 0;
            for (auto& schema : *it) {
                schema = extension_schema(schema, depth, path + "/allOf/" + std::to_string(i++));
            }
        }
    }

    json::Value extension_schema(const json::Value& schema, std::size_t depth,
                                 const std::string& path) const {
        if (schema.is_boolean()) {
            return schema;
        }
        return data_schema(schema, depth, path);
    }

    const json::Value& resolve(const json::Value& reference, const std::string& path) const {
        if (!reference.is_string()) {
            throw ConversionError(Kind::not_a_schema, "expected a schema (an object or a boolean), found `" +
                                                          json::dump(reference) + "` (at " + path + "/$ref)");
        }
        const auto& text = reference.get_ref<const std::string&>();
        if (text.empty() || text[0] != '#') {
            throw ConversionError(Kind::non_local_reference,
                                  "only local references (starting with `#`) can be resolved, not `" + text +
                                      "` (at " + path + ")");
        }
        try {
            const json::Value::json_pointer pointer(text.substr(1));
            if (root_.contains(pointer)) {
                return root_.at(pointer);
            }
        } catch (const json::Value::exception&) {
            // Not a valid pointer: not found.
        }
        throw ConversionError(Kind::unresolved_reference,
                              "reference `" + text + "` was not found in the schema (at " + path + ")");
    }

    void check_depth(std::size_t depth, const std::string& path) const {
        if (depth > limit_) {
            throw ConversionError(Kind::recursion_limit, "recursion limit of " + std::to_string(limit_) +
                                                             " exceeded at " + abbreviate(path) +
                                                             ": is the type recursive?");
        }
    }

    const json::Value& root_;
    std::size_t limit_;
};

}  // namespace

json::Value convert(const json::Value& schema, const ConversionOptions& options) {
    return Converter(schema, options.recursion_limit).data_schema(schema, 0, "#");
}

DataSchema from_json_schema(const json::Value& schema, const ConversionOptions& options) {
    const json::Value converted = convert(schema, options);
    try {
        return data_schema_from_json(converted);
    } catch (const InvalidDataSchema& error) {
        throw ConversionError(Kind::invalid_data_schema,
                              std::string("the converted schema is not a valid DataSchema: ") + error.what() +
                                  "\n" + json::dump_pretty(converted));
    }
}

}  // namespace teta::wot::td
