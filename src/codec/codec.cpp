#include "teta/wot/codec.hpp"

#include <charconv>

namespace teta::wot {

namespace codec {

namespace {

bool has_null_type(const json::Value& schema) {
    if (!schema.is_object()) {
        return false;
    }
    auto type = schema.find("type");
    if (type == schema.end()) {
        return false;
    }
    if (type->is_string()) {
        return type->get<std::string>() == "null";
    }
    if (type->is_array()) {
        for (const auto& t : *type) {
            if (t == "null") {
                return true;
            }
        }
    }
    return false;
}

json::Value null_schema() {
    return {{"type", "null"}};
}

}  // namespace

json::Value allow_null(json::Value schema, bool referenced) {
    if (schema.is_boolean()) {
        return schema.get<bool>() ? schema : null_schema();
    }
    if (schema.size() == 1 && schema.contains("anyOf") && schema["anyOf"].is_array()) {
        for (const auto& branch : schema["anyOf"]) {
            if (has_null_type(branch)) {
                return schema;
            }
        }
    }
    bool immediate_subschema = referenced;
    for (const char* key : {"if", "allOf", "anyOf", "oneOf", "$ref"}) {
        immediate_subschema = immediate_subschema || schema.contains(key);
    }
    if (immediate_subschema) {
        return {{"anyOf", json::Value::array({std::move(schema), null_schema()})}};
    }
    if (auto type = schema.find("type"); type != schema.end()) {
        if (type->is_array()) {
            bool has_null = false;
            for (const auto& t : *type) {
                has_null = has_null || t == "null";
            }
            if (!has_null) {
                type->push_back("null");
            }
        } else if (type->is_string() && type->get<std::string>() != "null") {
            *type = json::Value::array({*type, "null"});
        }
    }
    if (auto c = schema.find("const"); c != schema.end()) {
        const json::Value value = *c;
        schema.erase("const");
        if (!value.is_null()) {
            schema["enum"] = json::Value::array({value, nullptr});
        }
    } else if (auto e = schema.find("enum"); e != schema.end() && e->is_array()) {
        bool has_null = false;
        for (const auto& v : *e) {
            has_null = has_null || v.is_null();
        }
        if (!has_null) {
            e->push_back(nullptr);
        }
    }
    return schema;
}

json::Value object_schema(const std::vector<Field>& fields, const std::optional<std::string>& description,
                          bool deny_unknown_fields) {
    json::Value schema = {{"type", "object"}};
    json::Value properties = json::Value::object();
    json::Value required = json::Value::array();
    for (const auto& field : fields) {
        json::Value member = field.schema;
        if (member.is_boolean() && (field.description || field.default_value)) {
            // `true` with metadata: an object of only the metadata.
            member = json::Value::object();
        }
        if (field.description) {
            member["description"] = *field.description;
        }
        if (field.default_value) {
            member["default"] = *field.default_value;
        }
        properties[field.name] = std::move(member);
        if (field.required) {
            required.push_back(field.name);
        }
    }
    schema["properties"] = std::move(properties);
    if (!required.empty()) {
        schema["required"] = std::move(required);
    }
    if (deny_unknown_fields) {
        schema["additionalProperties"] = false;
    }
    if (description) {
        schema["description"] = *description;
    }
    return schema;
}

}  // namespace codec

json::Value Codec<float>::encode(float value) {
    char buffer[32];
    const auto written = std::to_chars(buffer, buffer + sizeof buffer, value);
    double shortest = 0.0;
    std::from_chars(buffer, written.ptr, shortest);
    return shortest;
}

}  // namespace teta::wot

namespace teta::wot {

json::Value Codec<NoInput>::json_schema() {
    return {
        {"description",
         "Represent the input of an action that never takes parameters.\n\nThis may be either an empty "
         "dictionary or "
         "``None``."},
        {"anyOf",
         json::Value::array({{{"description", "A model representing an object that must have no keys."},
                              {"type", "object"},
                              {"properties", json::Value::object()},
                              {"additionalProperties", false}},
                             {{"type", "null"}}})},
    };
}

}  // namespace teta::wot
