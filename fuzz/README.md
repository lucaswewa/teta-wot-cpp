# Fuzz targets

Each target feeds arbitrary bytes to one part of teta-wot-cpp and checks invariants that must hold whatever the input:

| Target | What it fuzzes | Invariants |
|---|---|---|
| `json` | `json::parse`, `dump`, `dump_pretty` | Refused with `ParseError`, or parsed; what `dump` writes parses back to the same value (floats within two ULPs: serde_json's decimals, ported, aren't always correctly rounded); the compact and indented texts parse alike |
| `validation` | `{"schema": ..., "input": ...}` documents: the TD model's DataSchemas and the validator | A value or a `ValidationError` with pydantic-shaped issues under `body`; a validated value validates again, unchanged. Each document that parses is also tried with its input replaced, whole and member by member, by awkward values |
| `websocket` | Text messages to a Thing's WebSocket | Only error responses, 404 or 403, for a name the message has; 403 only for the property that can't be observed (teta-wot's proptest in `observe.rs`) |
| `request_body` | Request bodies to every kind of route of a probe Thing, in both wire profiles, with eight content types | No 500; a real status; JSON wherever the answer says JSON |

## Running them

Without libFuzzer, each target is linked with [`driver.cpp`](driver.cpp), which runs the seeds in [`seeds/`](seeds/) and then mutations of them (bit flips, interesting bytes, insertions, deletions, splices, [`json.dict`](json.dict)'s tokens, awkward numbers), from a fixed random seed, so a run is repeatable. CTest runs each briefly (label `fuzz`); run one longer, with another seed:

```
build/windows-msvc/fuzz/fuzz-validation --iterations 1000000 --seed 7 --dict fuzz/json.dict fuzz/seeds/validation.seeds
```

A broken invariant, or an exception the target doesn't expect, prints the input and saves it as `failure-<target>.bin`; replay it with `fuzz-<target> --iterations 0 failure-<target>.bin`.

With Clang, `-DTETA_WOT_LIBFUZZER=ON` links them with libFuzzer instead, for coverage-guided fuzzing (with `-fsanitize=address` in the build's flags, it finds memory errors too):

```
python3 fuzz/make_seeds.py --dir corpus
build/linux-clang/fuzz/fuzz-request-body -dict=fuzz/json.dict -max_total_time=3600 corpus/request_body
```

## The seeds

[`make_seeds.py`](make_seeds.py) writes `seeds/<target>.seeds` (one seed per line: a JSON string, or `{"hex": ...}`) from the repository's corpora, which teta-wot produced, and the conformance scenarios' bodies and WebSocket messages. Run it again when they change.
