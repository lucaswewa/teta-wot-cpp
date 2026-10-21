# Phase 1: JSON, TD and schemas

Status: **done** (2026-10-22). Plan: [§5, Phase 1](../PLAN.md#phase-1-json-td-and-schemas-m).

## What was built

| Deliverable | Where |
|---|---|
| The TD 1.1 model, written and read in teta-wot's order: DataSchema, forms, links, security schemes, `@context`, the three affordances, the Thing Description, with builders and teta-wot's checks (`TdError`) | `include/teta/wot/td/`, `src/td/` |
| The port of teta-wot-td's JSON Schema → DataSchema conversion, rule for rule, with its errors and recursion limit | `td/convert.hpp` |
| Constraints (`ge`, `gt`, `le`, `lt`, `multiple_of`, lengths, `pattern`) written as pydantic does | `td/constraints.hpp` |
| Codecs: a type's schemars-shaped JSON Schema and serde-shaped JSON, for scalars, containers, maps, tuples, described enums and (Boost.PFR) aggregate structs ([ADR-0007](../adr/0007-types-to-schemas.md)) | `codec.hpp` |
| `td::describe`: a TD assembled from a Thing's affordances as teta-wot's runtime assembles it (`TdOptions`: forms, observation, events, links, top-level and invocation forms, profiles, security), `split_docstring`, and the TD `id` (UUIDv5, with its own SHA-1) | `td/describe.hpp` |
| `teta::wot-td-validation`: the vendored W3C TD 1.1 JSON Schema, compiled in, checked with json-schema-validator (teta-wot's `validation` feature) | `td/validation.hpp`, `schema/` |
| `teta_wot_embed`: files compiled in as byte arrays (any size, MSVC included; reused for the docs pages' assets in Phase 8) | `cmake/TetaWotEmbed.cmake` |
| The nine twins on the reference server, and their TDs in both profiles | `conformance/reference-rs/src/lib.rs`, `fixtures/http/tds*.json` |
| The schema corpus: 54 Rust types, schemars' JSON Schema and teta-wot's DataSchema for each | `conformance/reference-rs/src/bin/schema_corpus.rs`, `tests/td/schema_corpus.jsonl` |
| Examples `td_builder` and `schema-to-dataschema` | `examples/` |

## The gate

- **Every twin TD the C++ side can describe equals the reference's, byte for byte, in both wire profiles** (`tests/td/twin_tds_test.cpp`): `mything`, `broken`, `counter`, `types`, `http_probe`, `settings_probe`, `ws_probe`. A mutation (one character of a docstring) fails both profiles, so the comparison is real.
- **Every TD the reference serves validates** against the W3C TD 1.1 JSON Schema (`tests/td/validation_test.cpp`), so the C++ TDs equal to them do too.
- **The conversion reproduces teta-wot's DataSchema for all 54 corpus types** from schemars' own JSON Schema, and **the codecs reproduce it for 53 C++ types** standing for them (`char` has no C++ counterpart).
- **The examples match teta-wot's:** `td_builder` prints exactly what teta-wot's prints (compared byte for byte), and `schema-to-dataschema`'s four DataSchemas equal teta-wot's.
- teta-wot-td's own tests are ported (`tests/td/model_test.cpp`); 66 tests in all, on GCC 13 and on Clang 18 with ASan and UBSan, and in CI on MSVC and clang-cl.

## Findings

- **A one-paragraph doc comment is both title and description**, a longer one splits into the first line and the rest (`split_docstring`); an undocumented affordance's title is its name. This is teta-wot's macro-level rule, now in `td` so the runtime and tests share it.
- **`Option<Struct>` is described differently from `Option<i64>`**: schemars wraps a referenced type in `anyOf`, so the struct's description stays inside the branch. The codecs reproduce this with `referenced` (ADR-0007).
- **schemars treats `Option` fields as optional** (not in `required`) even without `#[serde(default)]`; the codecs do the same.
- **teta-wot's served TD comes from its runtime** (`ThingHandle::thing_description` with `TdOptions`), not from the HTTP layer. `td::describe` ports it to the TD library so Phase 3's runtime and Phase 1's tests use the same code.
- **serde's pretty printer** indents by two spaces (`to_string_pretty`); settings files use four. `json::dump_pretty` takes the indent.
- **Clang 18 crashes** (a segfault in the compiler) instantiating the defaulted `operator==` of the recursive `DataSchema` through `std::pair`; it is written out by hand.
- **json-schema-validator reports a failing `additionalProperties` member at its parent** (`/properties`, naming the member in the message), where Rust's `jsonschema` reports it at `/properties/temperature`; it has no schema path either. Test-only difference; one ported test adapts.

## Deviations from the plan

- **`blob_probe` and `ndarray_probe`**: their TDs need the Blob and `NdArray` types, so they join the gate in Phases 7 and 10. Their fixtures are already captured; they are the last steps of the TD scenarios, so the UUID numbering of the others doesn't depend on them.
- **The TD `id` derivation** (`td::thing_id`) is in the TD library rather than the HTTP binding (teta-wot has it in `teta-wot-http`); it is pure, and the gate needs it.
- **Builders return `T&`** for chaining (not the `&&`-qualified style first written, which forced self-moves); `build()` moves the result out.
- **The C++ twins are TD-only descriptions** (`td::ThingInfo` filled by hand in the test, as the runtime will fill it from a Thing's definition in Phase 3).
