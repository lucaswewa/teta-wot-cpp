# Phase 0: baseline and design lock

Status: **done** (2026-10-21). Plan: [§5, Phase 0](../PLAN.md#phase-0-baseline-and-design-lock-sm).

## What was built

| Deliverable | Where |
|---|---|
| ADRs for decisions D1–D13 | [`docs/adr/`](../adr/README.md) |
| CMake skeleton: presets for MSVC, clang-cl, GCC, Clang (ASan+UBSan, TSan) and a vcpkg-free `system` preset; vcpkg manifest pinned to a baseline; install rules and the `teta-wot` CMake package | `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `vcpkg.json`, `vcpkg-configuration.json` |
| `teta::wot-json`: values and serde_json-exact text (the JSON spike, kept as the first library) | `include/teta/wot/json.hpp`, `src/json/` |
| The serde_json corpus and its tests | `tests/json/` |
| The examples harness and `cmake-consumer` | `cmake/TetaWotExamples.cmake`, `examples/` |
| The Drogon spike, its raw-request probe and its checks | `spikes/drogon/` |
| The reference server, teta-wot v0.1.0 with its own `Cargo.lock`, and the corpus generator | `conformance/reference-rs/` |
| The parity harness (`capture`, `check`, `diff`) and the first scenario and fixture | `conformance/run.py`, `conformance/scenarios/`, `conformance/fixtures/` |
| The wire contract | [`docs/wire-contract.md`](../wire-contract.md) |
| CI on Windows (MSVC, clang-cl) and Linux (GCC, Clang with sanitizers); the reference fixtures; formatting | `.github/workflows/ci.yml` |

## Spike: Drogon as a transport

`spikes/drogon/main.cpp` is a Drogon 1.9.13 server with no routes of its own. `probe.py` sends raw requests to it and to the reference server; `checks.py` checks streaming, WebSockets, worker threads and shutdown. Results on Linux (GCC 13):

| Question | Answer |
|---|---|
| Does one handler see every request? | **Yes.** `setDefaultHandler` receives every method, including `PATCH`, `OPTIONS` and unknown ones (`PROPFIND`), and every path. Drogon's own 404/405/OPTIONS handling never runs. |
| Is the raw path available? | **Yes,** with `getOriginalPath()`: `/a%2Fb` stays `/a%2Fb` (`path()` decodes it to `/a/b`). Dot segments and `//` are passed through as sent. |
| `HEAD`? | Drogon strips the body of the handler's answer and keeps its `content-length`, as hyper does. |
| WebSocket on `/{thing}/ws` for any Thing? | **Yes,** with `registerWebSocketControllerRegex`. The controller must be an instance registered with `registerController` first (a non-auto-created controller registered only by name **crashes** on the first connection). A plain `GET` on the path reaches the default handler, so it can answer 404 as teta-wot does. |
| SSE and MJPEG? | **Yes,** with `newAsyncStreamResponse`, fed from another thread, chunked. **But** Drogon silently doesn't stream when the connection will close (`Connection: close`, or HTTP/1.0): it sends the headers and never calls the stream callback (`HttpServer.cc`: "Chunking Set CloseConnection"). Workaround, verified: a pre-sending advice keeps such a connection open and adds `connection: close`; the stream shuts the connection after its last chunk. |
| Responses completed from worker threads? | **Yes.** 20 concurrent 300 ms requests on 2 I/O threads finish in 0.31 s. |
| Body size limit? | Drogon's default is 1 MiB (413 beyond). **teta-wot has no limit** (a 64 MiB body was accepted), so the adapter must lift it. |
| `server` and `date` headers? | Both controllable: `server` off, `date` on, as teta-wot (hyper) sends. |
| Graceful drain? | **Yes,** with our own signal handling (`disableSigtermHandling`): a new-connection advice closes new connections, in-flight requests finish (a 1.5 s request started before Ctrl-C completed), then `quit()`. A connection opened meanwhile is accepted and reset, where teta-wot refuses it. |
| Malformed requests? | Drogon answers 400 before the handler, like hyper, with different headers (`Connection: close` only; hyper adds `content-length: 0` and `date`). |
| `Expect: 100-continue`? | Both send `100 Continue`; Drogon's carries `content-type` and `date`. |
| Header order? | Differs (hyper: `content-type, vary, content-length, connection, date`; Drogon: `content-length, connection, content-type, date`) and can't be controlled. |

**Decision.** The spike confirms [ADR-0004](../adr/0004-drogon-is-a-transport-only.md); the workarounds and limits above are recorded there. Header order and transport headers are left out of the contract ([wire contract §1](../wire-contract.md#1-what-is-compared-and-what-isnt)).

Not yet checked, for Phase 4: the same probe on Windows; pipelined requests; what teta-wot does with open SSE streams at shutdown.

## Spike: JSON

The plan assumed serde_json writes doubles as ryu does (`1e20`). **It doesn't:** serde_json 1.0.151, teta-wot's version, formats with `zmij` and writes a signed exponent: `1e+16`, `1e-7`, `1.7976931348623157e+308`. The decimal range is the same as ryu's: decimal exponents −5 to 15 are written as decimals (`0.00001`, `1000000000000000.0`), others in scientific notation.

The corpus (`tests/json/serde_json_corpus.txt`) is written by serde_json itself, through `conformance/reference-rs` built with **teta-wot v0.1.0's `Cargo.lock`** (a fresh lock had resolved serde_json 1.0.154; the lock is now part of the reference, ADR-0001):

- 7,520 doubles: special values, every power of ten from 1e-30 to 1e30 and their neighbours, 2,000 human-style decimals and 5,000 random bit patterns;
- integers at the 64-bit limits;
- every ASCII character, escapes, and non-ASCII text;
- 8 documents indented as settings files are.

`teta::wot::json::dump` uses `std::to_chars` for the shortest digits and lays them out as serde_json does. **It matches all 7,669 cases** with GCC 13 and with Clang 18 under ASan and UBSan. For comparison, nlohmann's own `dump()` differs on 469 of the doubles (`1e-07`, `1e+15`, `9.007199254740992e+15`), and on none of the strings.

## The reference and the harness

- `conformance/reference-rs` builds teta-wot v0.1.0 on Linux with Rust 1.98.1 (teta-wot itself supports Windows only, but builds and serves on Linux), so the differential tests can run on both platforms.
- `conformance/run.py capture` writes `fixtures/http/counter.json` from the reference: the TD, the redirect without a trailing slash, a property read, a read-only write (405 with `allow`), an invocation and its final state, the invocation lists, `things/`, a 404 and a CORS preflight.
- `check` replays it against a server, and `diff` compares two servers. In Phase 0 the only server is the reference, so `diff` runs it against itself: this proves the normalisation is stable (UUIDs, times and the invocation race differ between runs, and compare equal).
- The TD `id` derivation was reproduced independently (Python's `uuid5`): `conformance/counter` → `urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd`.

## Environment notes

- The development container used for Phase 0 can't download from `codeload.github.com`, so vcpkg can't fetch sources there. Locally, dependencies came from Ubuntu packages and Drogon 1.9.13 built from its git tag, with the `system` preset; **CI uses vcpkg** as ADR-0003 says.
- The Clang sanitizer builds need the compiler's runtime libraries (`libclang-rt-18-dev` on Ubuntu).

## Deviations from the plan

- `vcpkg.json` lists only what Phase 0 uses (nlohmann-json; Drogon and GoogleTest as features). RE2, CLI11, tl-expected, Boost.PFR and spdlog join in the phases that use them (ADR-0003).
- The harness is plain Python 3 with the standard library, not a `uv` project: it needs no packages yet.
- The serde_json number format is `1e+16`, not `1e20` as the plan's D6 said; the plan is corrected.
- The `cmake-consumer` test found a packaging bug (`GNUInstallDirs` included after the library, so the installed target had no include directory); fixed before the first commit.
