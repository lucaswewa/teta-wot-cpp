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

## Implementation (Phase 3)

- `tw::log::info("moved to {}", x)` (and `trace`, `debug`, `warn`, `error`) formats with `std::format`; the format string carries the call's `std::source_location`, which gives the record's `filename` and `lineno`.
- An event emitted inside an invocation's scope (the action's thread, a thread that carries the scope, or a device call) is captured into that invocation's log at INFO and above (DEBUG in debug mode, `log::init(true)`); child invocations have their own logs. Records are teta-wot's (`message`, `levelname`, `levelno`, `lineno`, `filename`, `created`, `exception_type`, `traceback`).
- Every event also goes to the sink, if any (`log::set_sink`; `log::console_sink()` writes to standard error).
- **The spdlog sink** (`teta/wot/log_spdlog.hpp`, header-only; vcpkg feature `spdlog`) forwards an spdlog logger's messages into the same path, so driver code that logs with spdlog is captured (the `invocation-logs` example). teta-wot-cpp itself doesn't depend on spdlog.
- Structured fields (`tracing`'s `strokes = 3`) are formatted into the message: teta-wot writes them as ` key=value` after it.
