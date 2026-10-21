// Validation against the W3C TD 1.1 JSON Schema (the `teta::wot-td-validation`
// target, built with TETA_WOT_TD_VALIDATION; teta-wot's `validation` feature).
//
// The schema is vendored in `schema/td-json-schema-validation.json`: the
// W3C-hosted copy at https://www.w3.org/2022/wot/td-schema/v1.1, version
// `1.1-12-March-2025`, the same file teta-wot vendors.

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/thing.hpp"

namespace teta::wot::td::validation {

/// The vendored W3C TD 1.1 JSON Schema, as text.
std::string_view td_json_schema();

/// One way in which a TD breaks the W3C JSON Schema.
struct SchemaViolation {
    /// Where in the TD, as a JSON pointer.
    std::string instance_path;
    /// What is wrong.
    std::string message;
};

/// A TD doesn't validate against the W3C TD 1.1 JSON Schema.
class SchemaValidationError : public std::runtime_error {
  public:
    explicit SchemaValidationError(std::vector<SchemaViolation> violations);
    const std::vector<SchemaViolation>& violations() const { return violations_; }

  private:
    std::vector<SchemaViolation> violations_;
};

/// Every way in which TD JSON breaks the schema (empty if it is valid).
std::vector<SchemaViolation> violations(const json::Value& td);

/// Validates TD JSON; throws SchemaValidationError.
void validate_json(const json::Value& td);

/// Writes a TD and validates it; throws SchemaValidationError.
void validate_schema(const ThingDescription& td);

}  // namespace teta::wot::td::validation
