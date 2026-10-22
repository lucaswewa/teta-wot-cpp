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

## Implementation (Phase 3)

- **The worker pool** (`WorkerPool`, one per runtime) runs invocations and child invocations; it grows when a task finds no idle thread, up to 1024, and keeps its threads. The HTTP binding will post getters, setters and actions to it (Phase 4).
- **Invocation scopes** are thread-local: the scope (ID, lock owner, cancel token, log) is entered for the duration of the action's call, and code that starts threads carries it with `scope.run(...)`. Device calls run in their caller's scope on the device thread.
- **Cancellation is teta-wot's resettable flag**, not `std::stop_token`: observing it consumes it (an action may handle it and carry on, and see a later one), which a stop token can't express. `ActionCtx::sleep` is a condition-variable wait on the token. Waiting for a child invocation or the parent's cancellation, whichever comes first, uses the token's callbacks.
- **Device actors** are a thread per device with a bounded mailbox; the driver lives and dies on its thread.
- What were `select!`s and paused clocks in teta-wot's tests are condition variables, an injectable clock (`RuntimeBuilder::clock`) and short real timeouts ([Phase 3 note](../notes/phase-03-runtime.md)).
