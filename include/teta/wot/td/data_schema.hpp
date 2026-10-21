// TD `DataSchema`: the TD's JSON-Schema-like description of data.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/one_or_many.hpp"

namespace teta::wot::td {

/// Human-readable text in several languages, keyed by language tag, in order
/// (`titles`, `descriptions`).
using MultiLanguage = std::vector<std::pair<std::string, std::string>>;

/// The `type` of a DataSchema. TD 1.1 allows exactly one; a JSON Schema
/// `type` array is converted to `oneOf` (see convert.hpp).
enum class DataType { boolean, integer, number, string, object, array, null };

/// The name used in JSON, such as `"integer"`.
const char* to_string(DataType type);

/// The type named `name`, if it is one.
std::optional<DataType> data_type_from_string(std::string_view name);

struct DataSchema;

/// The `items` of an array schema: one schema for every item, or one schema
/// per position (a tuple).
struct ArrayItems {
    /// Whether the schemas are per position (`[...]`) rather than one for all.
    bool tuple = false;
    /// The schemas: exactly one unless `tuple`.
    std::vector<DataSchema> schemas;

    bool operator==(const ArrayItems&) const;
};

/// A TD `DataSchema` (TD 1.1 §5.3.2), including the terms of its array,
/// number, integer, object and string subclasses.
///
/// Members are in the order teta-wot writes them. Terms TD 1.1 doesn't
/// define, such as JSON Schema's `additionalProperties`, are kept in `extra`
/// and written after the typed terms. A `null` `default` or `const` is never
/// written. Numbers keep their JSON kind (integer or float).
struct DataSchema {
    std::optional<OneOrMany<std::string>> semantic_type;  // `@type`
    std::optional<std::string> description;
    std::optional<std::string> title;
    std::optional<MultiLanguage> descriptions;
    std::optional<MultiLanguage> titles;
    std::optional<bool> write_only;
    std::optional<bool> read_only;
    std::optional<std::vector<DataSchema>> one_of;
    std::optional<std::string> unit;
    std::optional<std::vector<json::Value>> enumeration;  // `enum`
    std::optional<std::string> format;
    std::optional<json::Value> constant;       // `const`
    std::optional<json::Value> default_value;  // `default`
    std::optional<DataType> data_type;         // `type`
    std::optional<ArrayItems> items;
    std::optional<std::uint64_t> max_items;
    std::optional<std::uint64_t> min_items;
    std::optional<json::Value> minimum;
    std::optional<json::Value> maximum;
    std::optional<json::Value> exclusive_minimum;
    std::optional<json::Value> exclusive_maximum;
    std::optional<json::Value> multiple_of;
    std::optional<std::vector<std::pair<std::string, DataSchema>>> properties;
    std::optional<std::vector<std::string>> required;
    std::optional<std::uint64_t> min_length;
    std::optional<std::uint64_t> max_length;
    std::optional<std::string> pattern;
    std::optional<std::string> content_encoding;
    std::optional<std::string> content_media_type;
    /// Terms TD 1.1 doesn't define for a DataSchema, in order. Keys must not
    /// repeat a typed term.
    json::Value extra = json::Value::object();

    bool operator==(const DataSchema&) const;

    /// A schema with no terms, which accepts any value.
    static DataSchema any() { return {}; }

    /// A schema for one JSON type, with no other terms.
    static DataSchema of(DataType type);

    DataSchema& with_title(std::string title);
    DataSchema& with_description(std::string description);
    DataSchema& with_unit(std::string unit);
    DataSchema& with_default(json::Value value);
    DataSchema& with_format(std::string format);
    DataSchema& with_semantic_type(std::string semantic_type);
};

/// A DataSchema as JSON, in teta-wot's order.
json::Value to_json(const DataSchema& schema);

/// JSON that doesn't fit the DataSchema model.
class InvalidDataSchema : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Reads a DataSchema from JSON, as teta-wot's model does. Throws
/// InvalidDataSchema for JSON that doesn't fit.
DataSchema data_schema_from_json(const json::Value& value);

/// Adds `value` to an `@type` term, keeping a single annotation as a string.
void push_semantic_type(std::optional<OneOrMany<std::string>>& target, std::string value);

}  // namespace teta::wot::td
