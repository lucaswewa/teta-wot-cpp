// Conversion from JSON Schema (draft 2020-12, as schemars generates it, and
// as the codecs of codec.hpp generate it) to TD `DataSchema`: a port of
// teta-wot-td's `convert`, rule for rule.
//
//  1. `$ref` is inlined (local references only); keywords next to a `$ref`
//     override the referenced schema's. Each reference followed, and each
//     level of nesting, counts towards a recursion limit (99).
//  2. `anyOf` becomes `oneOf`; having both is an error.
//  3. `prefixItems` becomes `items` (a tuple); an `items: false` next to it
//     is dropped, any other `items` is an error.
//  4. A `type` array becomes `oneOf`, with each type's keywords moved into
//     its branch, `enum`/`const` values sent to the branch of their type,
//     and redundant types removed (`number` includes `integer`).
//  5. `$defs`, `definitions`, `$schema` and `$id` are removed.
//  6. schemars' numeric `format`s (`int32`, `double`, ...) are removed.
//  7. A `null` `default` is removed; `const: null` becomes `type: "null"`.
//  8. A single-element `allOf` is merged into the schema around it.
//  9. `true` becomes `{}` where TD expects a DataSchema, `false` is an error
//     there; booleans are kept under keywords TD doesn't define.
// 10. Keywords are sorted alphabetically in every converted schema (member
//     names inside `properties` keep their order).

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "teta/wot/json.hpp"
#include "teta/wot/td/data_schema.hpp"

namespace teta::wot::td {

/// The default recursion limit of the conversion.
inline constexpr std::size_t default_recursion_limit = 99;

/// Options for the conversion.
struct ConversionOptions {
    /// How many levels of nesting, plus references followed, are allowed.
    std::size_t recursion_limit = default_recursion_limit;
};

/// A JSON Schema can't be converted to a DataSchema.
class ConversionError : public std::runtime_error {
  public:
    enum class Kind {
        recursion_limit,
        non_local_reference,
        unresolved_reference,
        unsupported,
        conflict,
        false_schema,
        not_a_schema,
        invalid_data_schema,
    };

    ConversionError(Kind kind, std::string message) : std::runtime_error(std::move(message)), kind_(kind) {}

    Kind kind() const { return kind_; }

  private:
    Kind kind_;
};

/// Converts a JSON Schema document to DataSchema JSON, without checking it
/// against the DataSchema model.
json::Value convert(const json::Value& schema, const ConversionOptions& options = {});

/// Converts a JSON Schema document to a DataSchema.
DataSchema from_json_schema(const json::Value& schema, const ConversionOptions& options = {});

}  // namespace teta::wot::td
