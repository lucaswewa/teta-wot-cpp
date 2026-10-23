# Conformance

How teta-wot-cpp is held to wire-level parity with [teta-wot v0.1.0](https://github.com/lucaswewa/teta-wot/tree/v0.1.0) ([ADR-0001](../docs/adr/0001-parity-reference.md), [ADR-0011](../docs/adr/0011-verification.md), [wire contract](../docs/wire-contract.md)).

| Path | What |
|---|---|
| `reference-rs/` | The reference: a Rust crate depending on teta-wot at the `v0.1.0` tag, built with **teta-wot v0.1.0's `Cargo.lock`** and toolchain (1.98.1). `reference-server` serves the twin Things with teta-wot's command line; `json-corpus` writes `tests/json/serde_json_corpus.txt`. |
| `scenarios/http/` | Scripted HTTP scenarios: a server configuration and a list of steps. |
| `fixtures/http/` | The reference's answers to each scenario, normalised. Generated: never edit by hand. |
| `reference-rs/src/bin/schema_corpus.rs` | `schema-corpus`: for 54 Rust types, schemars' JSON Schema and teta-wot's DataSchema, written to `tests/td/schema_corpus.jsonl` for the C++ conversion and codec tests. |
| `reference-rs/src/bin/wire_corpus.rs` | `wire-corpus`: request bodies through `serde_json::from_slice` and path IDs through `Uuid::parse_str`, as teta-wot reads them, written to `tests/http/wire_corpus.jsonl` for the C++ body parser and ID parser. |
| `reference-rs/src/bin/validation_corpus.rs` | `validation-corpus`: teta-wot's validator run over schemas (primitives, bounds, patterns, arrays, tuples, models, maps, unions, literals, structs and enums) and inputs, each result written to `tests/validate/validation_corpus.jsonl` for the C++ validator's replay. |
| `twins/` | The twins as C++ Things (`teta_wot_twins`, defined with `ThingSpec`), registered under the reference's class names, for the C++ twin server and the tests that compare them with the reference: their TDs, and every HTTP fixture in-process (`tests/http/fixtures_test.cpp`). |
| `twin-server-cpp/` | The C++ twin server (`twin-server`): the twins' registry with teta-wot-cpp's command line (`tw::cli::serve_from_cli`), for `run.py`. CTest runs `run.py check` against it (`conformance.http`, `conformance.cli`). |
| `scenarios/cli/`, `fixtures/cli/` | The command line: arguments, configurations, exit codes, output and the fallback server (`run.py --cli`). |
| `run.py` | The harness: `capture`, `check` and `diff` (see its docstring). Python 3, standard library only. |
| `browse_mdns.py` | Finds a server's DNS-SD advertisement with Python's `zeroconf` and checks its TXT record (`conformance.mdns`; skipped without it). |
| `validate_openapi.py` | Validates every OpenAPI document the C++ servers serve (each scenario configuration, each example) with `openapi-spec-validator` (`conformance.openapi`; skipped without it). |

## The twins

Things served by both implementations under the same registry names, so one configuration file configures either. They are ported from the `wot` repository's twins as phases need them.

| Name in configurations | Thing | What it exercises |
|---|---|---|
| `labthings_fastapi.example_things:MyThing` | `MyThing` | Properties, actions with many parameter shapes, no-argument and `**kwargs` actions |
| `labthings_fastapi.example_things:ThingWithBrokenAffordances` | `ThingWithBrokenAffordances` | A failing property and action |
| `labthings_fastapi.example_things:ThingThatCantStart` | `ThingThatCantStart` | A Thing whose start fails (command line) |
| `twins.counter:TestThing` | `TestThing` | The LabThings quickstart counter |
| `twins.types_probe:TypesThing` | `TypesThing` | DataSchema conversion: constraints, optionals, literals, nested models, lists, tuples, maps |
| `twins.http_probe:HttpProbe` | `HttpProbe` | A falsy output, a cancellable action, an anticipated error, a writable property for the global lock |
| `twins.settings_probe:SettingsProbe` | `SettingsProbe` | Settings of every kind |
| `twins.ws_probe:ThingWithProperties` | `ThingWithProperties` | Observation: observable and functional properties, actions ending in each status |
| `twins.blob_probe:BlobProbe` | `BlobProbe` | Blobs in and out, and an MJPEG stream |
| `twins.ndarray_probe:NDArrayProbe` | `NDArrayProbe` | Arrays as properties and action inputs and outputs |
| `twins.event_probe:EventProbe` | `EventProbe` | Events emitted by actions, and an observable property (teta-wot-cpp's own: none of the `wot` twins has events) |

They are ported from the `wot` repository's `conformance/twin-server` to teta-wot v0.1.0's API (the only changes are the crate paths). The C++ ports (`twins/`) joined phase by phase: seven in Phase 3, `blob_probe` with Blobs (Phase 7), and `ndarray_probe` with arrays (Phase 10); every one's TD equals the reference's. `event_probe` was added in Phase 6, to both implementations, for events.

| Scenario | What it captures |
|---|---|
| `counter` | The counter over HTTP: TD, redirect, property read and refused write, an invocation, lists, 404, CORS preflight |
| `tds` | Every twin's TD (`tetathing` profile) |
| `tds-wot` | Every twin's TD in the `wot` profile |
| `validation` | Property writes and invocations with coercible, invalid and malformed bodies (pydantic's 422s) |
| `default` | The HTTP binding: routes, redirects, 404/405 and `allow`, `HEAD`, properties (read, write, reset, all, several), bodies (missing, malformed, too large), invocations (each ending, outputs, lists, cancellation), IDs, CORS, `Host` |
| `locked` | The global lock: writes, resets and invocations while an action holds it, and after |
| `prefixed` | An API prefix (`/api/v1`): every route and URL under it |
| `wot` | The `wot` profile's answers: 204s, problems, `ActionStatus` |
| `settings` | Settings files: none before a change, every kind of setting saved (numbers, Unicode, escapes, maps, structs, optionals, a functional setting), refused writes, several at once, resets, loading after restarts, a hand-edited file's warnings, a list, broken JSON, an empty object |
| `ws` | WebSockets: property and action observation (each ending), refusals (404, 403, the rest of the message skipped), bad messages ignored, duplicate subscriptions, two sockets, an upgrade for another path, the close at shutdown |
| `sse` | Server-sent events: a property, all properties, an event, all events, `Accept` among other ranges, a functional property read instead, the end at shutdown |
| `sse-wot` | Named events with `id`s in the `wot` profile |
| `events` | An event Thing's TD, events over the WebSocket, and a closed socket |
| `blobs` | Blobs: outputs from memory, a temporary file, text, CSV and a map of two; `/output` and downloads with their media types; Blob inputs found by absolute, foreign and relative `href`s; every kind of refused Blob input (wrong media type, unknown, malformed ID, no ID, missing fields, not an object); unknown and malformed Blob IDs; `HEAD` and `POST` |
| `blobs-wot` | Blobs in the `wot` profile, under an API prefix: `ActionStatus` outputs, downloads, refused inputs as problems |
| `streams` | MJPEG: the viewer page, frames to one and two clients, nothing more until a frame is added, a stopped stream ending its clients, a stream opened while stopped (it ends at once), a frame added while stopped (kept, but sent to no one), a restart, the end at shutdown, 404 and 405 |
| `openapi` | The OpenAPI document of every twin, byte for byte |
| `openapi-wot` | The same in the `wot` profile |
| `arrays` | Arrays: a matrix property read and written as matrices, cubes, scalars, empty and deep arrays; ragged and mixed lists (numpy's message), strings, objects, `null` refused; several properties at once; actions taking and returning arrays of any rank and of integers |
| `arrays-wot` | Arrays in the `wot` profile: 204s, ragged lists and bad elements as problems, `ActionStatus` outputs |
| `openapi-prefixed` | The same under an API prefix: the document stays at the root, its paths carry the prefix |
| `discovery` | `/.well-known/wot` and the directory: listings, collections, pages and their links, every kind of bad argument, retrieval by plain, escaped and odd IDs, `HEAD`, 405s, redirects |
| `discovery-wot` | The same in the `wot` profile under a prefix, and the profile's own 405 and 404 beside discovery's |
| `security-basic` | HTTP Basic: the public descriptions, refused reads, writes, invocations, events and WebSocket upgrades, wrong and odd credentials, the right ones, and 404, 405 and redirects before the challenge |
| `security-bearer` | A bearer token, in the `wot` profile under a prefix |
| `docs` | The docs routes: `/openapi.json`, `/docs`, `/docs/oauth2-redirect`, `/redoc`, with `HEAD`, 405s, redirects, CORS and a query; no assets without `TETA_WOT_DOCS_OFFLINE`; nothing served from the server's working directory (`workdir_files`) |
| `settings-load` | A server started with a settings file already there (as teta-wot writes it): loaded, not rewritten by loading, rewritten identically by the next save; no file for a Thing without settings |

Command-line scenarios (`scenarios/cli/`, run with `--cli`) run the server's command line and record its exit code and output (log lines aside), and, for those that serve, a scripted session: `help`, `unknown-argument`, `missing-value`, `invalid-port`, `repeated-argument`, `no-configuration`, `missing-file`, `both-configurations`, `invalid-json`, `not-an-object`, `invalid-configuration` (every kind of problem at once), `unknown-class`, `invalid-kwargs`, `args-and-kwargs`, `startup-failure`, `startup-failure-fallback` and `configuration-fallback` (the fallback page, its redirects and CORS), `serve`, `serve-json`, `port-in-use` and `port-in-use-fallback`.

Example scenarios (`scenarios/examples/`, run with `--examples`) compare an example served with `--serve` with its Rust original: its OpenAPI document, its TD and a scripted session. Their fixtures (`fixtures/examples/`) are captured from teta-wot v0.1.0's examples, built from its repository (`cargo build --release -p <example>`); CTest checks the C++ examples against them (`conformance.examples`). `openapi-export` doesn't serve: CTest compares the document it writes with the one teta-wot's example writes (`fixtures/examples/openapi-export/openapi.json`, `conformance.openapi-export`).

| Example scenario | What it captures |
|---|---|
| `quickstart-counter` | The Thing list, the TD, the counter, an invocation, a cancelled one |
| `light` | The TD, the functional `status` before and after `toggle`, a refused and an accepted brightness, read-only writes, reset, all properties |
| `counter-server` | The TD, an invocation, the lists |
| `http-walkthrough` | What `walkthrough.sh` does, plus an unknown field and a completed fade |
| `observe-websocket` | The oven's TD, its temperature and `heat_to` over the WebSocket, a refusal, the SSE stream |
| `events` | The laser's TD, a trip over the WebSocket and over SSE, the interlock's state |
| `sse-observe` | The thermometer's TD and stream (its readings depend on the time, so only the answer's headers) |
| `ndarray-property` | The camera's TD, its dark frame, corrections (and a refused shape), histograms, a ragged and an empty frame (teta-wot's example installs no log subscriber, so a failed invocation's log isn't compared) |
| `blob-camera` | The TD, captures from memory and from a file, their downloads' headers, `describe` with a found, an unknown and a mistyped Blob, the capture count (not the JPEGs, which differ: see the Phase 7 note) |
| `docs-offline` | The docs pages and their compiled-in assets (headers only: teta-wot's Windows build embeds CRLF copies), the lamp; only with `TETA_WOT_DOCS_OFFLINE` |
| `bearer-security` | The safe: refused without the token or with a wrong one, opened and locked with it, the public descriptions, a refused WebSocket |
| `node-wot-consumer` | The lamp in the `tetathing` profile, as node-wot uses it: an observed write, the synchronous action, a fade |
| `discovery` | The well-known TD, the directory and a page of it, both Things' TDs (the advertisement itself: `conformance.mdns`, and the example's demo) |
| `mjpeg-stream` | The TD, the viewer page, the stream's headers, `fps` and its bounds, a snapshot and its download's headers, `sharpness` (not the JPEGs, nor so their sizes) |

Every scenario runs against the C++ twin server (`conformance.http`).

## Running it

```text
cd conformance/reference-rs && cargo build --release --locked && cd ../..
python3 conformance/run.py capture --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --server <a server>
python3 conformance/run.py diff --server conformance/reference-rs/target/release/reference-server --server <a server>
python3 conformance/run.py check --server build/<preset>/conformance/twin-server-cpp/twin-server counter validation default locked prefixed wot settings settings-load ws sse sse-wot events blobs blobs-wot streams openapi openapi-wot openapi-prefixed docs discovery discovery-wot security-basic security-bearer
python3 conformance/validate_openapi.py --server build/<preset>/conformance/twin-server-cpp/twin-server --examples 'build/<preset>/examples/{name}/example-{name}'
python3 conformance/browse_mdns.py --server build/<preset>/conformance/twin-server-cpp/twin-server
python3 conformance/run.py capture --cli --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --cli --server build/<preset>/conformance/twin-server-cpp/twin-server
python3 conformance/run.py capture --examples --server '<teta-wot>/target/release/{name}'
python3 conformance/run.py check --examples --server 'build/<preset>/examples/{name}/example-{name}'
```

CI rebuilds the reference, re-captures every fixture and regenerates the serde_json, schema and validation corpora, and fails if anything changed.
