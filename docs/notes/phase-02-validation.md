# Phase 2: Validation

Status: **done** (2026-10-22). Plan: [§5, Phase 2](../PLAN.md#phase-2-validation-m).

## What was built

| Deliverable | Where |
|---|---|
| `SchemaValidator`: a port of teta-wot-core's `validate.rs`, rule for rule: pydantic's lax coercions, error types, `loc`, `msg`, `input` and `ctx`, the order errors are found in, unions (exact type first, then lax, branch tags), literals with Python's numeric equality, models, maps, tuples, `extra_forbidden` ([ADR-0008](../adr/0008-validation.md)) | `include/teta/wot/validate.hpp`, `src/core/validate.cpp` (`teta::wot-core`) |
| `ValidationError` and `ValidationIssue`, written as FastAPI writes them, with teta-wot's `Display` text; `missing`, `value_error` and `decode_error` (teta-wot's `from_serde`) | `validate.hpp` |
| The coercions on their own (`coercion::integer`, `number`, `boolean`, `string`), for the runtime's parameter decoding | `validate.hpp` |
| The validation corpus: teta-wot's validator over 47 schemas (primitives, bounds, patterns, arrays, tuples, models, maps, unions, literals, the codec corpus's structs and enums) and 8,413 inputs (edge cases, most from pydantic captures in the `wot` repository, and 60 seeded random values per schema) | `conformance/reference-rs/src/bin/validation_corpus.rs`, `tests/validate/validation_corpus.jsonl` |
| The `validation` HTTP scenario: property writes and invocations with good and bad bodies on `types` and `mything`, including bodies that aren't JSON | `conformance/scenarios/http/validation.json`, `fixtures/http/validation.json` |

## The gate

- **The randomised comparison:** for all 8,413 corpus inputs the C++ validator's result (the coerced value, or the errors) equals teta-wot's, as text, byte for byte (`tests/validate/validation_corpus_test.cpp`). CI regenerates the corpus from teta-wot and fails if it changes.
- **The reference's refusals:** every value-level 422 the reference answered in the `validation` scenario (32 steps: properties and action inputs, nested locations, unions, tuples, maps, extra fields) is reproduced byte for byte by `SchemaValidator` from the affordance's DataSchema in the TD fixtures (`tests/validate/http_validation_test.cpp`). The rest of the scenario (the HTTP status of accepted writes, a `null` or empty body, malformed JSON) is the HTTP binding's and is replayed in Phase 4.
- teta-wot-core's `tests/validate.rs` is ported (`tests/validate/validate_test.cpp`); 87 tests in all on GCC 13 and on Clang 18 with ASan and UBSan, and in CI on MSVC and clang-cl.

## Findings

- **Patterns:** RE2 and Rust's `regex` share their syntax, but Rust's `\d`, `\w` and `\s` are Unicode-aware and RE2's are ASCII: `"١٢٣"` matches `^\d+$` in teta-wot. The validator rewrites them as Unicode classes before compiling (`rust_regex_for_re2`). The random inputs found this.
- **Messages print numbers as Rust's `Display` does** (`1e20` as `100000000000000000000`, no exponent ever), which is how pydantic-core prints them; `rust_display` reproduces it. String lengths are counted in Unicode scalar values, and `str::trim` trims Unicode White_Space, not only ASCII.
- **Literal equality is serde's**, not nlohmann's: `1` and `1.0` are different JSON values except where pydantic's numeric equality applies.
- **The HTTP layer reads a `null` body as missing** (`{"type": "missing", "loc": ["body"], "input": null}`), even for a nullable property, and a malformed body as `json_invalid` with serde_json's error text and a `loc` of `["body", <byte offset>]`. Both are Phase 4's (the scenario captures them).
- **serde_json parsing differs from nlohmann's** in ways the HTTP binding must reproduce: `-0` is the float `-0.0`; `1e400` is an error ("number out of range") and `1e-400` is `0.0`; integers beyond `u64` become floats; with duplicate keys the last value wins at the first key's position; and the error texts and positions of `json_invalid`. Phase 4 needs a serde-compatible body parser.

## Deviations from the plan

- **Rust-only regex syntax** (nested classes, set operations such as `[a-z&&[^aeiou]]`), `\b`, and `\W`/`\S` inside a bracketed class are not translated; such patterns behave as RE2 reads them. The word class is letters, marks, digits and connector punctuation, which is Rust's except for a few Alphabetic non-letters (such as Roman numerals) and joiners. None of the twins' patterns use these.
- **The JSON body parser** (serde_json's number and duplicate-key rules, its error messages) moves to Phase 4 with the HTTP binding, where `json_invalid` is produced.
- **`decode_error`'s message** is the C++ decoder's text, not serde's (`invalid type: string "x", expected i64`); it only appears when a schema allows a value its type doesn't, which the codecs are built to prevent.
