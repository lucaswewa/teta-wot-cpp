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

They are ported from the `wot` repository's `conformance/twin-server` to teta-wot v0.1.0's API (the only changes are the crate paths). The C++ ports (`twins/`) join phase by phase: since Phase 3, seven are C++ Things whose TDs equal the reference's; `blob_probe` and `ndarray_probe` follow with Blobs (Phase 7) and arrays (Phase 10).

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
| `settings-load` | A server started with a settings file already there (as teta-wot writes it): loaded, not rewritten by loading, rewritten identically by the next save; no file for a Thing without settings |

Command-line scenarios (`scenarios/cli/`, run with `--cli`) run the server's command line and record its exit code and output (log lines aside), and, for those that serve, a scripted session: `help`, `unknown-argument`, `missing-value`, `invalid-port`, `repeated-argument`, `no-configuration`, `missing-file`, `both-configurations`, `invalid-json`, `not-an-object`, `invalid-configuration` (every kind of problem at once), `unknown-class`, `invalid-kwargs`, `args-and-kwargs`, `startup-failure`, `startup-failure-fallback` and `configuration-fallback` (the fallback page, its redirects and CORS), `serve`, `serve-json`, `port-in-use` and `port-in-use-fallback`.

Example scenarios (`scenarios/examples/`, run with `--examples`) compare an example served with `--serve` with its Rust original: its TD and a scripted session. Their fixtures (`fixtures/examples/`) are captured from teta-wot v0.1.0's examples, built from its repository (`cargo build --release -p <example>`); CTest checks the C++ examples against them (`conformance.examples`).

| Example scenario | What it captures |
|---|---|
| `quickstart-counter` | The Thing list, the TD, the counter, an invocation, a cancelled one |
| `light` | The TD, the functional `status` before and after `toggle`, a refused and an accepted brightness, read-only writes, reset, all properties |
| `counter-server` | The TD, an invocation, the lists |
| `http-walkthrough` | What `walkthrough.sh` does, plus an unknown field and a completed fade |

`tds` and `tds-wot` need `blob_probe` and `ndarray_probe`, which the C++ twin server serves from Phases 7 and 10; the other scenarios run against it.

## Running it

```text
cd conformance/reference-rs && cargo build --release --locked && cd ../..
python3 conformance/run.py capture --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --server <a server>
python3 conformance/run.py diff --server conformance/reference-rs/target/release/reference-server --server <a server>
python3 conformance/run.py check --server build/<preset>/conformance/twin-server-cpp/twin-server counter validation default locked prefixed wot settings settings-load
python3 conformance/run.py capture --cli --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --cli --server build/<preset>/conformance/twin-server-cpp/twin-server
python3 conformance/run.py capture --examples --server '<teta-wot>/target/release/{name}'
python3 conformance/run.py check --examples --server 'build/<preset>/examples/{name}/example-{name}'
```

CI rebuilds the reference, re-captures every fixture and regenerates the serde_json, schema and validation corpora, and fails if anything changed.
