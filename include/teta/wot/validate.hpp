// Validating JSON values against a TD DataSchema, as pydantic does in lax
// mode: a port of teta-wot-core's `validate` (ADR-0008).
//
// teta-wot validates request bodies as LabThings' pydantic does, so clients
// see pydantic's coercions (the string `"2"` is accepted for an integer) and
// its errors (`{"type": "int_parsing", "loc": ["body", "n"], "msg": ...,
// "input": ...}`). `SchemaValidator` reproduces both from the DataSchema that
// describes the value, and returns the coerced value, which then decodes into
// the C++ type. `tests/validate/validation_corpus.jsonl`, written by
// teta-wot's own validator, is the specification.

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/data_schema.hpp"

namespace re2 {
class RE2;
}

namespace teta::wot {

/// One step of an error's location: an object key or an array index.
class LocItem {
  public:
    LocItem(std::string key) : item_(std::move(key)) {}
    LocItem(const char* key) : item_(std::string(key)) {}
    LocItem(std::size_t index) : item_(index) {}

    bool is_index() const { return std::holds_alternative<std::size_t>(item_); }
    /// The key, or the index in decimal (as it is shown in messages).
    std::string to_string() const;
    json::Value to_json() const;

    bool operator==(const LocItem&) const = default;

  private:
    std::variant<std::string, std::size_t> item_;
};

using Loc = std::vector<LocItem>;

/// One validation error, in pydantic's shape.
struct ValidationIssue {
    /// The error type, such as `int_parsing` or `missing`.
    std::string type;
    /// Where the error is.
    Loc loc;
    /// pydantic's message.
    std::string msg;
    /// The value that failed.
    json::Value input;
    /// Extra context, such as the bound that was exceeded.
    std::optional<json::Value> ctx;

    bool operator==(const ValidationIssue&) const = default;
};

/// The issue as pydantic writes it: `type`, `loc`, `msg`, `input`, `ctx`.
json::Value to_json(const ValidationIssue& issue);

/// A value didn't validate. Holds every error found, in order.
class ValidationError : public std::exception {
  public:
    explicit ValidationError(std::vector<ValidationIssue> issues);

    const std::vector<ValidationIssue>& issues() const { return issues_; }
    /// "N validation error(s)" and one line per issue, as teta-wot's Display.
    const char* what() const noexcept override { return text_.c_str(); }

    /// A single error.
    static ValidationError single(ValidationIssue issue);
    /// pydantic's error for a required body that is absent.
    static ValidationError missing(Loc loc, json::Value input);
    /// pydantic's `value_error`, raised by a validator: `Value error, ...`,
    /// with an empty `ctx.error`.
    static ValidationError value_error(Loc loc, std::string_view message, json::Value input);
    /// A value that validated but didn't decode into the C++ type (the schema
    /// allowed something the type doesn't): `value_error` without `ctx`, as
    /// teta-wot's `from_serde`.
    static ValidationError decode_error(Loc loc, std::string_view message, json::Value input);
    /// pydantic's `value_error` for a value that its type's decoding refused
    /// (a Blob that isn't on the server): at `path` below `loc`, with the
    /// value that `input` (as the client sent it) has there.
    static ValidationError value_error_at(Loc loc,
                                          const std::vector<std::variant<std::string, std::size_t>>& path,
                                          std::string_view message, const json::Value& input);

  private:
    std::vector<ValidationIssue> issues_;
    std::string text_;
};

/// The coercions pydantic applies in lax mode, as teta-wot measured them
/// against pydantic 2. Each returns the coerced value, or the error type and
/// message.
namespace coercion {

/// An error type and message.
struct Failure {
    const char* type;
    const char* msg;
    bool operator==(const Failure& other) const {
        return std::string_view(type) == other.type && std::string_view(msg) == other.msg;
    }
};

using Result = std::variant<json::Value, Failure>;

/// An integer: integers, whole floats, `true`/`false`, and strings of
/// digits (surrounding whitespace, a sign, `_` between digits and a fraction
/// of zeros such as `.00` allowed; `2.` and `1e3` are not).
Result integer(const json::Value& value);

/// A number: integers and floats (always returned as a float), `true` and
/// `false`, and strings that parse as numbers (`_` between digits allowed;
/// `"inf"` and `"nan"` parse but JSON can't carry them: `float_parsing`).
Result number(const json::Value& value);

/// A boolean: booleans, 0 and 1, and pydantic's spellings (case-insensitive
/// `0`, `off`, `f`, `false`, `n`, `no`, `1`, `on`, `t`, `true`, `y`, `yes`).
Result boolean(const json::Value& value);

/// A string: only strings.
Result string(const json::Value& value);

}  // namespace coercion

/// A `pattern` isn't a valid regular expression.
class InvalidPattern : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};

/// A DataSchema prepared for validation (its `pattern`s compiled with RE2).
class SchemaValidator {
  public:
    /// Prepares `schema`; throws InvalidPattern.
    explicit SchemaValidator(td::DataSchema schema);

    const td::DataSchema& schema() const { return schema_; }

    /// Validates `value`, returning it with pydantic's coercions applied.
    /// Errors are located under `loc` (such as `["body"]`); throws
    /// ValidationError.
    json::Value validate(const json::Value& value, const Loc& loc) const;

  private:
    td::DataSchema schema_;
    std::shared_ptr<const std::map<std::string, std::shared_ptr<re2::RE2>>> patterns_;
};

/// A finite or non-finite double as Rust's `Display` writes it, which is how
/// pydantic-core writes numbers in messages: no exponent ever (`1`, `0.5`,
/// `100000000000000000000`, `0.0000001`), `inf`, `NaN`.
std::string rust_display(double value);

}  // namespace teta::wot
