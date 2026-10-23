# Phase 8: OpenAPI and docs

Status: **done** (2026-10-23). Plan: [§5, Phase 8](../PLAN.md#phase-8-openapi-and-docs-sm). Servers describe their API in an OpenAPI 3.1 document that is teta-wot's byte for byte, with Swagger UI and ReDoc pages, online or compiled in.

## What was built

| Deliverable | Where |
|---|---|
| `tw::http::openapi(runtime, options)`, a port of teta-wot-http's `openapi.rs`: the framework's, discovery's and each Thing's routes, FastAPI's `operationId`s (`tw::http::operation_id`, duplicates numbered), the components, both profiles; it needs no server and no started Things | `http/openapi.hpp`, `src/http/openapi.cpp` |
| The docs pages, a port of `docs.rs`: Swagger UI at `/docs`, its OAuth2 redirect page at `/docs/oauth2-redirect` (the script compiled in, inline), ReDoc at `/redoc`, titled after the API | `src/http/docs.hpp`, `src/http/docs.cpp` |
| The routes: `/openapi.json`, `/docs`, `/docs/oauth2-redirect`, `/redoc` at the root, with `GET` and `HEAD` (`Routes::add_get_and_head`, a 405's `allow` `GET, HEAD`); the document made once, when the binding is | `src/http/routes.cpp`, `src/http/binding.cpp` |
| `TETA_WOT_DOCS_OFFLINE` (CMake option, off by default as teta-wot's `docs-offline` feature): Swagger UI 5.33.0 and ReDoc 2.5.4 compiled in as byte arrays (`teta_wot_embed`), served from `/docs/…` and `/redoc/…`, cached a day; `tw::http::docs_offline()`; the preset `windows-msvc-docs-offline` | `CMakeLists.txt`, `src/CMakeLists.txt`, `CMakePresets.json` |
| The assets, from teta-wot's git blobs with their licences, stored byte for byte (`-text`) | `assets/` |
| `DataSchema::for_type` drops a root `title` that is only the type's schema name, as teta-wot's does (`Codec<T>::schema_name()`; a Blob's is its media type's title) | `codec.hpp`, `blob.hpp` |
| `drogon_adapter::answer_all`: a server that owns its Drogon application (`ThingServer`, the fallback server) answers every request before Drogon routes it, but the Things' WebSockets | `drogon.hpp`, `src/drogon/adapter.cpp`, `src/server/thing_server.cpp` |
| Scenarios and fixtures: `openapi`, `openapi-wot`, `openapi-prefixed`, `docs`; every example scenario now compares the OpenAPI document; a `docs-offline` example scenario; `run.py`'s `workdir_files` | `conformance/` |
| `conformance/validate_openapi.py`: every document the C++ servers serve through `openapi-spec-validator` (`conformance.openapi`, skipped without it; CI installs it) | `conformance/`, `tests/CMakeLists.txt`, `.github/workflows/ci.yml` |
| Two examples, `openapi-export` and `docs-offline`; the endpoints of `custom-endpoint`, `observe-websocket` and `sse-observe` named as teta-wot names them | `examples/` |

## The gate

- **`/openapi.json` is byte-equal to the reference's** for every twin (but `ndarray_probe`, Phase 10) in both profiles and under a prefix (`openapi`, `openapi-wot`, `openapi-prefixed`), and for every example that serves, against teta-wot's examples (`conformance.examples`). `openapi-export` writes the document (and both TDs) byte for byte as teta-wot's example does (`conformance.openapi-export`).
- **`openapi-spec-validator` (0.9.0) passes** on the 29 documents the C++ servers serve: the twin server in each of the 19 scenario configurations, and the 10 examples that serve.
- **The docs pages equal the reference's** (`docs`): the three pages, `HEAD`, 405s, the trailing-slash redirects, CORS, no assets without the option.
- **The examples**: `openapi-export` prints what teta-wot's prints; `docs-offline`, built with `TETA_WOT_DOCS_OFFLINE`, prints what teta-wot's prints (but the CRLF bundles' sizes, below) and answers its pages, assets and the lamp as teta-wot's does (`conformance.examples` in that build); without the option it exits 77, which CTest reports as skipped.
- teta-wot-http's `openapi.rs` and `docs.rs` tests are ported, with tests of the document's routes and order, the profiles' answers, the routes at the root and offline assets (7).
- 370 tests with `windows-msvc`: all pass, but three skip in the default build (the offline assets' test, the `docs-offline` example, and `conformance.openapi` without the validator, which passes with it). With `TETA_WOT_DOCS_OFFLINE`, all pass but `conformance.openapi`, skipped the same way. Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now.

## Findings

- **Drogon served files from the server's working directory.** Before the default handler, Drogon's static file router answers a path that names a file there with an extension it knows (`GET /CMakeLists.txt` returned the file), and a directory with an implicit `index.html`, or a 405 for other methods. This was there since Phase 4; the `docs` scenario found it (the repository has a `docs/` directory). `ThingServer` and the fallback server now answer every request in a pre-routing advice, so Drogon routes nothing but the Things' WebSockets; the scenario runs the server in a working directory with files that would be served. `mount`, for applications that embed the binding, leaves Drogon's routing to the application, as documented.
- **teta-wot names a custom endpoint after its function** (`page`, `monitor`), which C++ can't see: an endpoint is named after its path unless `.name(...)` is given, so the examples now give teta-wot's names.
- **A Blob output's schema had a `title`** that teta-wot's `DataSchema::for_type` drops (schemars' root title, when it is the schema's name); TDs didn't show it, since an action's output is retitled there, but OpenAPI components did.
- The document already describes discovery's routes (`/.well-known/wot`, the directory), as teta-wot's does, though they are served from Phase 9; with `security`, teta-wot adds `securitySchemes` and 401s, which come with security in Phase 9.
- Rust's `usize` has no C++ twin, but neither the TD nor the document keeps numeric formats, so an unsigned integer's schema is the same.
- **MSVC ran out of heap** compiling the document as deeply nested `json` initialiser lists; its constant parts are JSON text, parsed when the document is made, as teta-wot's `json!` reads.
- teta-wot's Windows build embeds the assets from a CRLF checkout (git's `autocrlf`), one byte longer for the Swagger bundle; teta-wot-cpp vendors git's LF bytes, so every checkout serves the same bytes, and the `docs-offline` scenario compares the assets' headers, not their bodies.
- The fixtures' normaliser masks `lineno` in any object with `levelname`, `levelno` and `lineno`, which includes the document's `LogRecordModel`; both sides are masked alike, so the comparison holds, and the validator reads the documents served, not the fixtures.

## Deviations from the plan

- **`openapi-spec-validator` is a CTest test that skips without the package**; CI's Windows job installs it with pip.
- **The `docs-offline` example builds in every configuration** and skips its check (exit 77) without the option, so it compiles everywhere; its example scenario runs only in a build with the option, and the `docs` scenario (teta-wot's pages without assets) only in one without. CI builds without it.
- **The offline build was checked in the `windows-msvc` tree with the option switched on**, not in a `windows-msvc-docs-offline` tree: a second tree (with its own vcpkg packages, about 15 GB) didn't fit on the development machine's disk. The preset is there for machines that have the room.
