# Conformance

How teta-wot-cpp is held to wire-level parity with [teta-wot v0.1.0](https://github.com/lucaswewa/teta-wot/tree/v0.1.0) ([ADR-0001](../docs/adr/0001-parity-reference.md), [ADR-0011](../docs/adr/0011-verification.md), [wire contract](../docs/wire-contract.md)).

| Path | What |
|---|---|
| `reference-rs/` | The reference: a Rust crate depending on teta-wot at the `v0.1.0` tag, built with **teta-wot v0.1.0's `Cargo.lock`** and toolchain (1.98.1). `reference-server` serves the twin Things with teta-wot's command line; `json-corpus` writes `tests/json/serde_json_corpus.txt`. |
| `scenarios/http/` | Scripted HTTP scenarios: a server configuration and a list of steps. |
| `fixtures/http/` | The reference's answers to each scenario, normalised. Generated: never edit by hand. |
| `run.py` | The harness: `capture`, `check` and `diff` (see its docstring). Python 3, standard library only. |

## The twins

Things served by both implementations under the same registry names, so one configuration file configures either. They are ported from the `wot` repository's twins as phases need them.

| Name in configurations | Thing | Since |
|---|---|---|
| `twins.counter:TestThing` | The LabThings quickstart counter | Phase 0 |

## Running it

```text
cd conformance/reference-rs && cargo build --release --locked && cd ../..
python3 conformance/run.py capture --server conformance/reference-rs/target/release/reference-server
python3 conformance/run.py check --server <a server>
python3 conformance/run.py diff --server conformance/reference-rs/target/release/reference-server --server <a server>
```

CI rebuilds the reference, re-captures every fixture and regenerates the serde_json corpus, and fails if anything changed.
