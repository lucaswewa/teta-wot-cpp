# Conformance

How teta-wot-cpp is held to wire-level parity with [teta-wot v0.1.0](https://github.com/lucaswewa/teta-wot/tree/v0.1.0) ([ADR-0001](../docs/adr/0001-parity-reference.md), [ADR-0011](../docs/adr/0011-verification.md), [wire contract](../docs/wire-contract.md)).

| Path | What |
|---|---|
| `reference-rs/` | The reference: a Rust crate depending on teta-wot at the `v0.1.0` tag, built with **teta-wot v0.1.0's `Cargo.lock`** and toolchain (1.98.1). `reference-server` serves the twin Things with teta-wot's command line; `json-corpus` writes `tests/json/serde_json_corpus.txt`. |
| `scenarios/http/` | Scripted HTTP scenarios: a server configuration and a list of steps. |
| `fixtures/http/` | The reference's answers to each scenario, normalised. Generated: never edit by hand. |
| `reference-rs/src/bin/schema_corpus.rs` | `schema-corpus`: for 54 Rust types, schemars' JSON Schema and teta-wot's DataSchema, written to `tests/td/schema_corpus.jsonl` for the C++ conversion and codec tests. |
| `reference-rs/src/bin/validation_corpus.rs` | `validation-corpus`: teta-wot's validator run over schemas (primitives, bounds, patterns, arrays, tuples, models, maps, unions, literals, the twins' action inputs) and inputs, each result written to `tests/validate/validation_corpus.jsonl` for the C++ validator's replay. |
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

They are ported from the `wot` repository's `conformance/twin-server` to teta-wot v0.1.0's API (the only changes are the crate paths). The C++ ports join phase by phase: Phase 1 describes all nine (their TDs); their behaviour follows from Phase 3 on.

| Scenario | What it captures |
|---|---|
| `counter` | The counter over HTTP: TD, redirect, property read and refused write, an invocation, lists, 404, CORS preflight |
| `tds` | Every twin's TD (`tetathing` profile) |
| `tds-wot` | Every twin's TD in the `wot` profile |
| `validation` | Property writes and invocations with coercible, invalid and malformed bodies (pydantic's 422s) |

## Running it

```text
cd conformance/reference-rs && cargo build --release --locked && cd ../..
python3 conformance/run.py capture --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --server <a server>
python3 conformance/run.py diff --server conformance/reference-rs/target/release/reference-server --server <a server>
```

CI rebuilds the reference, re-captures every fixture and regenerates the serde_json, schema and validation corpora, and fails if anything changed.
