# Phase 6: Observation and events

Status: **done** (2026-10-22). Plan: [§5, Phase 6](../PLAN.md#phase-6-observation-and-events-m). Clients observe properties, actions and events over a Thing's WebSocket and over server-sent events, as they do teta-wot's, and a slow client can't make the server buffer without limit.

## What was built

| Deliverable | Where |
|---|---|
| Transport-neutral WebSockets: `WebSocketPeer` (the transport's side: send, close, readiness) and `WebSocketSession` (the binding's: text, binary, closed); `HttpBinding::is_websocket` and `open_websocket` | `http/websocket.hpp`, `http/binding.hpp` |
| The WebSocket dialect, a port of teta-wot-http's `observe.rs`: one subscription per connection, `addPropertyObservation`, `addActionObservation`, `addEventSubscription`, `propertyStatus`, `actionStatus`, `event`, the Web Thing Protocol's error responses, bad messages logged and ignored, the close (1001) at shutdown | `src/http/websocket.hpp`, `src/http/websocket.cpp` |
| Drogon: a regex WebSocket controller for the binding's Things (`ThingSockets`), and a pre-routing advice that answers other WebSocket requests as the binding answers their path (`answer_upgrades`: `mount` claims `{prefix}/…/ws`, `ThingServer` every request); WebSocket messages up to 64 MiB, as tungstenite reads | `drogon.hpp`, `src/drogon/adapter.cpp`, `src/server/thing_server.cpp` |
| Backpressure: `StreamSink` and `WebSocketPeer` say whether the client keeps up (`ready`, `set_resume`); SSE streams and sockets take no more notifications while it doesn't, and the broker drops what doesn't fit (with its warning). On Drogon, a flow gate counts what is sent and checks the connection's `bytesSent()` on its loop once more than 256 KiB may be unsent, until less than 64 KiB is | `http/message.hpp`, `src/http/sse.cpp`, `src/drogon/adapter.cpp` |
| `TestClient::websocket` and `TestWebSocket` (`send_json`, `send_text`, `receive_json`, `receive_json_within`, `closed`, `close`), deferred from Phase 4; served in-process, straight to the binding's session | `testing.hpp`, `src/server/testing.cpp` |
| `EventProbe`, a twin with events (none of the `wot` twins has any), in the reference crate and the C++ twins | `conformance/reference-rs/src/lib.rs`, `conformance/twins/` |
| `run.py`: a WebSocket client and an SSE client (standard library only), and their steps (`websocket`, `socket` with `send`, `receive`, `quiet`, `closed`, `close`; `events`, `stream` with `receive`, `ended`); SSE `id`s (nanoseconds) masked as timestamps | `conformance/run.py` |
| Scenarios and fixtures: `ws`, `sse`, `sse-wot`, `events`; example scenarios `observe-websocket`, `events`, `sse-observe` | `conformance/scenarios/`, `conformance/fixtures/` |
| Three examples: `observe-websocket`, `sse-observe`, `events` | `examples/` |

## The gate

- **The WebSocket fixtures replay with no deltas** over real sockets (`conformance.http`): `ws` (property and action observation through each ending, 403 and 404 refusals and the rest of a message skipped, bad messages ignored, a duplicate subscription, two sockets, a plain `GET` and upgrades for other paths, the close at shutdown) and `events`. The plan's `fixtures/ws/ws.json` is `fixtures/http/ws.json`.
- **SSE equals the reference's**: `sse` (a property, all properties, an event, all events, `Accept` among other ranges, a functional property read instead of streamed, the end at shutdown) and `sse-wot` (named events with `id`s).
- **The examples**: `observe-websocket`, `sse-observe` and `events` print what their Rust originals print (but for timestamps, readings and the page's line ends), and, served, answer as teta-wot's examples do (`conformance.examples`).
- **teta-wot-http's `observe.rs` tests are ported**: the relayed messages, the error responses, and its proptest of the dialect (2,000 random messages); with tests of observation through `TestClient`, refusals, bad messages, the close at shutdown, and a slow client (10 in all).
- **Backpressure, over a real socket** (by hand): a client that stopped reading while 30,000 writes published notifications received the first 4,624 (about 256 KiB), the next 25,376 were dropped with the broker's warning, and once it read, a new write reached it at once.
- 325 tests pass with `windows-msvc`. Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now.

## Findings

- **trantor never calls a connection's write-complete callback** (it stores it, but `writeCallback` doesn't call it), and `bytesSent()` is the loop's to read; the flow gate polls on the loop while congested instead. Drogon uses neither callback.
- **Drogon answers a WebSocket request that matches no controller with its own HTML 404**, before any handler; pre-routing advices do run for WebSocket requests, so the binding answers them (teta-wot routes them as plain requests: an upgrade for a property's path is a `GET` of the property).
- **Drogon's 101** carries `content-type: text/html; charset=utf-8` and no `vary`; **Drogon pings** every WebSocket every 30 s. Neither is compared (transport).
- **Neither server answers a client's Close frame with its own**: both close the connection.
- **An event's route always streams**, with or without `Accept: text/event-stream`, as does `{thing}/events`.
- The Rust examples' pages are embedded from a CRLF checkout on Windows (git's `autocrlf`); the C++ examples serve the repository's LF text, so the example scenarios don't compare pages.

## Deviations from the plan

- **`TestClient::websocket` is in-process**: teta-wot's serves its router on a loopback port for the socket, which Drogon (one run per process) can't; the test socket talks to the binding's session directly.
- **The backpressure threshold is the flow gate's** (drops start once about 256 KiB is unsent), not hyper's buffering; which notification a slow client misses first can differ.
- **`mount` claims only `{prefix}/…/ws` upgrades**, so an application's own WebSocket controllers keep their paths; `ThingServer`, which owns the application, answers every upgrade as teta-wot does.
- **`EventProbe`** is a new twin, in both implementations, since none of the `wot` twins has events.
