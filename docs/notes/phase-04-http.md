# Phase 4: HTTP binding, Drogon adapter and TestClient

Status: **done** (2026-10-22). Plan: [§5, Phase 4](../PLAN.md#phase-4-http-binding-drogon-adapter-and-testclient-l-first-end-to-end-milestone). The first end-to-end milestone: Things are served over HTTP.

## What was built

| Deliverable | Where |
|---|---|
| Request bodies read as `serde_json::from_slice` reads them: the same values (`-0` a float, integers beyond 64 bits as floats, serde_json's own decimal arithmetic), the same error messages at the same line and column, and teta-wot's character offset for the 422's `loc` | `json.hpp` (`parse`, `character_offset`), `src/json/parse.cpp` |
| Transport-neutral HTTP messages: `Request`, `Response` (a streamed body is a producer that pushes chunks into a `StreamSink`), `Headers` | `http/message.hpp` (in `teta::wot-core`, since endpoints are declared in `ThingSpec`) |
| Custom endpoints: `t.endpoint(method, path, handler)` with `.doc()`, `.name()` and `.link()`; teta-wot's path and duplicate checks; linked endpoints in the TD's `links` | `thing.hpp`, `runtime.hpp` |
| `teta::wot-http`: `HttpBinding`, a port of teta-wot-http's dispatcher: the route table (first match wins, Starlette's trailing-slash redirects, 404 and 405 with `allow`), CORS, the `tetathing` renderer and the `wot` one, request bodies and their 413/422, IDs read as the `uuid` crate reads them, invocation JSON, problems; server-sent events; Thing code on the worker pool | `http/binding.hpp`, `src/http/` |
| `teta::wot-drogon`: the adapter (`mount`, `Handler`, request and response conversion, streamed bodies, the Phase 0 workaround for streams on closing connections) | `drogon.hpp`, `src/drogon/` |
| `teta::wot-server`: `ThingServer` (builder, `serve`, the shutdown sequence), `shutdown_signal` (console events on Windows, SIGINT and SIGTERM elsewhere), `TestClient` and `TestEventStream` | `thing_server.hpp`, `testing.hpp`, `src/server/` |
| The C++ twin server, with teta-wot's command line | `conformance/twin-server-cpp/` |
| Four scenarios and their fixtures: `default` (routing, redirects, 404/405, HEAD, properties, bodies, 413, invocations, cancellation, IDs, CORS, `Host`), `locked` (the global lock), `prefixed` (`/api/v1`) and `wot` (the `wot` profile's answers) | `conformance/scenarios/http/`, `conformance/fixtures/http/` |
| `run.py`: `poll` (with `until`), `body_repeat`, invocation IDs from `location` (the `wot` profile's `ActionStatus` has no `id`), log-record locations masked in `text` too, UTF-8 bodies, executables resolved on Windows | `conformance/run.py` |
| Example scenarios: four examples served by both implementations, compared with `run.py --examples` | `conformance/scenarios/examples/`, `conformance/fixtures/examples/` |
| `wire-corpus`: 8,559 request bodies through serde_json and 38 path IDs through `uuid`, written by the reference's own crates | `conformance/reference-rs/src/bin/wire_corpus.rs`, `tests/http/wire_corpus.jsonl` |
| Seven examples, and `functional-properties` now over HTTP | `examples/` |

## The gate

- **The HTTP fixtures replay with no deltas**, through `TestClient` (`tests/http/fixtures_test.cpp`, a C++ port of `run.py`'s runner and normalisation) and over real sockets (`conformance.http`: `run.py check` against the C++ twin server, run by CTest): `default`, `locked` and `prefixed`, and also `counter`, `validation` (whose malformed-body steps Phase 2 left for this phase) and `wot`. `tds` and `tds-wot` need `blob_probe` and `ndarray_probe` (Phases 7 and 10); their TDs are compared in-process since Phase 1.
- **The differential runner is green**: `run.py diff` of the reference and the C++ twin server on those six scenarios. The reference against itself is green too, so the new fixtures are stable.
- **teta-wot-http's tests are ported and pass**: `http.rs` (19 tests), `cors.rs` (5), `options.rs` (5), and the unit tests of `routes.rs` (3), `problem.rs` (2) and `observe.rs` (`accept_headers`); with tests of the body parser and the ID parser against `wire-corpus`, custom endpoints, server-sent events and where Thing code runs.
- **The examples**: `quickstart-counter`, `light`, `counter-server`, `api-prefix` and `custom-endpoint` print what the Rust examples print (but for IDs); `http-walkthrough` walks through every route over a socket and checks each status; `drogon-integration` serves Things beside an application's own Drogon route. CTest runs them all.
- **The examples equal their Rust originals on the wire** (plan §6.1): served with `--serve`, `quickstart-counter`, `light`, `counter-server` and `http-walkthrough` answer their TD and a scripted session as teta-wot v0.1.0's examples do (`run.py --examples`, fixtures captured from the Rust examples; `conformance.examples` in CTest; the differential run is green too). `api-prefix` and `custom-endpoint` don't serve, in either implementation.
- **Graceful shutdown**: Ctrl-Break during a running action cancels it, and the server exits 0.
- 261 tests pass with `windows-msvc` (the examples included). Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now (`.github/workflows/ci.yml`).

## Findings

- **serde_json's decimals aren't always correctly rounded.** Without `float_roundtrip` (teta-wot doesn't enable it) serde_json makes a double from the significand and a power of ten from a table, multiplying or dividing once. That is ported exactly; a correctly rounding parser would differ (`1.7976931348623158e308` is *number out of range* for serde_json).
- **Error positions**: serde_json reports the byte just read (`error`) or the byte peeked (`peek_error`), columns in bytes; teta-wot turns them into a character offset that counts characters on earlier lines and bytes on the last. Both are ported as they are.
- **The `uuid` crate's messages** index braced and URN forms from inside the braces or prefix (they are parsed as 36 characters), and other lengths are diagnosed whole (`{}` is *invalid group count: expected 5, found 1*).
- **axum's body limit** answers 413 with `{"detail":"length limit exceeded"}`.
- **Drogon answers a HEAD as a GET** before the handler (and drops the body itself); `HttpRequest::isHead()` tells, and the binding routes it as a HEAD, as teta-wot does (`HEAD /counter/` is a 405).
- **Methods Drogon's parser doesn't know** (it knows `GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS`, `PATCH`, `PROPFIND`, `MKCOL`, `COPY`, `MOVE`) are answered 405 with no body before the handler; teta-wot routes them. **An empty query** (`/counter?`) can't be told from none in Drogon. **Bodies over Drogon's limit** (set to 128 MiB) get Drogon's own 413. All three are deltas in the [wire contract](../wire-contract.md#3-deltas).
- **Drogon's application is a process-wide singleton that runs once**: a process can `serve` once. Tests use `TestClient` in-process and separate processes over sockets.
- **trantor exits the process (code 1) when it can't bind** a listener. teta-wot's command line exits 1 for a port in use too; Phase 5's command line should check the port first, to say why.
- `drogon::ResponseStream::send("")` ends a chunked body, so sinks drop empty chunks; `setCustomStatusCode` keeps a view of the reason phrase, so phrases are static.
- MSVC: Drogon includes `<windows.h>`, so targets that include Drogon get `NOMINMAX`; the twins' translation unit needs `/bigobj` now that `<teta/wot.hpp>` includes the server.
- Iterating `json.value("key", default).items()` in a range-for dangles (the temporary dies before the loop, before C++23); it crashed twice during this phase.
- **Drogon's CMake package** needs vcpkg's toolchain in a consumer (its zlib dependency is found through vcpkg's wrapper), and its `FindFilesystem` leaves `CMAKE_CXX_STANDARD` set to `""` in a project that hadn't set it; `teta-wot-config.cmake` undoes that, and CI builds `cmake-consumer` with vcpkg's toolchain.
- `run.py` masked log-record locations in parsed bodies but not in the exact `text`; the earlier fixtures had no log records, so none of their bytes changed.

## Deviations from the plan

- **Server-sent events arrive a phase early**: property, event and "all" streams, named in the `wot` profile, with axum's keep-alive (`:` after 15 s of quiet, one thread per binding for all streams), as the first producer of streamed bodies. WebSockets, the SSE fixtures and broker backpressure stay in Phase 6.
- **The `wot` profile's answers are ported with the rest of `handlers.rs`** (they are branches of the same code) and its scenario passes; discovery, security and the rest of the profile stay in Phase 9.
- **Not in the route table yet**: OpenAPI and the docs (Phase 8), discovery (Phase 9), Blobs and MJPEG streams (Phase 7). Those paths are 404s until then; security (Phase 9) isn't checked.
- **`Harness`** moves to Phase 5 (it needs settings and slots); **`TestClient::websocket`** to Phase 6. `TestClient::del` is teta-wot's `delete`.
- **The neutral message types are in `teta::wot-core`**, because endpoints are declared in `ThingSpec`; the rest of the binding is `teta::wot-http`. `TestClient` is in `teta::wot-server`, as in teta-wot.
- **The default server ID** is teta-wot's (`COMPUTERNAME`, then `HOSTNAME`, then `localhost`), not the plan's §7 (`gethostname`): it decides TD `id`s, so it is wire.
- **A custom endpoint that throws** answers 500 with a problem (`application/problem+json`, the message as `detail`); a panicking axum handler drops the connection, which C++ can't reproduce usefully.
- **Building a server** throws `BuildError` or `http::RouteError`, where teta-wot wraps both in `ServerBuildError`.
- **Drogon is a required dependency** (it was a vcpkg feature); `<teta/wot/drogon.hpp>` and the target `teta::wot-drogon` are for applications that use Drogon themselves.
- **The twin server reads only the configuration keys the scenarios use**; the configuration model, the registry and `serve_from_cli` are Phase 5.
