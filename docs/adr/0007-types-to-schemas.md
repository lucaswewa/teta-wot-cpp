# ADR-0007: Types to schemas: Codec<T>

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D7

## Context

teta-wot derives each type's JSON Schema with schemars, and its TDs and OpenAPI documents are made from those schemas. C++20 has no reflection; field names of aggregates are available through Boost.PFR on all three compilers.

## Decision

- **`teta::wot::Codec<T>`** is the customisation point: a type's JSON Schema, encoder and decoder. Built in for scalars, strings, `std::optional`, `std::vector`, `std::array`, `std::map`, `std::tuple`, `std::variant`, enums, `Blob`, `NdArray`.
- **Aggregate structs are reflected with Boost.PFR** (field names); `teta::wot::describe<T>` adds titles, descriptions and constraints.
- **The schemas must equal schemars'** for the corresponding Rust type, checked per type against output generated in `conformance/`.

## Consequences

- Phase 1 delivers it; Boost.PFR joins `vcpkg.json` then.

## Implementation (Phase 1)

What building it settled, within the decision above:

- **Codecs generate inline JSON Schemas**, not schemars' `$defs` and `$ref`s. Conversion inlines references and sorts every schema's keys, so the resulting DataSchema is the same; `tests/td/schema_corpus.jsonl` (54 Rust types, written by teta-wot v0.1.0 itself) proves it type by type.
- **`Codec<T>::referenced`** says whether schemars would refer to the type (structs and enums). It changes one thing: making it optional wraps it in `anyOf` with `null` (schemars' `allow_null`), where an inline schema gets `null` added to its `type`. This is visible in the DataSchema (where a struct's `description` ends up), so it is part of the contract.
- **Recursive structs**: a struct that contains itself is the one case where a `$ref` is generated (through a per-call `codec::Generator`), so the conversion stops at its recursion limit with the same error as teta-wot's.
- **What Rust reads from doc comments and serde attributes** is said in C++ by:
  - a struct's static `describe(StructInfo<T>&)` (or a `Describe<T>` specialisation for types that can't be changed): its description, `deny_unknown_fields`, and per field a description and `defaulted()` (`#[serde(default)]`; the default is the field's value in a value-initialised struct);
  - `Describe<E>::describe(EnumInfo<E>&)` for enums: each value's name, optionally with a description (documented variants make a `oneOf` of `const`s, as schemars does), and the enum's description.
- **A field is required** unless it is `defaulted()` or a `std::optional` (schemars treats `Option` fields as optional).
- **Built-in codecs** cover `bool`, every integer (with schemars' `format` and bounds for its size), `float` (written as its shortest decimal, as serde writes an `f32`), `double`, `std::string`, `std::monostate` (Rust's `()`), `json::Value` (any value, the schema `true`), `std::optional`, `std::vector`, `std::array`, `std::pair`, `std::tuple`, maps from strings (`std::map`, `std::unordered_map`, `OrderedMap` for Rust's `IndexMap`) and `NoInput`. A user's own `Codec<T>` specialisation is the escape hatch for anything else (the twins' `EmptyInput` uses it).
- `char` has no codec: Rust's `char` is a Unicode scalar, C++'s a byte.
