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
