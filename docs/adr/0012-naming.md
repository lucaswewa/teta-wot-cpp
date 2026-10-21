# ADR-0012: Naming

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D12

## Context

The project needs names for its namespace, headers, CMake package and targets.

## Decision

- Project `teta-wot-cpp`; namespace `teta::wot`; headers under `<teta/wot/…>`, with the umbrella header `<teta/wot.hpp>`.
- CMake package `teta-wot`; targets `teta::wot` (what applications link), `teta::wot-json`, and later `teta::wot-td`, `teta::wot-core`, `teta::wot-http`, `teta::wot-drogon`, `teta::wot-server`.

## Consequences

- Examples keep teta-wot's names; their executables are `example-<name>`.
