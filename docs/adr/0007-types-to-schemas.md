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
