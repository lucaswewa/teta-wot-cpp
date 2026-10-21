# ADR-0005: Concurrency: actions on threads only

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D5

## Context

teta-wot's actions are `async fn` on tokio. C++20 has coroutines, but no standard task type, executor or cancellation. Lab servers run tens of concurrent invocations, and their driver code blocks (vendor SDKs, serial ports).

## Decision

- **Thing code runs on threads.** Actions and functional properties run on a growable worker pool (with a high, configurable cap, so nested in-process calls never starve each other); cancellation is `std::stop_token`; device actors are `std::jthread`s. `ActionCtx::sleep`, `check_cancelled` and `cancelled` give cooperative cancellation.
- **No coroutines**, in the authoring API or inside the library. Drogon's I/O loops never run Thing code; the adapter uses Drogon's callback API.
- Streaming connections (SSE, WebSocket, MJPEG viewers) are callback-driven and never hold a thread each.

## Consequences

- Thing authors write ordinary functions; `blocking` is accepted for parity but changes nothing.
- Adding coroutine actions later would need the runtime's internal action interface changed; this was weighed and accepted.
