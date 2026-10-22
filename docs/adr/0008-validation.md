# ADR-0008: Validation: port teta-wot's SchemaValidator

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D8

## Context

teta-wot validates inputs as pydantic does in lax mode (coercions such as `"2"` for an integer, and pydantic's 422 error bodies), driven by each value's DataSchema (`teta-wot-core/src/validate.rs`).

## Decision

- **Port `validate.rs` one-to-one**, with its tests, to C++.
- **RE2** for `pattern`: the same syntax family as Rust's `regex` (which pydantic-core also uses), and linear time; `std::regex` is neither.

## Consequences

- Phase 2 delivers it, gated by validation fixtures captured from the reference and a randomised comparison with the Rust validator.

## Implementation (Phase 2)

What building it settled, within the decision above:

- **The specification is teta-wot's validator itself:** `validation-corpus` (in `conformance/reference-rs`) runs it over 47 schemas and 8,413 inputs, edge cases and seeded random values, and the C++ validator must reproduce every result as text. This is the plan's randomised comparison, made reproducible.
- **RE2 with a translation:** Rust's `\d`, `\w` and `\s` are Unicode-aware, RE2's ASCII; patterns are rewritten to Unicode classes before compiling. Rust-only syntax (nested classes, set operations) and `\b` are not translated ([Phase 2 note](../notes/phase-02-validation.md)).
- **Rust's text rules are ported where pydantic-core shows them:** f64 `Display` in messages (`rust_display`), `str::trim`'s Unicode White_Space, lengths in Unicode scalar values, Rust's `f64` and integer parsing for coerced strings, serde's `Value` equality for literals.
- **`ValidationError` is an exception** (`SchemaValidator::validate` returns the coerced value or throws), matching the library's error style ([ADR-0009](0009-errors.md)); teta-wot's `from_serde` is `decode_error`.
- Parsing request bodies as serde_json does (its number rules and `json_invalid` errors) belongs to the HTTP binding (Phase 4).
