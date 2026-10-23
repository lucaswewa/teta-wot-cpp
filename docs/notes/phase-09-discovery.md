# Phase 9: WoT profile, discovery, security

Status: **done** (2026-10-23). Plan: [§5, Phase 9](../PLAN.md#phase-9-wot-profile-discovery-security-ml). Servers can be found (the well-known URL, a Thing Description Directory, DNS-SD) and can require credentials, as teta-wot's do; the `wot` profile, served since Phase 4, now covers them too.

## What was built

| Deliverable | Where |
|---|---|
| Security, a port of teta-wot-http's `security.rs`: `tw::http::Security` (`basic`, `bearer`) in `HttpOptions` and `ThingServerBuilder::security`; credentials checked before every route that isn't a description, and before a WebSocket upgrade (a transport upgrades only what `is_websocket` accepts; the binding answers the rest 401); the 401 and its challenge; the TDs' security definitions; constant-time comparison | `http/binding.hpp`, `src/http/security.*`, `src/http/binding.cpp` |
| The OpenAPI document's `securitySchemes`, and `security` and a 401 on every operation but the descriptions | `src/http/openapi.cpp` |
| Configuration files' `security`, served (it was refused): the secret read from the variable the file names | `src/server/thing_server.cpp` |
| Discovery, a port of `discovery.rs`: `/.well-known/wot` (the directory's TD), `{prefix}/directory/things` (sorted, paged, `ThingCollection`, `next` and `canonical` links, `etag`), `{prefix}/directory/things/{id}`; `HEAD`; errors always problems; query arguments read as Rust's `serde_urlencoded` and `usize` read them; IDs decoded as teta-wot decodes them | `src/http/discovery.*`, `src/http/routes.cpp` |
| DNS-SD over mDNS (`TETA_WOT_MDNS`, off by default as teta-wot's feature, on in the repository's presets): a responder thread on mjansson/mdns 1.4.3 (vendored, its SHA-512 checked against vcpkg's pin) advertising the server as a TD Directory, announced twice, answering PTR (the service, its `_directory` subtype, DNS-SD's service list), SRV, TXT, A and AAAA questions, withdrawn with a goodbye at shutdown; `tw::mdns_supported()`, `tw::directory_service_type` | `src/server/mdns.*`, `third_party/mdns/`, `include/teta/wot/thing_server.hpp` |
| The Drogon adapter joins a repeated response header's values (`link`) with `, `, since Drogon keeps one value per name; `run.py` records repeated headers the same way | `src/drogon/adapter.cpp`, `conformance/run.py` |
| `run.py`: `env` (the secrets a configuration names), a `wait`'s follow-up request with the step's headers | `conformance/run.py` |
| Scenarios and fixtures: `discovery`, `discovery-wot`, `security-basic`, `security-bearer`; example scenarios `bearer-security`, `node-wot-consumer`, `discovery`; `browse_mdns.py` (`conformance.mdns`, `zeroconf`; CI installs it) | `conformance/`, `tests/CMakeLists.txt`, `.github/workflows/ci.yml` |
| Five examples: `strict-profile`, `discovery`, `bearer-security`, `node-wot-consumer`, `units-and-semantics` | `examples/` |

## The gate

- **The differential runner is green in the `wot` profile**: writes, problems, `ActionStatus` and synchronous actions (`wot`, since Phase 4), named SSE (`sse-wot`, Phase 6), discovery (`discovery-wot`) and security challenges (`security-bearer`, in `wot` under a prefix); and in `tetathing` (`discovery`, `security-basic`). No existing fixture changed when every one was captured again with the new header handling.
- **DNS-SD is found by `zeroconf`** (0.151.5): the twin server's advertisement, with its port, host, TXT record and addresses, and `td` answering the directory's TD (`conformance.mdns`). On Windows only: the Linux presets are off in CI for now.
- **The examples**: `strict-profile`, `bearer-security`, `node-wot-consumer` and `units-and-semantics` print what their Rust originals print (but for IDs, times, and `bearer-security`'s last line, which is Rust's `Debug` form there); `discovery` finds itself as teta-wot's does. Served, `bearer-security`, `node-wot-consumer` and `discovery` answer their OpenAPI document, TDs and a session as teta-wot's examples do (`conformance.examples`).
- teta-wot-http's `discovery.rs` and `security.rs` tests are ported, with tests of the directory TD against the W3C schema, credentials through `TestClient` (descriptions, events, WebSockets) and the OpenAPI document's security (10); the Phase 5 test that `security` was refused now checks that it is served.
- `openapi-spec-validator` passes on all 36 documents served, the secured ones included.
- 386 tests pass with `windows-msvc` (4 skip: the offline docs' test and example, and the validator and `zeroconf` checks without those packages, which pass with them). Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now.

## Findings

- **Drogon keeps one value per response header name**: the directory's two `link` headers became one. The adapter now joins repeated values with `, `, which HTTP allows for `link`; hyper sends two lines, so `run.py` joins them too before comparing.
- **teta-wot decodes a path's `%XX` with `u8::from_str_radix`, which takes a sign**: `%+3` is the byte 3. The query is decoded differently (`form_urlencoded`: `+` a space, a sign not taken); `?offset=%2B1` is 1, `?limit=+1` is refused. The scenarios pin both.
- **An unauthorised WebSocket upgrade is answered 401**, before any upgrade, in both servers: the C++ binding's `is_websocket` refuses it, so neither Drogon's controller nor `TestClient` upgrades it, and the binding answers.
- **teta-wot's `mdns-sd` answers per interface**, loopback included: a browser on the same computer saw `127.0.0.1`. The C++ responder names the computer's other addresses (as mjansson's example does), so a local browser sees the LAN address.
- teta-wot-cpp's `security` key had been parsed since Phase 5 and refused at serving; serving it needed no change to the configuration model.

## Deviations from the plan

- **`node-wot-consumer --serve` serves one profile per process** (`--profile wot` for the second): Drogon runs one application per process, where teta-wot serves both from one. The JavaScript consumer the README mentions isn't in teta-wot v0.1.0's repository, so the example checks in-process what node-wot relies on, as teta-wot's does.
- **The DNS-SD responder** listens on one socket per address family, joined to the multicast group on the default interface, and names the addresses the computer had when it started; teta-wot's `mdns-sd` follows interfaces as they change. A server ID with a `.` becomes several DNS labels.
- **`TETA_WOT_MDNS` is on in the repository's presets**, so CI builds and tests DNS-SD; teta-wot's feature, and the option's default, are off.
- **The `discovery` example browses with mjansson/mdns** (included from `third_party/`), as teta-wot's browses with `mdns-sd`, rather than through a teta-wot-cpp API.
