# Architecture and Implementation Plan: teta-wot-cpp

Status: **approved** (2026-10-21), with the answers to the open questions in §11. This plan implements [goals.md](goals.md). Work starts with Phase 0.

The goals, restated:

1. **Functionality parity** with [`teta-wot`](https://github.com/lucaswewa/teta-wot) [v0.1.0](https://github.com/lucaswewa/teta-wot/releases/tag/v0.1.0) (Rust): every feature in its guide and its 35 examples has a C++ counterpart.
2. **Wire-level parity:** a client can't tell the two servers apart. The same configuration, served by either, gives the same status codes, headers, bodies, Thing Descriptions, OpenAPI documents, WebSocket messages, server-sent events and settings files.
3. **Drogon** is the HTTP/WebSocket server.
4. **Modern C++**: C++20.
5. **Windows and Linux**, both built and tested in CI.

---

## 0. Decisions

Each becomes an ADR in `docs/adr/` in Phase 0. D1, D2, D3, D5, D11 and D12 are settled by your answers (§11); approving the plan accepts the rest.

| # | Topic | Proposal |
|---|---|---|
| D1 | Parity reference | **`teta-wot` at the `v0.1.0` tag (`135f9ff`) is the reference, and the only one.** Its behaviour on the wire is the contract, whatever its relation to LabThings-FastAPI; we add no deltas of our own. A later `teta-wot` release is adopted deliberately, with the fixtures re-captured. |
| D2 | Language level | **C++20**, as compiled by MSVC 17.10+, GCC 13+ and Clang 17+: concepts, `std::jthread`/`std::stop_token`, `std::format`, `std::source_location`, `std::span`, ranges, class-type non-type template parameters, designated initialisers. No C++23: `std::expected` is replaced by `tl::expected` (vcpkg `tl-expected`), aliased as `teta::wot::Expected`. Coroutines are not used (D5). |
| D3 | Build and packages | **CMake ≥ 3.28 with presets, and vcpkg in manifest mode** (pinned baseline); no other package manager is supported. Drogon, trantor, OpenSSL, nlohmann-json, RE2, CLI11, tl-expected, GoogleTest and Boost.PFR all have vcpkg ports on both platforms. Windows builds use the `x64-windows-static` triplet, so a server is one `.exe`, as in `teta-wot`. |
| D4 | Drogon's role | **Drogon is a transport only.** The HTTP binding is a transport-neutral dispatcher (`Request → Response`, where a response body can be a stream). A thin adapter connects it to Drogon. This keeps routing, 404/405, redirects, CORS and errors under our control (byte-exact parity), and lets the in-process `TestClient` call the dispatcher with no sockets, like `teta-wot`'s. |
| D5 | Concurrency | **Actions run on threads only.** Drogon's I/O loops never run Thing code. Actions and functional properties run on a growable worker pool; cancellation is `std::stop_token`; device actors are `std::jthread`s. There are no coroutine actions, and the library uses no coroutines internally either (Drogon's callback API is used in the adapter). |
| D6 | JSON | **`nlohmann::ordered_json` as the in-memory value** (insertion order, like `serde_json`'s `preserve_order`), with **our own serialiser** that writes numbers and strings exactly as `serde_json` does (serde_json 1.0.151's float layout, measured in Phase 0: `1.0`, `0.00001`, `1e+16`, `1e-7`; only `"`, `\` and control characters escaped). Drogon's jsoncpp is never used on the wire: its objects are sorted maps. |
| D7 | Types to schemas | **A `teta::wot::Codec<T>` customisation point** gives each type its JSON Schema, encoder and decoder. Built in for scalars, strings, `std::optional`, `std::vector`, `std::array`, `std::map`, `std::tuple`, `std::variant`, enums, `Blob`, `NdArray`. Aggregate structs are reflected with Boost.PFR (field names, C++20); `teta::wot::describe<T>` adds titles, descriptions and constraints. The schemas are built to equal what `schemars` emits for the corresponding Rust type, because they become the TD's DataSchemas. |
| D8 | Validation | **Port `teta-wot`'s `SchemaValidator`** (pydantic lax-mode coercions and error bodies, driven by the DataSchema) one-to-one, with **RE2** for `pattern` (same syntax family as Rust's `regex`, which pydantic-core also uses). |
| D9 | Errors | Thing code reports failures by throwing (`HandledError` for anticipated ones) or returning `Expected<T, ActionError>`. Any exception escaping an action ends its invocation as `error`; the server carries on. A crash (segfault) can't be caught in C++; this is a documented behavioural difference, not a wire one. |
| D10 | Logging | A small `teta::wot::log` facade (`std::format` + `std::source_location`) with per-invocation capture through a thread-local invocation scope, and an **spdlog sink** so driver code that already logs with spdlog is captured too. Records have `teta-wot`'s JSON shape (`levelname`, `levelno`, `lineno`, `filename`, `created`, …). |
| D11 | Verification | **Parity with `teta-wot` v0.1.0 is the gate** (§8): golden fixtures captured from the Rust reference server and replayed against the C++ server, and live **differential testing** of both servers. LabThings' Python `ThingClient` is not a gating client. |
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
- Configuration files, the registry, the command line with `teta-wot`'s exit codes, the fallback server, graceful shutdown.
- Blobs; `NdArray`; the Windows service.
- `TestClient`, `Harness`, `action_ctx` for tests.
- The 35 examples (§6) and the guide.

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
  conformance/               Rust reference server, scenarios, captured fixtures, differential runner (§8)
  examples/<name>/           one per teta-wot example, same names
  fuzz/                      libFuzzer targets
  tools/soak/                the soak tool
  docs/                      this plan, ADRs, phase notes, the guide (mdBook, as teta-wot's)
  third_party/               vendored: mdns.h (mjansson/mdns)
  assets/                    Swagger UI and ReDoc, as teta-wot vendors them (Phase 8)
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
- **Threads only (D5, final).** Rust actions are `async fn`, but what they need from it is being cancellable while waiting, which `ActionCtx::sleep(d)` (a `condition_variable_any` wait on the `stop_token`), `check_cancelled()` and `cancelled()` give on threads. Coroutines were considered and rejected:
  - lab servers run tens of concurrent invocations, not thousands, so a thread per running action costs nothing;
  - driver code blocks (vendor SDKs, serial ports), so coroutine actions would still have to move it to threads;
  - C++20 has no standard task type, executor or cancellation, so coroutines would tie Thing code to Drogon's `Task` or to a scheduler of our own;
  - references and `this` dangling across a suspension, poor stack traces and compiler differences would cost Thing authors, who know threads;
  - LabThings runs actions on threads too, and the wire is the same either way.

  The costs of threads are handled: the pool grows on demand (with a high configurable cap), so an action waiting on a nested in-process call never starves the others; streaming (SSE, WebSocket, MJPEG viewers) is callback-driven in the adapter and never holds a thread per client.

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
| JPEG frames (examples only) | stb_image_write (vendored) | Simulated camera images, as `jpeg-encoder` in the Rust examples |

---

## 3. Wire contract

The contract is what `teta-wot` v0.1.0 does on the wire, unchanged:

- the `tetathing` profile, exactly as `teta-wot` answers (routes, redirects, 404/405, HEAD, CORS, 422 bodies, `{"detail": …}` errors, invocation JSON, WebSocket dialect, settings files). That profile descends from LabThings-FastAPI v0.3.0, but LabThings itself is not a reference: where the two differ, `teta-wot` wins;
- the `wot` profile follows the W3C WoT HTTP Basic and SSE Profiles exactly as `teta-wot` does, with the problem types of `wot/docs/problems.md`;
- TDs equal `teta-wot`'s byte-for-byte, after the same normalisation (UUIDs, times);
- OpenAPI documents equal `teta-wot`'s, including `operationId`s;
- settings files are byte-identical (CRLF on Windows, LF on Linux, as `teta-wot` writes them on each).

Any difference a test finds is a C++ bug, unless it is a bug in `teta-wot` v0.1.0 itself. Then it is reported upstream and recorded in `docs/wire-contract.md` as a delta with its exact expected answer, to be removed when a later `teta-wot` release fixes it and is adopted.

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
| `ActionError::handled`, `?` on any error, caught panics | `HandledError`, any exception, `Expected` | D9 |
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
- its conformance gate (§8) passing;
- the examples for its concepts (§6), each run by CI and, from Phase 4, compared with the Rust original;
- a note in `docs/notes/phase-NN-*.md` (what was built, deviations, wire facts found) and its ADRs;
- `clang-format`, `clang-tidy` and warnings-as-errors clean (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang); ASan+UBSan and TSan runs on Linux.

### Phase 0: Baseline and design lock (S–M)

**Done** (2026-10-21): see the [Phase 0 note](notes/phase-00-baseline.md).

- Repository skeleton: CMake presets (`windows-msvc`, `linux-gcc`, `linux-clang`, `*-asan`, `*-tsan`), vcpkg manifest with a pinned baseline, CI on `windows-latest` and `ubuntu-24.04`.
- **Drogon spike**, answering with code: catch-all routing for every method; regex WebSocket controller on dynamic paths; `newAsyncStreamResponse` for SSE and `multipart/x-mixed-replace`; control of `server`/`date`/`content-length` headers; graceful drain (refuse new connections, finish in-flight requests, then quit); body size limit; behaviour on malformed requests compared with hyper's.
- **JSON spike:** the `serde_json`-exact serialiser, checked against a corpus of numbers and strings serialised by `serde_json` (generated by a tiny Rust program in `conformance/`).
- **Reference server:** `conformance/reference-rs/`, a Rust binary in this repository depending on `teta-wot` at the `v0.1.0` tag, serving the twin Things (ported from `wot/conformance/twin-server` to `teta-wot`'s API, plus the examples' Things) with `teta-wot`'s command line. The C++ twin server (`conformance/twin-server-cpp/`) serves the same Things under the same registry names.
- **Fixture capture:** `conformance/scenarios/` (scripted requests, adapted from `wot`'s capture scripts) and `conformance/capture.py`, which runs them against the reference server and writes the golden fixtures to `conformance/fixtures/`.
- The examples harness: `teta_wot_add_example`, CTest registration, `examples/README.md`, and the `cmake-consumer` example (§6).
- ADRs for D1–D13; `docs/wire-contract.md`; the guide skeleton.

**Gate:** the spike results recorded in the Phase 0 note; any spike that fails reopens its decision before Phase 1.

### Phase 1: JSON, TD and schemas (M)

**Done** (2026-10-22): see the [Phase 1 note](notes/phase-01-td-schema.md). `blob_probe` and `ndarray_probe` TDs move to Phases 7 and 10.

Ports `teta-wot-td`: TD model and builders, DataSchema, forms, security schemes, constraints, `Codec<T>` with schema generation equal to `schemars`' for each built-in type and reflected structs, JSON Schema → DataSchema conversion, W3C validation.

**Gate:** every TD captured from the reference server (`conformance/fixtures/td/`) reproduced from the C++ twins (TD-only, no server yet), and valid against the W3C TD 1.1 schema; the `td_builder` and `schema-to-dataschema` examples.

### Phase 2: Validation (M)

**Done** (2026-10-22): see the [Phase 2 note](notes/phase-02-validation.md). The serde-compatible request body parser moves to Phase 4.

Ports `validate.rs`: lax coercions, error types, `loc`, `msg`, `input`, `ctx`, the `value_error` path for Blobs and arrays.

**Gate:** the validation fixtures captured from the reference server (each type's coercions, and the answers to property writes and invocations of a probe Thing) replayed with no deltas; a randomised test comparing the C++ validator with the Rust one on generated inputs.

### Phase 3: Runtime core (L)

**Done** (2026-10-22): see the [Phase 3 note](notes/phase-03-runtime.md). Settings, slots, endpoints, streams and Blobs join the definition API in their phases.

Ports `teta-wot-core`: `Thing`, `ThingSpec` and `ThingDefinition`; `Prop<T>` and functional properties; actions, invocations (state machine, retention, lazy expiry), `ActionCtx`, cancellation with child invocations; global lock; logs; broker; events; device actors; the worker pool.

**Gate:** `teta-wot-core`'s tests (`property`, `runtime`, `cancel`, `device`, `validate`) ported and passing; TSan clean; the compile-fail suite; the `compile-errors`, `property-cells`, `functional-properties`, `cancellable-action`, `global-lock`, `device-actor`, `invocation-logs`, `builder_thing` examples (in-process).

### Phase 4: HTTP binding, Drogon adapter and TestClient (L). **First end-to-end milestone.**

**Done** (2026-10-22): see the [Phase 4 note](notes/phase-04-http.md). Server-sent events arrive here, a phase early; OpenAPI, discovery, Blob and stream routes join the table in their phases.

Ports `teta-wot-http` (routes, dispatcher, the `tetathing` renderer, errors, CORS) and the server builder, plus the Drogon adapter and `TestClient`.

**Gate:** the HTTP fixtures (`default`, `locked`, `prefixed` scenarios) replayed through `TestClient` and over real sockets, with no deltas; the differential runner (§8.2) green on the HTTP scenarios; the quickstart, `light`, `counter-server`, `http-walkthrough`, `api-prefix`, `custom-endpoint`, `drogon-integration` examples.

### Phase 5: Composition, settings, configuration, lifecycle (M–L)

**Done** (2026-10-22): see the [Phase 5 note](notes/phase-05-composition.md). The settings fixtures are `fixtures/http/settings.json` and `settings-load.json`; the command line's are `fixtures/cli/`.

Slots and interfaces, services and `Dep<S>`, settings persistence, the configuration model and registry, the command line and exit codes, the fallback server, start order and the shutdown sequence (Ctrl-C/console events on Windows, SIGINT/SIGTERM on Linux).

**Gate:** `fixtures/settings/settings.json` byte-identical; settings files interchangeable with the Rust server's both ways; the command line's exit codes and the fallback page equal to the reference's; `thing-slots`, `dependency-injection`, `settings`, `config-and-cli`, `graceful-shutdown` examples.

### Phase 6: Observation and events (M)

**Done** (2026-10-22): see the [Phase 6 note](notes/phase-06-observation.md). The WebSocket fixtures are `fixtures/http/ws.json` and `events.json`; the SSE ones `sse.json` and `sse-wot.json`.

WebSocket dialect (`addPropertyObservation`, `addActionObservation`, `addEventSubscription`, errors), SSE on properties, all properties and events, broker backpressure.

**Gate:** `fixtures/ws/ws.json` with no deltas; SSE scenarios equal to the Rust server's; `observe-websocket`, `sse-observe`, `events` examples.

### Phase 7: Blobs and MJPEG (M)

**Done** (2026-10-23): see the [Phase 7 note](notes/phase-07-blobs.md). The fixtures are `fixtures/http/blobs.json`, `blobs-wot.json` and `streams.json`.

Blob outputs and inputs, the Blob store and downloads, MJPEG streams and the viewer page.

**Gate:** the Blob and stream scenarios (downloads, Blob inputs and their errors, frames, viewer page, a stopped stream) equal to the reference's; `blob-camera`, `mjpeg-stream` examples.

### Phase 8: OpenAPI and docs (S–M)

**Done** (2026-10-23): see the [Phase 8 note](notes/phase-08-openapi.md). The fixtures are `fixtures/http/openapi.json`, `openapi-wot.json`, `openapi-prefixed.json` and `docs.json`, and every example's document.

OpenAPI 3.1 generation with `teta-wot`'s `operationId`s, Swagger UI and ReDoc pages, offline assets compiled in (CMake generates byte arrays; no `#embed`).

**Gate:** `/openapi.json` byte-equal to the Rust server's for every twin configuration and both profiles; `openapi-spec-validator` passes; `openapi-export`, `docs-offline` examples.

### Phase 9: WoT profile, discovery, security (M–L)

**Done** (2026-10-23): see the [Phase 9 note](notes/phase-09-discovery.md). The fixtures are `fixtures/http/discovery.json`, `discovery-wot.json`, `security-basic.json` and `security-bearer.json`, and the examples' `bearer-security`, `node-wot-consumer` and `discovery`.

The `wot` wire profile (204s, problem details, `ActionStatus`, synchronous actions, named SSE), top-level forms, `/.well-known/wot` and the directory, DNS-SD, Basic and bearer security.

**Gate:** differential runner green in the `wot` profile (writes, problems, `ActionStatus`, synchronous actions, named SSE, discovery, security challenges); DNS-SD found by `zeroconf` on both OSes; `strict-profile`, `discovery`, `bearer-security`, `node-wot-consumer`, `units-and-semantics` examples.

### Phase 10: Arrays, services, hardening, release (M)

**In progress**, in parts: see the [Phase 10 note](notes/phase-10-release.md). Parts 1 (arrays), 2 (the simulated microscope), 3 (the Windows service and the systemd unit), 4 (fuzzing, the soak tool and the shutdown-under-load test) and 5 (the guide, the vcpkg port and the release's preparation) are done; the v0.1.0 tag waits for the release build's 24-hour soak.

`NdArray`; the Windows service (install/uninstall, stop during a long action) and the systemd unit; fuzz targets (validation, WS parser, request bodies) in CI; the shutdown-under-load test; the soak tool; the simulated microscope and `microscope-service`; the full guide; vcpkg overlay port; v0.1.0.

**Gate:** the array scenarios (including ragged arrays) equal to the reference's; `service_test.ps1` on Windows and a systemd test on Linux; 24 h soak with no leak (ASan/LSan on Linux, CRT debug heap on Windows); every example in §6 ported, run by CI on both OSes, and equal on the wire to its Rust original.

Rough sizing (S ≈ 1 week, M ≈ 2–3, L ≈ 4–5 for one developer): about 30–40 weeks, Phases 2, 3 and 4 being the bulk.

---

## 6. Examples

Examples are part of each phase's deliverable (§5.0), not an afterthought: each teaches one concept with the public API only, and doubles as a test.

### 6.1 Conventions

- **One folder per example,** `examples/<name>/`, with the same name as `teta-wot`'s, holding `CMakeLists.txt`, `main.cpp` (plus any headers or assets) and a `README.md` with `teta-wot`'s sections: **Concept**, **Technologies**, how to run it, what to look for.
- **Two modes, as in `teta-wot`:** run without arguments, an example runs a short in-process demonstration (through `TestClient` or in-process calls), prints what it shows, and exits 0; with `--serve` it serves on `127.0.0.1:5000` until Ctrl-C/SIGTERM.
- **One CMake helper,** `teta_wot_add_example(<name> SOURCES … [WINDOWS_ONLY] [NEEDS python|node])`, builds it against the installed-style targets (`teta::wot`) and registers its demonstration as a CTest test (label `example`), so CI runs every example on both OSes.
- **Wire parity, too.** Each example's Things are the same as the Rust example's, under the same names and options. Phase 4 onwards, the differential runner (§8.2) serves each example with `--serve` from both implementations (the Rust ones built from `teta-wot` v0.1.0's `examples/`) and compares their TDs, OpenAPI documents and a scripted session. A C++ example therefore can't drift from its Rust original.
- **Scripts and clients** that `teta-wot`'s examples use (`walkthrough.sh`/`.ps1`, the Python clients, the node-wot consumer, the browser pages) are copied unchanged, since they only see the wire: a script that works against the Rust example must work against the C++ one.
- **Only the examples need extra code:** a small baseline JPEG encoder (`examples/common/jpeg.hpp`) encodes the simulated JPEG frames where the Rust examples use `jpeg-encoder` (Phase 7 note).
- **`examples/README.md`** catalogues them by phase and by concept, with the same "to learn / start with" table as `teta-wot`'s guide.

### 6.2 Catalogue

The 35 examples of `teta-wot` v0.1.0, by the phase that delivers them:

| Phase | Example | Concept | C++ notes |
|---|---|---|---|
| 1 | `td_builder` | A TD built by hand, validated against the W3C schema | `td::ThingDescription::builder`, the affordance builders |
| 1 | `schema-to-dataschema` | How C++ types become DataSchemas | `Codec<T>`, Boost.PFR structs, `describe<T>` for docs and constraints (replaces schemars) |
| 3 | `builder_thing` | A Thing defined with the builder API | `ThingDefinition`, `Runtime`, the type-erased registry |
| 3 | `property-cells` | `Prop<T>`: validation, notification, read-only | `Watcher<T>` replaces `tokio::sync::watch` |
| 3 | `functional-properties` | Getters, setters, resetters | `.setter()`, `.resetter()`, `.blocking()` |
| 3 | `cancellable-action` | Cooperative cancellation | `ActionCtx::sleep`, `check_cancelled`, `cancelled`, child invocations, `std::stop_token` |
| 3 | `global-lock` | Actions and writes one at a time | `.global_lock(false)`, `ctx.hold_global_lock()` |
| 3 | `device-actor` | A synchronous driver on its own thread | `Device<D>`: `call`, `post`, timeouts, faults, `reset`, `abort` |
| 3 | `invocation-logs` | Per-invocation logs | `tw::log`, and the **spdlog sink** capturing a driver that logs with spdlog |
| 4 | `quickstart-counter` | The first Thing, served | `Thing<T>` + `define()`, `TestClient` |
| 4 | `light` | The guide's tutorial Thing | constraints, `readonly`, a functional property |
| 4 | `counter-server` | The first end-to-end milestone | `ThingServer` on Drogon |
| 4 | `http-walkthrough` | Every route, status and header over a real socket | `walkthrough.sh` / `walkthrough.ps1` unchanged |
| 4 | `api-prefix` | Serving under `/api/v1` | `api_prefix`, `TestClient::with_host` |
| 4 | `custom-endpoint` | Routes outside the TD | `t.endpoint(...)` on the neutral `Request`/`Response` |
| 5 | `thing-slots` | Composition, interfaces, swapping implementations by configuration | `Slot`, `OptSlot`, `SlotMap`, abstract-base-class interfaces, two configuration files |
| 5 | `dependency-injection` | Services shared between Things | `ThingServerBuilder::service`, `Dep<S>`, `Server` |
| 5 | `settings` | Properties that survive restarts | `.setting()`; files interchangeable with the Rust example's |
| 5 | `config-and-cli` | An application binary served from configuration files | `Registry`, `serve_from_cli`, exit codes, the fallback server |
| 5 | `graceful-shutdown` | Stopping without leaving hardware in a bad state | Console control events on Windows, SIGINT/SIGTERM on Linux |
| 6 | `observe-websocket` | Observing over `/{thing}/ws` | Browser page and Python script unchanged |
| 6 | `sse-observe` | `EventSource` in the browser | `on_start`/`on_stop` feeding a property from a `std::jthread` |
| 6 | `events` | A driver emitting events from its thread | `Event<T>::emit` from a `Device<D>` |
| 7 | `blob-camera` | Blobs out and back in | `Blob<blob::Jpeg>`, `from_bytes`, `from_temporary_directory` |
| 7 | `mjpeg-stream` | A live preview and actions waiting for frames | `MjpegStream`, `add_frame` from a thread, `grab_frame`, `next_frame_size` |
| 8 | `openapi-export` | The OpenAPI document and TDs without a server | `openapi()`, `thing_description()`; `openapi-typescript` unchanged |
| 8 | `docs-offline` | Swagger UI and ReDoc with no internet | `TETA_WOT_DOCS_OFFLINE=ON` |
| 9 | `strict-profile` | `tetathing` and `wot` side by side | Two servers, same requests |
| 9 | `discovery` | `/.well-known/wot`, the directory, DNS-SD | `TETA_WOT_MDNS=ON`; found by Python `zeroconf` |
| 9 | `bearer-security` | Requiring a token | `Security::bearer` |
| 9 | `node-wot-consumer` | Interoperability with Eclipse node-wot | The JavaScript consumer unchanged |
| 9 | `units-and-semantics` | Units, `@type`, `@context` | `.unit()`, `.semantic_type()`, `t.context()` |
| 10 | `ndarray-property` | Arrays as nested lists | `NdArray<double, 2>`, `NdArray<double, dynamic_rank>` |
| 10 | `simulated-microscope` | The capstone: stage, camera, autofocus with slots, settings, MJPEG, Blobs, events, arrays | Same `microscope.json` and `demo.py` as the Rust one |
| 10 | `microscope-service` | The microscope as a service | Windows: SCM install/uninstall/run/console and `service_test.ps1`; Linux: `install` writes a systemd unit, `Type=notify`, and `service_test.sh` checks a stop during a long action |

### 6.3 Examples only the C++ project needs

| Phase | Example | Concept |
|---|---|---|
| 0 | `cmake-consumer` | Using the library from another CMake project: `find_package(teta-wot CONFIG REQUIRED)` with vcpkg (and, from Phase 10, the overlay port). CI builds it from an installed tree on both OSes. |
| 3 | `compile-errors` | What the compiler says when a Thing is declared wrongly (a type without a `Codec`, a wrong number of parameter names, a setter without its property), with each message explained. It mirrors `tests/compile_fail/`, and is the counterpart of `teta-wot`'s `trybuild` UI tests. Its demonstration only lists the cases; CI checks they fail to compile. |
| 4 | `drogon-integration` | Serving Things inside an application that already uses Drogon for its own routes: mounting the dispatcher next to existing controllers, sharing the event loops, and leaving shutdown to the application. |

---

## 7. Platform specifics

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

## 8. Verification strategy

### 8.1 Golden fixtures from the reference

`conformance/fixtures/` holds the answers of the Rust reference server (`teta-wot` v0.1.0) to every scenario in `conformance/scenarios/`: TDs, validation, HTTP exchanges in several server configurations (default, global lock, API prefix, `wot` profile, security), settings files written and read, WebSocket and SSE messages, OpenAPI documents. They are normalised as the `wot` repository's fixtures are (UUIDs numbered by first appearance, timestamps masked, the race in a fresh invocation's state masked, only contract headers kept). GoogleTest suites replay them against the C++ twin server, through `TestClient` and over sockets, and require equality. These run on every CI job without building Rust. CI also re-captures them and fails if anything under `fixtures/` changes, so they can't drift from the reference.

### 8.2 Live differential testing

`conformance/diff/` (Python, pinned with `uv`) starts the reference server and the C++ twin server from the **same configuration file**, sends the same requests (HTTP, SSE, WebSocket, MJPEG) to both, normalises both answers the same way, and requires them to be equal, headers included. Besides the scripted scenarios it has a randomised mode: request bodies generated from each affordance's DataSchema (valid, borderline and invalid), odd paths and methods, and WebSocket messages, to find differences no one scripted. It runs on Windows and Linux (the reference is built from source on both).

### 8.3 Engineering tests

Unit tests ported from `teta-wot`; compile-fail tests; ASan/UBSan/TSan builds; libFuzzer targets; the shutdown-under-load test and the soak tool; every example run by CI.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Drogon can't stop accepting while finishing in-flight requests, or can't stream as needed | Phase 0 spike; if the drain is impossible, answer new requests during the grace period with `teta-wot`'s refusal, and record it; worst case a small patch upstream to Drogon |
| Drogon's request parser rejects or normalises requests hyper accepts (odd paths, percent-encoding, `Expect`, HTTP/1.0) | Differential scenarios with malformed requests in Phase 4; where Drogon answers before our handler, match the answer or document the delta |
| Float/string formatting differs in rare cases | Own serialiser, corpus from `serde_json`, fuzzed against the Rust serialiser |
| pydantic emulation is large and subtle | Port line-for-line from `validate.rs` with its tests; fixture and randomised differential tests |
| `schemars` schema details (titles, `$defs`, `oneOf` shapes) for every type | Schema generation tested per type against `schemars` output generated in `conformance/` |
| No doc comments in C++ | `.doc()` with the same rule; optional clang-based generator later |
| A crashing driver takes the process down (no panic catching) | Device actors keep driver code on known threads; documented; out-of-process drivers are a non-goal |
| Compiler differences in C++20 support (`std::format`, ranges, NTTPs) | Minimum versions in D2; a clang-cl CI job besides MSVC catches portability issues early |
| The reference moves | Pinned commit; bumps re-run all suites (§3) |

---

## 10. Deliverables of this plan's first step (Phase 0)

1. ADRs 0001–0013 for D1–D13.
2. The repository skeleton building an empty library and a test on both OSes in CI, with the examples harness and the `cmake-consumer` example (§6).
3. The Drogon and JSON spikes, with their results in `docs/notes/phase-00-baseline.md`.
4. `conformance/`: the Rust reference server at `teta-wot` v0.1.0, the capture tooling, and the first scenario captured and compared (the counter's TD and one invocation).
5. `docs/wire-contract.md`.

---

## 11. Answered questions

| # | Question | Answer | Where it lands |
|---|---|---|---|
| 1 | Language level | **C++20** | D2 |
| 2 | Concurrency | **Actions on threads only** (confirmed after weighing coroutines) | D5, §2.3 |
| 3 | Package manager | **vcpkg** | D3 |
| 4 | Reference | **`teta-wot` tag `v0.1.0`** | D1, §3 |
| 5 | Rust reference twins | **In this repository** (`conformance/reference-rs/`) | Phase 0, §8 |
| 6 | Naming | **`teta::wot`, `<teta/wot/…>`** | D12 |
| 7 | Gating client | **Matching `teta-wot` is enough**; LabThings' `ThingClient` is not gating | D11, §3, §8 |
