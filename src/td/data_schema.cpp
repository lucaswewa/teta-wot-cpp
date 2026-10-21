#include "teta/wot/td/data_schema.hpp"

#include "json_io.hpp"

namespace teta::wot::td {

namespace {

constexpr std::pair<DataType, const char*> kTypeNames[] = {
    {DataType::boolean, "boolean"}, {DataType::integer, "integer"}, {DataType::number, "number"},
    {DataType::string, "string"},   {DataType::object, "object"},   {DataType::array, "array"},
    {DataType::null, "null"},
};

DataSchema read(const json::Value& value);

std::vector<DataSchema> read_schemas(const json::Value& array, std::string_view key) {
    if (!array.is_array()) {
        detail::fail(key, "an array of schemas", array);
    }
    std::vector<DataSchema> out;
    for (const auto& item : array) {
        out.push_back(read(item));
    }
    return out;
}

DataSchema read(const json::Value& value) {
    detail::ObjectReader r(value, "a DataSchema");
    DataSchema s;
    s.semantic_type = r.one_or_many_strings("@type");
    s.description = r.string("description");
    s.title = r.string("title");
    s.descriptions = r.string_map("descriptions");
    s.titles = r.string_map("titles");
    s.write_only = r.boolean("writeOnly");
    s.read_only = r.boolean("readOnly");
    if (const auto* one_of = r.get("oneOf")) {
        s.one_of = read_schemas(*one_of, "oneOf");
    }
    s.unit = r.string("unit");
    if (const auto* e = r.get("enum")) {
        if (!e->is_array()) {
            detail::fail("enum", "an array", *e);
        }
        s.enumeration = std::vector<json::Value>(e->begin(), e->end());
    }
    s.format = r.string("format");
    s.constant = r.any("const");
    s.default_value = r.any("default");
    if (auto type = r.string("type")) {
        s.data_type = data_type_from_string(*type);
        if (!s.data_type) {
            detail::fail("type", "a JSON type name", json::Value(*type));
        }
    }
    if (const auto* items = r.get("items")) {
        if (items->is_array()) {
            s.items = ArrayItems{true, read_schemas(*items, "items")};
        } else {
            s.items = ArrayItems{false, {read(*items)}};
        }
    }
    s.max_items = r.unsigned_integer("maxItems");
    s.min_items = r.unsigned_integer("minItems");
    s.minimum = r.number("minimum");
    s.maximum = r.number("maximum");
    s.exclusive_minimum = r.number("exclusiveMinimum");
    s.exclusive_maximum = r.number("exclusiveMaximum");
    s.multiple_of = r.number("multipleOf");
    if (const auto* properties = r.get("properties")) {
        if (!properties->is_object()) {
            detail::fail("properties", "an object of schemas", *properties);
        }
        s.properties.emplace();
        for (const auto& [name, schema] : properties->items()) {
            s.properties->emplace_back(name, read(schema));
        }
    }
    s.required = r.strings("required");
    s.min_length = r.unsigned_integer("minLength");
    s.max_length = r.unsigned_integer("maxLength");
    s.pattern = r.string("pattern");
    s.content_encoding = r.string("contentEncoding");
    s.content_media_type = r.string("contentMediaType");
    s.extra = r.rest();
    return s;
}

}  // namespace

const char* to_string(DataType type) {
    for (const auto& [t, name] : kTypeNames) {
        if (t == type) {
            return name;
        }
    }
    return "?";
}

std::optional<DataType> data_type_from_string(std::string_view name) {
    for (const auto& [t, n] : kTypeNames) {
        if (name == n) {
            return t;
        }
    }
    return std::nullopt;
}

bool ArrayItems::operator==(const ArrayItems&) const = default;
bool DataSchema::operator==(const DataSchema&) const = default;

DataSchema DataSchema::of(DataType type) {
    DataSchema s;
    s.data_type = type;
    return s;
}

DataSchema& DataSchema::with_title(std::string text) {
    title = std::move(text);
    return *this;
}

DataSchema& DataSchema::with_description(std::string text) {
    description = std::move(text);
    return *this;
}

DataSchema& DataSchema::with_unit(std::string text) {
    unit = std::move(text);
    return *this;
}

DataSchema& DataSchema::with_default(json::Value value) {
    default_value = std::move(value);
    return *this;
}

DataSchema& DataSchema::with_format(std::string text) {
    format = std::move(text);
    return *this;
}

DataSchema& DataSchema::with_semantic_type(std::string text) {
    push_semantic_type(semantic_type, std::move(text));
    return *this;
}

json::Value to_json(const DataSchema& s) {
    using detail::put;
    json::Value out = json::Value::object();
    put(out, "@type", s.semantic_type);
    put(out, "description", s.description);
    put(out, "title", s.title);
    if (s.descriptions) {
        out["descriptions"] = detail::pairs_to_json(*s.descriptions);
    }
    if (s.titles) {
        out["titles"] = detail::pairs_to_json(*s.titles);
    }
    put(out, "writeOnly", s.write_only);
    put(out, "readOnly", s.read_only);
    if (s.one_of) {
        json::Value branches = json::Value::array();
        for (const auto& branch : *s.one_of) {
            branches.push_back(to_json(branch));
        }
        out["oneOf"] = std::move(branches);
    }
    put(out, "unit", s.unit);
    if (s.enumeration) {
        out["enum"] = json::Value(*s.enumeration);
    }
    put(out, "format", s.format);
    detail::put_value(out, "const", s.constant, true);
    detail::put_value(out, "default", s.default_value, true);
    if (s.data_type) {
        out["type"] = to_string(*s.data_type);
    }
    if (s.items) {
        if (s.items->tuple) {
            json::Value items = json::Value::array();
            for (const auto& item : s.items->schemas) {
                items.push_back(to_json(item));
            }
            out["items"] = std::move(items);
        } else {
            out["items"] = to_json(s.items->schemas.at(0));
        }
    }
    put(out, "maxItems", s.max_items);
    put(out, "minItems", s.min_items);
    detail::put_value(out, "minimum", s.minimum, false);
    detail::put_value(out, "maximum", s.maximum, false);
    detail::put_value(out, "exclusiveMinimum", s.exclusive_minimum, false);
    detail::put_value(out, "exclusiveMaximum", s.exclusive_maximum, false);
    detail::put_value(out, "multipleOf", s.multiple_of, false);
    if (s.properties) {
        json::Value properties = json::Value::object();
        for (const auto& [name, schema] : *s.properties) {
            properties[name] = to_json(schema);
        }
        out["properties"] = std::move(properties);
    }
    if (s.required) {
        out["required"] = json::Value(*s.required);
    }
    put(out, "minLength", s.min_length);
    put(out, "maxLength", s.max_length);
    put(out, "pattern", s.pattern);
    put(out, "contentEncoding", s.content_encoding);
    put(out, "contentMediaType", s.content_media_type);
    detail::put_extra(out, s.extra);
    return out;
}

DataSchema data_schema_from_json(const json::Value& value) {
    try {
        return read(value);
    } catch (const detail::ReadError& error) {
        throw InvalidDataSchema(error.message);
    }
}

void push_semantic_type(std::optional<OneOrMany<std::string>>& target, std::string value) {
    if (!target) {
        target.emplace(std::move(value));
        return;
    }
    auto all = target->values();
    all.push_back(std::move(value));
    target.emplace(std::move(all));
}

}  // namespace teta::wot::td
