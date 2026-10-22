// JSON values, and their serialisation exactly as teta-wot writes them.
//
// Values are nlohmann's `ordered_json`, which keeps object keys in insertion
// order, as teta-wot's `serde_json` (with `preserve_order`) does. They are
// never written with nlohmann's `dump()`: `dump` and `dump_pretty` reproduce
// `serde_json` 1.0.151, byte for byte (ADR-0006):
//
// - floats in their shortest round-trip form, as decimals when the decimal
//   exponent is in [-5, 15] (`1.0`, `0.00001`, `1000000000000000.0`), in
//   scientific notation otherwise, with a signed exponent (`1e+16`, `1e-7`,
//   `1.5e+300`);
// - strings with only `"`, `\` and control characters escaped (`\b`, `\t`,
//   `\n`, `\f`, `\r`, and `\u00XX` with lowercase hex for the others);
//   non-ASCII text is written as UTF-8;
// - `dump_pretty` as `serde_json`'s `PrettyFormatter`: two spaces by
//   default (`to_string_pretty`), four for teta-wot's settings files.
//
// `tests/json/serde_json_corpus.txt`, written by `serde_json` itself
// (`conformance/reference-rs`, `json-corpus`), is the specification.
//
// `parse` reads JSON as `serde_json::from_slice` does, which is how teta-wot
// reads request bodies: the same values (`-0` a float, integers beyond 64
// bits as floats, serde_json's own decimal arithmetic) and the same errors,
// at the same positions. `tests/http/wire_corpus.jsonl` (`wire-corpus`) is
// its specification.

#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace teta::wot::json {

/// A JSON value. Object keys keep their insertion order.
using Value = nlohmann::ordered_json;

/// The compact text of a value, as `serde_json::to_string` writes it.
///
/// Throws `std::invalid_argument` for values JSON can't hold: non-finite
/// floats, binary values and discarded values.
std::string dump(const Value& value);

/// The indented text of a value, as `serde_json`'s `PrettyFormatter` writes
/// it with `indent` spaces (two, as `to_string_pretty`; settings files use
/// four).
std::string dump_pretty(const Value& value, std::size_t indent = 2);

/// Appends a finite double as `serde_json` writes it.
void append_double(std::string& out, double value);

/// Appends a string as a JSON string literal, as `serde_json` writes it.
void append_string(std::string& out, std::string_view text);

/// A text `parse` refuses, with serde_json's message and position.
class ParseError : public std::runtime_error {
  public:
    ParseError(std::string message, std::size_t line, std::size_t column);

    /// serde_json's message, without the position (`expected value`).
    const std::string& message() const { return message_; }
    /// The 1-based line, and the byte in it (0 before its first byte).
    std::size_t line() const { return line_; }
    std::size_t column() const { return column_; }

  private:
    std::string message_;
    std::size_t line_;
    std::size_t column_;
};

/// Parses a JSON text (any bytes) as `serde_json::from_slice::<Value>`:
/// positive integers that fit 64 bits are unsigned, negative ones that fit
/// are signed, and other numbers are doubles (`-0` too); a repeated key
/// keeps its first place and its last value; nesting stops at 128 levels.
/// Throws ParseError.
Value parse(std::string_view text);

/// Where teta-wot locates a parse error in its 422: the character offset of
/// `line` and `column` in `text`, counting characters of `text` read as UTF-8
/// (each invalid sequence one character) in the lines before, and bytes in
/// the line (teta-wot-http's `char_offset`).
std::size_t character_offset(std::string_view text, std::size_t line, std::size_t column);

}  // namespace teta::wot::json
