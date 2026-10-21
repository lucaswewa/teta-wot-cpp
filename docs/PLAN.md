# Architecture and Implementation Plan: teta-wot-cpp

Status: **draft for review** (2026-10-21). This plan implements [goals.md](goals.md). No library code is written until the plan is approved. The open questions are in §10.

The goals, restated:

1. **Functionality parity** with [`teta-wot`](https://github.com/lucaswewa/teta-wot) v0.1.0 (Rust): every feature in its guide and its 35 examples has a C++ counterpart.
2. **Wire-level parity:** a client can't tell the two servers apart. The same configuration, served by either, gives the same status codes, headers, bodies, Thing Descriptions, OpenAPI documents, WebSocket messages, server-sent events and settings files.
3. **Drogon** is the HTTP/WebSocket server.
4. **Modern C++**: C++23 where all three compilers support it, C++20 otherwise.
5. **Windows and Linux**, both built and tested in CI.

---

## 0. Proposed decisions

Each becomes an ADR in `docs/adr/` in Phase 0. Approving the plan accepts them.

| # | Topic | Proposal |
|---|---|---|
| D1 | Parity reference | **`teta-wot` at a pinned commit is the reference.** Where `teta-wot` documents a delta from LabThings-FastAPI, we inherit it; we add no deltas of our own on the wire. The pinned commit is bumped deliberately, with the fixtures re-captured. |
| D2 | Language level | **C++23 as compiled by MSVC 17.10+, GCC 13+ and Clang 17+.** We use only the C++23 features all three ship (`std::expected`, `std::to_underlying`, deducing `this`, `std::ranges` additions). `std::print`, `std::stacktrace`, `std::mdspan` and `#embed` are not relied on. |
| D3 | Build and packages | **CMake ≥ 3.28 with presets, and vcpkg in manifest mode** (pinned baseline). Drogon, trantor, OpenSSL, nlohmann-json, RE2, CLI11, GoogleTest and Boost.PFR all have vcpkg ports on both platforms. Windows builds use the `x64-windows-static` triplet, so a server is one `.exe`, as in `teta-wot`. |
| D4 | Drogon's role | **Drogon is a transport only.** The HTTP binding is a transport-neutral dispatcher (`Request → Response`, where a response body can be a stream). A thin adapter connects it to Drogon. This keeps routing, 404/405, redirects, CORS and errors under our control (byte-exact parity), and lets the in-process `TestClient` call the dispatcher with no sockets, like `teta-wot`'s. |
| D5 | Concurrency | **Threads, not coroutines, for Thing code.** Drogon's I/O loops never run Thing code. Actions and functional properties run on a growable worker pool; cancellation is `std::stop_token`; device actors are `std::jthread`s. C++20 coroutines are kept out of the authoring API (see §2.3 for why); they may be used inside the adapter. |
| D6 | JSON | **`nlohmann::ordered_json` as the in-memory value** (insertion order, like `serde_json`'s `preserve_order`), with **our own serialiser** that writes numbers and strings exactly as `serde_json` does (ryu's float layout: `1.0`, `1e20`, `1e-7`; only `"`, `\` and control characters escaped). Drogon's jsoncpp is never used on the wire: its objects are sorted maps. |
| D7 | Types to schemas | **A `teta::wot::Codec<T>` customisation point** gives each type its JSON Schema, encoder and decoder. Built in for scalars, strings, `std::optional`, `std::vector`, `std::array`, `std::map`, `std::tuple`, `std::variant`, enums, `Blob`, `NdArray`. Aggregate structs are reflected with Boost.PFR (field names, C++20); `teta::wot::describe<T>` adds titles, descriptions and constraints. The schemas are built to equal what `schemars` emits for the corresponding Rust type, because they become the TD's DataSchemas. |
| D8 | Validation | **Port `teta-wot`'s `SchemaValidator`** (pydantic lax-mode coercions and error bodies, driven by the DataSchema) one-to-one, with **RE2** for `pattern` (same syntax family as Rust's `regex`, which pydantic-core also uses). |
| D9 | Errors | Thing code reports failures by throwing (`HandledError` for anticipated ones) or returning `std::expected<T, ActionError>`. Any exception escaping an action ends its invocation as `error`; the server carries on. A crash (segfault) can't be caught in C++; this is a documented behavioural difference, not a wire one. |
| D10 | Logging | A small `teta::wot::log` facade (`std::format` + `std::source_location`) with per-invocation capture through a thread-local invocation scope, and an **spdlog sink** so driver code that already logs with spdlog is captured too. Records have `teta-wot`'s JSON shape (`levelname`, `levelno`, `lineno`, `filename`, `created`, …). |
| D11 | Verification | Three layers (§7): fixture replay (`teta-wot`'s inherited LabThings fixtures), **differential testing against the pinned Rust server**, and the LabThings Python `ThingClient` suite. |
| D12 | Naming | Project `teta-wot-cpp`; namespace `teta::wot`; CMake package `teta-wot`, targets `teta::wot`, `teta::wot-td`, …; headers under `<teta/wot/…>`, umbrella `<teta/wot.hpp>`. |
| D13 | Licence | MIT (the repository's licence), so it can be packaged (a vcpkg overlay port in Phase 10). |

---

## 1. Scope

**In scope:** everything `teta-wot` v0.1.0 has.

- Things with data and functional properties, settings, actions, events, MJPEG streams, slots, interfaces, devices, services, custom endpoints.
- Invocations: lifecycle, retention, cancellation, child invocations, logs, the global lock.
- pydantic-compatible validation and 422 bodies.
- The HTTP binding in both wire profiles (`tetathing`, `wot`), WebSocket and SSE observation.
- TD 1.1 generation and W3C validation; OpenAPI 3.1, Swagger UI and ReDoc (online and offline).
- Discovery (`/.well-known/wot`, the TD directory, DNS-SD); Basic and bearer security; CORS.
- Configuration files, the registry, the command line with LabThings' exit codes, the fallback server, graceful shutdown.
- Blobs; `NdArray`; the Windows service.
- `TestClient`, `Harness`, `action_ctx` for tests.
- The 35 examples and the guide.

**Added for Linux** (no wire effect): SIGINT/SIGTERM shutdown, and a systemd unit with `sd_notify` readiness as the Linux counterpart of the Windows service.

**Non-goals:** a C++ client library; WoT Scripting API; loading Thing types from plugins at run time (Things are compiled in, as in `teta-wot`); TLS termination (as in `teta-wot`, put a proxy in front); macOS (it will likely build, but isn't tested).

---

## 2. Architecture

### 2.1 Layout

Libraries mirror `teta-wot`'s crates, so each phase ports one crate and its tests:

| CMake target | Mirrors | Contents | Depends on |
|---|---|---|---|
| `teta::wot-json` | (serde_json) | `Value` alias, the `serde_json`-exact serialiser, parse with positions, the pretty writer for settings files | nlohmann-json |
| `teta::wot-td` | `teta-wot-td` | TD 1.1 model, DataSchema, forms/ops, security schemes, builders, `Codec<T>` and schema generation, JSON Schema → DataSchema conversion, constraints, W3C validation (test-only) | json, Boost.PFR |
| `teta::wot-core` | `teta-wot-core` | Thing model and `ThingSpec`, `Prop<T>`, functional properties, actions, invocations, `ActionCtx`, cancellation, global lock, broker, events, device actors, slots, settings, services, Blob, MJPEG stream, `NdArray`, logs, the validator | td, RE2, spdlog (optional sink) |
| `teta::wot-http` | `teta-wot-http` | **Transport-neutral** binding: route table, dispatcher, renderers per wire profile, problems, CORS, security, observation protocol (WS messages, SSE framing), OpenAPI, discovery, docs pages, fallback page | core |
| `teta::wot-drogon` | (axum glue) | Drogon adapter: catch-all HTTP handler, WebSocket controller, async stream bodies (SSE, MJPEG), connection draining, signal handling | http, Drogon |
| `teta::wot-server` | `teta-wot-server` | `ThingServer` builder, configuration model, registry, CLI, lifecycle, fallback server, Windows service / systemd, mDNS advertiser, `testing` (TestClient, Harness) | drogon, CLI11 |
| `teta::wot` | `teta-wot` | Interface target and umbrella header `<teta/wot.hpp>` | all |

```
teta-wot-cpp/
  CMakeLists.txt  CMakePresets.json  vcpkg.json  vcpkg-configuration.json
  include/teta/wot/...      public headers, one folder per library
  src/<library>/...          implementation
  tests/<library>/...        GoogleTest suites; tests/compile_fail/ (expected build failures)
  conformance/               fixtures, reference server, scenario runner, client suite (§7)
  examples/<name>/           one per teta-wot example, same names
  fuzz/                      libFuzzer targets
  tools/soak/                the soak tool
  docs/                      this plan, ADRs, phase notes, the guide (mdBook, as teta-wot's)
  third_party/               vendored: mdns.h (mjansson/mdns), Swagger UI and ReDoc assets
```

### 2.2 Runtime model

```
 Drogon I/O loops (N threads)                       in-process callers
 ───────────────────────────                        (other Things, tests)
  catch-all HttpHandler ─┐   WebSocketController ─┐          │
                         ▼                        ▼          │
      teta::wot-http  Dispatcher  ◄── TestClient (no sockets)│
      (routes, profile renderers, CORS, security)            │
                         │  never blocks an I/O loop         │
                         ▼                                   ▼
 ┌──────────────────────── Runtime ───────────────────────────────────────┐
 │ ThingHandles (type-erased PropertyEntry / ActionEntry / EventEntry)    │
 │ InvocationManager ── worker pool (growable std::jthreads)              │
 │   ActionCtx: stop_token, log scope, global-lock hold, child invocations│
 │ GlobalLock (reentrant per invocation tree, 50 ms try)                  │
 │ MessageBroker ── bounded per-subscriber queues (teta-wot's policy)     │
 │ Settings store (atomic write: temp file + rename / MoveFileExW)        │
 │ Blob store (refcounted, expires with invocations)                      │
 │ Services (type-indexed), application_config                            │
 └────────────────────────────────────────────────────────────────────────┘
        │                                │
   Device<D> actor threads          MjpegStream (latest-frame slot +
   (driver created, used and        condition variable per viewer;
   destroyed on its own thread)     add_frame never waits)
```

Rules:

- **I/O loops only parse, route and write.** Anything that runs Thing code (a functional getter, a setter, an action, a slot call) is posted to the worker pool, and the response is completed from there through Drogon's callback. Data property reads are an in-memory copy under a short lock, and may stay on the loop.
- **Streaming bodies** (SSE, MJPEG) are producers: the dispatcher returns a `StreamBody` (a pull interface with a stop signal). The Drogon adapter drives it with `newAsyncStreamResponse`; `TestClient` drives it directly.
- **Shutdown** follows `teta-wot`'s sequence: refuse new connections, cancel invocations, wait up to the grace period (5 s), stop Things in reverse start order, close devices, then quit Drogon's loops.

### 2.3 Authoring API

Rust's proc-macros (`#[derive(Thing)]`, `#[thing_impl]`) take everything from the source, doc comments included. C++ has no such reflection on any compiler we target (C++26 reflection isn't in MSVC), so a Thing **declares its affordances in one static `define` function**, using member pointers, so every type stays checked at compile time. `ThingDefinition` (the builder for dynamic Things) is the same API without a class.

```cpp
#include <teta/wot.hpp>
namespace tw = teta::wot;

class Light : public tw::Thing<Light> {
public:
    tw::Prop<std::int64_t> brightness{100};
    tw::Prop<bool> is_on{false};

    std::string status() const {
        return is_on.get() ? std::format("The light is on at {}% brightness.", brightness.get())
                           : "The light is off.";
    }
    void toggle(tw::ActionCtx&) { is_on.update([](bool& on) { on = !on; }); }

    static void define(tw::ThingSpec<Light>& t) {
        t.doc("A testing light Thing.");
        t.property("brightness", &Light::brightness).doc("The brightness of the light.").ge(0).le(100);
        t.property("is_on", &Light::is_on).doc("Whether the light is on.").readonly();
        t.property("status", &Light::status).doc("A human-readable status of the light.");
        t.action("toggle", &Light::toggle).doc("Toggle the light on and off.");
    }
};
```

- **Action parameters** are named with a compile-time list, so a wrong count fails to compile: `t.action<"x", "y", "z">("move_to", &Stage::move_to)`. Server-supplied parameters (`ActionCtx&`, `Server&`, `Dep<S>`) are recognised by type and aren't named. `.param("speed").default_value(1000).doc("…")` mirrors `#[param]`; `.input()` mirrors `#[input]`.
- **Options** mirror the Rust attributes one-for-one: `readonly`, `ge/gt/le/lt/multiple_of`, `min_length/max_length/pattern`, `allow_inf_nan(false)`, `unit`, `semantic_type`, `global_lock(false)`, `title`, `description`, `blocking` (accepted for parity; every action already runs on a worker), `retention`, `synchronous`; `.setting()` instead of `.property()`; `.setter(&T::set_x)`, `.resetter(&T::reset_x)`; `t.event("arrived", &T::arrived)`; `t.stream("preview", &T::preview)`; `t.slot("camera", &T::camera).default_name("camera")`; `t.device(&T::port, init)`; `t.endpoint("GET", "path", handler)`; `t.on_start(&T::start)`, `t.on_stop(&T::stop)`; `t.implements<CameraApi>()`; `t.context("saref", "https://saref.etsi.org/core/")`; `t.config<StageConfig>()`.
- **Docs.** `.doc(text)` follows `teta-wot`'s doc-comment rule: the first paragraph is the title, the whole text the description, so TDs come out the same.
- **Misuse fails to compile** with a readable message (concepts and `static_assert`), as `teta-wot`'s `trybuild` tests check: a property type without a `Codec`, a setter without its property, a reserved or colliding name (checked when the definition is built, at server build time), a non-copyable property type. `tests/compile_fail/` ports those cases.
- **In-process calls** are typed through member pointers and run as child invocations (shared cancellation, logs and lock hold), blocking the calling worker until they finish: `auto pos = stage.call(&Stage::move_to, 0, 0, 250);` Interfaces are abstract base classes: `Slot<CameraApi>` holds whichever Thing `implements<CameraApi>()`.
- **Why not coroutines (D5):** Rust actions are `async fn`, but their main need is being cancellable while waiting, which `ActionCtx::sleep(d)` (a `condition_variable_any` wait on the `stop_token`), `check_cancelled()` and `cancelled()` give on threads. Coroutines would force every Thing author to pick an executor and would make `blocking` and non-blocking actions behave differently. LabThings itself runs actions on threads. The thread pool grows on demand (with a configurable cap) so concurrent invocations never deadlock each other.

A clang-based tool that generates `define()` (and `.doc()` from `///` comments) is a possible later addition, not part of parity.

### 2.4 The wire layer

Parity is easiest if a few things are owned outright:

- **Routing.** One Drogon handler receives every request (all methods, any path); our route table (ported from `routes.rs`, with Starlette's trailing-slash redirects, 404/405 bodies and `allow` headers) decides. Drogon's own 404/405/HEAD handling is never reached.
- **WebSockets.** One `WebSocketController` registered by regex for `{prefix}/{thing}/ws`; a non-upgrade `GET` falls through to the dispatcher (404, as both reference servers answer).
- **Headers.** Drogon's `server` and `date` headers are configured to match `teta-wot` (axum sends `date`, no `server`), the body size limit is set to axum's, and every response is built by the renderer, so `content-type` strings are exactly `teta-wot`'s.
- **JSON bytes** come from the `serde_json`-exact serialiser (D6), so number formatting, escaping and key order match.
- **Times:** local naive times (`%Y-%m-%dT%H:%M:%S%.6f` rules from `render.rs`) and RFC 3339 UTC with microseconds.
- **IDs:** UUID v4 for invocations and Blobs, and TD `id`s with `teta-wot`'s exact v5 derivation (namespace from `https://github.com/lucaswewa/wot#td-id`, then `{server_id}/{thing}`), so the same server ID gives the same TD ids in both servers.

### 2.5 Third-party components

| Need | Choice | Why |
|---|---|---|
| HTTP, WebSocket | Drogon (latest 1.9.x, pinned) + trantor | The goal; async stream responses for SSE/MJPEG |
| JSON | nlohmann-json (`ordered_json`) + own serialiser | Order-preserving; distinguishes ints and floats as pydantic emulation needs |
| Struct reflection | Boost.PFR | Field names of aggregates on all three compilers, header-only |
| Regex | RE2 | Rust-`regex`-compatible syntax, linear time |
| CLI | CLI11 | `-c/-j/--host/--port/--fallback/--debug` and exit code 2 on bad usage |
| Logging | own facade; spdlog sink (optional) | Invocation capture; drivers using spdlog are captured |
| DNS-SD | mjansson/mdns (vendored single header) | Embedded responder on both OSes, like Rust's `mdns-sd`; no Avahi/Bonjour dependency |
| SHA-1 (UUID v5), base64 | `drogon::utils` | Already linked |
| Tests | GoogleTest; JSON-Schema validation with pboettch/json-schema-validator (draft-07, for the W3C TD schema) | |
| Fuzzing | libFuzzer (Clang, Linux CI) | Ports `teta-wot`'s fuzz targets |

---

## 3. Wire contract

The contract is `teta-wot`'s, unchanged:

- the `tetathing` profile reproduces LabThings-FastAPI v0.3.0 (routes, redirects, 404/405, HEAD, CORS, 422 bodies, `{"detail": …}` errors, invocation JSON, WebSocket dialect, settings files), with `teta-wot`'s documented deltas (B1–B10, G1–G18);
- the `wot` profile follows the W3C WoT HTTP Basic and SSE Profiles exactly as `teta-wot` does, with the problem types of `wot/docs/problems.md`;
- TDs equal `teta-wot`'s byte-for-byte, after the same normalisation (UUIDs, times);
- OpenAPI documents equal `teta-wot`'s, including `operationId`s;
- settings files are byte-identical (CRLF on Windows, LF on Linux, as `teta-wot` writes them on each).

Any difference a test finds is a bug in one of the two servers. If it is `teta-wot`'s, it is reported upstream and recorded as a temporary delta, with an expiry when the pinned commit moves.

Phase 0 writes `docs/wire-contract.md`: the list above, expanded from `teta-wot`'s module docs and the `wot` repository's ADRs and phase notes (where most of the wire facts are recorded).

---

## 4. Parity matrix (teta-wot → teta-wot-cpp)

| teta-wot | teta-wot-cpp | Notes |
|---|---|---|
| `#[derive(Thing)]` struct | `class X : public tw::Thing<X>` + `static void define(ThingSpec<X>&)` | §2.3 |
| `#[thing_impl]` methods | member functions registered in `define` | |
| doc comments | `.doc("…")` | same title/description rule |
| `Prop<T>`: `get/read/set/update/subscribe` | `Prop<T>`: same names; `subscribe()` returns a `Watcher<T>` (`wait_changed(stop_token)`) | |
| `Event<T>::emit` | `Event<T>::emit` (copyable, any thread) | |
| `MjpegStream`: `add_frame/grab_frame/next_frame_size/stop` | same | |
| `Slot<T>`, `OptSlot<T>`, `SlotMap<T>`, `#[teta_wot::interface]` | same names; interfaces are abstract base classes | |
| `Device<D>`: `call/call_blocking/reset/abort`, timeouts, faults | `Device<D>`: `call` (from a worker), `post` (returns `std::future`), same faults and recovery | |
| `ActionCtx`: `sleep`, `check_cancelled`, `cancelled`, `blocking`, `hold_global_lock` | same; `blocking` is a direct call | |
| `ActionError::handled`, `?` on any error, caught panics | `HandledError`, any exception, `std::expected` | D9 |
| `Dep<S>`, `ThingServerBuilder::service` | same | |
| `Blob<M>`: `from_bytes/from_file/from_temporary_directory/from_url` | same; media types as tag types (`blob::Jpeg`, …) | |
| `NdArray<A, D>` (`Ix1`…, `IxDyn`) | `NdArray<T, Rank>` with `dynamic_rank` | own container; nested-list codec and numpy's ragged message |
| `#[endpoint]` (axum handler) | `t.endpoint(method, path, fn(const Request&) -> Response)` | on the neutral request type |
| `ThingServer::builder()…build()`, `serve`, `serve_with` | `ThingServer::builder()…build()`, `serve(host, port, stop_token)` | |
| `Registry`, `serve_from_cli`, `serve_until` | same | |
| `run_as_service`, `install`, `uninstall` | same on Windows; `systemd::notify_ready` on Linux | |
| `testing::{TestClient, Harness, action_ctx, in_invocation}` | same | |
| `thing_description()`, `openapi()` without a server | same | |
| `tracing` capture | `tw::log` + spdlog sink | D10 |
| features `ndarray`, `mdns`, `docs-offline`, `windows-service`, `testing` | CMake options `TETA_WOT_NDARRAY`, `…_MDNS`, `…_DOCS_OFFLINE`, `…_WINDOWS_SERVICE`, `…_TESTING` | |

---

## 5. Phases

Each phase ports one area of `teta-wot`, and ends with its gate green on **both** Windows and Linux CI.

### 5.0 Every phase delivers

- the code, with unit tests ported from the corresponding `teta-wot` tests;
- its conformance gate (§7) passing;
- the examples for its concepts (same names as `teta-wot`'s), each run by CI;
- a note in `docs/notes/phase-NN-*.md` (what was built, deviations, wire facts found) and its ADRs;
- `clang-format`, `clang-tidy` and warnings-as-errors clean (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang); ASan+UBSan and TSan runs on Linux.

### Phase 0: Baseline and design lock (S–M)

- Repository skeleton: CMake presets (`windows-msvc`, `linux-gcc`, `linux-clang`, `*-asan`, `*-tsan`), vcpkg manifest with a pinned baseline, CI on `windows-latest` and `ubuntu-24.04`.
- **Drogon spike**, answering with code: catch-all routing for every method; regex WebSocket controller on dynamic paths; `newAsyncStreamResponse` for SSE and `multipart/x-mixed-replace`; control of `server`/`date`/`content-length` headers; graceful drain (refuse new connections, finish in-flight requests, then quit); body size limit; behaviour on malformed requests compared with hyper's.
- **JSON spike:** the `serde_json`-exact serialiser, checked against a corpus of numbers and strings serialised by `serde_json` (generated by a tiny Rust program in `conformance/`).
- **Reference server:** `conformance/reference-rs/`, a Rust binary depending on `teta-wot` at the pinned commit, serving the twin Things ported from `wot/conformance/twin-server` (and the examples' Things), with LabThings' command line.
- ADRs for D1–D13; `docs/wire-contract.md`; the guide skeleton.

**Gate:** the spike results recorded in the Phase 0 note; any spike that fails reopens its decision before Phase 1.

### Phase 1: JSON, TD and schemas (M)

Ports `teta-wot-td`: TD model and builders, DataSchema, forms, security schemes, constraints, `Codec<T>` with schema generation equal to `schemars`' for each built-in type and reflected structs, JSON Schema → DataSchema conversion, W3C validation.

**Gate:** every TD fixture in `conformance/fixtures/td/` reproduced from C++ twins built with the builder API (TD-only, no server yet), and valid against the W3C TD 1.1 schema; the `td_builder` and `schema-to-dataschema` examples.

### Phase 2: Validation (M)

Ports `validate.rs`: lax coercions, error types, `loc`, `msg`, `input`, `ctx`, the `value_error` path for Blobs and arrays.

**Gate:** `fixtures/validation/coercions.json` and `bodies.json` replayed with no deltas beyond `teta-wot`'s (non-finite numbers, out-of-range integers); a property-based test comparing the C++ validator with the Rust one on generated inputs (through the reference server).

### Phase 3: Runtime core (L)

Ports `teta-wot-core`: `Thing`, `ThingSpec` and `ThingDefinition`; `Prop<T>` and functional properties; actions, invocations (state machine, retention, lazy expiry), `ActionCtx`, cancellation with child invocations; global lock; logs; broker; events; device actors; the worker pool.

**Gate:** `teta-wot-core`'s tests (`property`, `runtime`, `cancel`, `device`, `validate`) ported and passing; TSan clean; the compile-fail suite; the `property-cells`, `functional-properties`, `cancellable-action`, `global-lock`, `device-actor`, `invocation-logs`, `builder_thing` examples (in-process).

### Phase 4: HTTP binding, Drogon adapter and TestClient (L). **First end-to-end milestone.**

Ports `teta-wot-http` (routes, dispatcher, the `tetathing` renderer, errors, CORS) and the server builder, plus the Drogon adapter and `TestClient`.

**Gate:** `fixtures/http/{default,locked,prefixed}.json` replayed through `TestClient` and over real sockets with the same allowed deltas as `teta-wot`; the differential runner (§7.2) green on the HTTP scenarios; the quickstart, `light`, `counter-server`, `http-walkthrough`, `api-prefix`, `custom-endpoint` examples.

### Phase 5: Composition, settings, configuration, lifecycle (M–L)

Slots and interfaces, services and `Dep<S>`, settings persistence, the configuration model and registry, the command line and exit codes, the fallback server, start order and the shutdown sequence (Ctrl-C/console events on Windows, SIGINT/SIGTERM on Linux).

**Gate:** `fixtures/settings/settings.json` byte-identical; settings files interchangeable with the Rust server's both ways; the client suite's `test_configuration.py`; `thing-slots`, `dependency-injection`, `settings`, `config-and-cli`, `graceful-shutdown` examples.

### Phase 6: Observation and events (M)

WebSocket dialect (`addPropertyObservation`, `addActionObservation`, `addEventSubscription`, errors), SSE on properties, all properties and events, broker backpressure.

**Gate:** `fixtures/ws/ws.json` with no deltas; SSE scenarios equal to the Rust server's; `test_websocket.py`; `observe-websocket`, `sse-observe`, `events` examples.

### Phase 7: Blobs and MJPEG (M)

Blob outputs and inputs, the Blob store and downloads, MJPEG streams and the viewer page.

**Gate:** the Blob and stream steps of the HTTP fixtures; `test_blobs.py`; `blob-camera`, `mjpeg-stream` examples.

### Phase 8: OpenAPI and docs (S–M)

OpenAPI 3.1 generation with `teta-wot`'s `operationId`s, Swagger UI and ReDoc pages, offline assets compiled in (CMake generates byte arrays; no `#embed`).

**Gate:** `/openapi.json` equal to the Rust server's for every twin configuration; `fixtures/openapi/*` with `teta-wot`'s deltas; `openapi-spec-validator` passes; `openapi-export`, `docs-offline` examples.

### Phase 9: WoT profile, discovery, security (M–L)

The `wot` wire profile (204s, problem details, `ActionStatus`, synchronous actions, named SSE), top-level forms, `/.well-known/wot` and the directory, DNS-SD, Basic and bearer security.

**Gate:** `test_wot_profile.py`; differential runner green in the `wot` profile; DNS-SD found by `zeroconf` on both OSes; `strict-profile`, `discovery`, `bearer-security`, `node-wot-consumer`, `units-and-semantics` examples.

### Phase 10: Arrays, services, hardening, release (M)

`NdArray`; the Windows service (install/uninstall, stop during a long action) and the systemd unit; fuzz targets (validation, WS parser, request bodies) in CI; the shutdown-under-load test; the soak tool; the simulated microscope and `microscope-service`; the full guide; vcpkg overlay port; v0.1.0.

**Gate:** `test_ndarray.py`; `service_test.ps1` on Windows and a systemd test on Linux; 24 h soak with no leak (ASan/LSan on Linux, CRT debug heap on Windows); every one of the 35 examples ported and run by CI.

Rough sizing (S ≈ 1 week, M ≈ 2–3, L ≈ 4–5 for one developer): about 30–40 weeks, Phases 2, 3 and 4 being the bulk.

---

## 6. Platform specifics

| Concern | Windows | Linux |
|---|---|---|
| Compilers | MSVC 17.10+ (also clang-cl in CI) | GCC 13+, Clang 17+ |
| Linking | static (`x64-windows-static`), one `.exe` | shared or static; a static `-DTETA_WOT_STATIC=ON` build is tested |
| Shutdown signal | `SetConsoleCtrlHandler` (Ctrl-C, Ctrl-Break, close, logoff, shutdown), SCM stop | SIGINT, SIGTERM (Drogon's own handlers disabled) |
| Service | SCM (`StartServiceCtrlDispatcher`), install/uninstall | systemd unit, `Type=notify` with `sd_notify` (no libsystemd dependency: the socket protocol is a few lines) |
| Settings file endings | CRLF | LF |
| Atomic replace | `MoveFileExW(MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)` | write, `fsync`, `rename` |
| Server ID default | `COMPUTERNAME` | `HOSTNAME`, then `gethostname` |
| Paths | `std::filesystem` everywhere, UTF-8 in and out (`/utf-8` on MSVC) | |

---

## 7. Verification strategy

### 7.1 Fixture replay

`conformance/fixtures/` takes the fixtures captured from LabThings-FastAPI v0.3.0 in the `wot` repository (TD, validation, HTTP, settings, WebSocket, OpenAPI), with their normalisation rules. The C++ twin server (`conformance/twin-server-cpp/`) serves C++ ports of the twins, registered under the same Python import strings. GoogleTest suites replay each fixture, allowing exactly the deltas `teta-wot` allows, each with its exact expected answer.

### 7.2 Differential testing against teta-wot

`conformance/diff/` is a scenario runner (Python, pinned with `uv`, as in `wot`): it starts the Rust reference server and the C++ twin server from the **same configuration file**, sends the same scripted requests (HTTP, SSE, WebSocket, MJPEG) to both, normalises both answers the same way, and requires them to be equal, headers included. Scenarios cover everything the fixtures cover, plus what LabThings doesn't have (the `wot` profile, discovery, security, events, top-level forms, `NdArray`). A randomised mode generates request bodies from each affordance's DataSchema to compare validation answers. This is the test that enforces wire parity with `teta-wot` itself rather than with LabThings; it runs on both OSes (the reference is built from source on Linux too).

### 7.3 Client suites

The `wot` repository's pytest suite (`conformance/client/`), which drives LabThings' unmodified Python `ThingClient` and a W3C-only consumer, runs against the C++ server (and, as a control, the Rust one).

### 7.4 Engineering tests

Unit tests ported from `teta-wot`; compile-fail tests; ASan/UBSan/TSan builds; libFuzzer targets; the shutdown-under-load test and the soak tool; every example run by CI.

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Drogon can't stop accepting while finishing in-flight requests, or can't stream as needed | Phase 0 spike; if the drain is impossible, answer new requests during the grace period with `teta-wot`'s refusal, and record it; worst case a small patch upstream to Drogon |
| Drogon's request parser rejects or normalises requests hyper accepts (odd paths, percent-encoding, `Expect`, HTTP/1.0) | Differential scenarios with malformed requests in Phase 4; where Drogon answers before our handler, match the answer or document the delta |
| Float/string formatting differs in rare cases | Own serialiser, corpus from `serde_json`, fuzzed against the Rust serialiser |
| pydantic emulation is large and subtle | Port line-for-line from `validate.rs` with its tests; fixture and randomised differential tests |
| `schemars` schema details (titles, `$defs`, `oneOf` shapes) for every type | Schema generation tested per type against `schemars` output generated in `conformance/` |
| No doc comments in C++ | `.doc()` with the same rule; optional clang-based generator later |
| A crashing driver takes the process down (no panic catching) | Device actors keep driver code on known threads; documented; out-of-process drivers are a non-goal |
| MSVC lags on C++23 | Only features in MSVC 17.10 are used (D2); clang-cl CI job catches portability issues early |
| The reference moves | Pinned commit; bumps re-run all suites (§3) |

---

## 9. Deliverables of this plan's first step (Phase 0)

1. ADRs 0001–0013 for D1–D13.
2. The repository skeleton building an empty library and a test on both OSes in CI.
3. The Drogon and JSON spikes, with their results in `docs/notes/phase-00-baseline.md`.
4. `conformance/`: fixtures copied from `wot`, the Rust reference server at the pinned `teta-wot` commit, and the first differential scenario (the counter's TD and one invocation).
5. `docs/wire-contract.md`.

---

## 10. Open questions

1. **Language level.** C++23 (subset in D2), or strictly C++20 (replacing `std::expected` with our own `Expected`)?
2. **Concurrency (D5).** Threads with `std::stop_token` for actions, as proposed, or coroutine actions (`drogon::Task<T>`) as a first-class option alongside?
3. **Package manager.** vcpkg (proposed) or Conan 2?
4. **Reference pin.** Pin `teta-wot` at its current `main` (`135f9ff`, v0.1.0 + docs), or wait for a tagged release?
5. **Rust reference twins.** Host them in this repository (`conformance/reference-rs/`, proposed), or add a `twin-server` to `teta-wot` itself so both projects share it?
6. **Naming (D12).** `teta::wot` namespace and `<teta/wot/…>` headers OK?
7. **Gating client.** Is the LabThings Python `ThingClient` still gating for this project (it is for `teta-wot`'s compatibility profile), or is parity with `teta-wot` enough?
