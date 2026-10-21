# ADR-0010: Logging: a facade with per-invocation capture

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D10

## Context

teta-wot records the `tracing` events emitted while an action runs in its invocation's `log`, with Python's record shape (`levelname`, `levelno`, `lineno`, `filename`, `created`, …).

## Decision

- **`teta::wot::log`**, a small facade (`std::format`, `std::source_location`), with capture through a thread-local invocation scope that threads started by an action can carry.
- An optional **spdlog sink** captures driver code that already logs with spdlog.
- Records have teta-wot's JSON shape.

## Consequences

- Phase 3 delivers it; spdlog joins `vcpkg.json` then, as an optional feature.
