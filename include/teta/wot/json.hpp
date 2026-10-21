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

#pragma once

#include <nlohmann/json.hpp>
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

}  // namespace teta::wot::json
