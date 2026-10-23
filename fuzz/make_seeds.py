"""Writes the fuzz targets' seeds from the repository's corpora and scenarios.

    python3 fuzz/make_seeds.py              # fuzz/seeds/<target>.seeds, for the driver
    python3 fuzz/make_seeds.py --dir OUT    # OUT/<target>/<n>, a corpus directory for libFuzzer

A `.seeds` file holds one seed per line: a JSON string (the input as UTF-8),
or `{"hex": "..."}` for any bytes. The seeds are deterministic: run it again
after the corpora change, and commit the result.

- json: request bodies of the wire corpus (tests/http/wire_corpus.jsonl) and
  the scenarios' JSON bodies;
- validation: `{"schema": ..., "input": ...}` pairs from the validation
  corpus (tests/validate/validation_corpus.jsonl), a few inputs per schema;
- websocket: the scenarios' WebSocket messages, and the dialect's message
  types with each kind of name;
- request_body: a route-and-content-type byte (see request_body.cpp), then a
  body from the json seeds.
"""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent


def scenario_steps():
    for path in sorted((ROOT / "conformance" / "scenarios").rglob("*.json")):
        scenario = json.loads(path.read_text(encoding="utf-8"))
        yield from scenario.get("steps", [])


def json_seeds():
    seeds = []
    lines = (ROOT / "tests" / "http" / "wire_corpus.jsonl").read_text(encoding="utf-8").splitlines()
    records = [json.loads(line) for line in lines]
    for record in [r for r in records if "body" in r][::25]:
        seeds.append(bytes.fromhex(record["body"]))
    for step in scenario_steps():
        if "json" in step:
            seeds.append(json.dumps(step["json"]).encode())
    return unique(seeds)


def validation_seeds():
    schemas = {}
    inputs = {}
    for line in (ROOT / "tests" / "validate" / "validation_corpus.jsonl").read_text(encoding="utf-8").splitlines():
        entry = json.loads(line)
        if "schema" in entry:
            schemas[entry["schema_id"]] = entry["schema"]
        else:
            inputs.setdefault(entry["schema_id"], []).append(entry["input"])
    seeds = []
    for schema_id, schema in schemas.items():
        for text in inputs.get(schema_id, [])[:4]:
            try:
                value = json.loads(text)
            except ValueError:
                continue
            seeds.append(json.dumps({"schema": schema, "input": value}, separators=(",", ":")).encode())
    return unique(seeds)


def websocket_seeds():
    seeds = []
    for step in scenario_steps():
        if "send" in step:
            seeds.append(json.dumps(step["send"]).encode())
        if "send_text" in step:
            seeds.append(step["send_text"].encode())
    for message_type in ["addPropertyObservation", "addActionObservation", "addEventSubscription", "setProperty"]:
        for name in ["data", "computed", "act", "happened", "nope", ""]:
            seeds.append(json.dumps({"messageType": message_type, "data": {name: True}}).encode())
    return unique(seeds)


def request_body_seeds(bodies):
    # request_body.cpp: routes[choice % 24], content_types[(choice / 24) % 8],
    # the wot profile if choice & 0x80.
    seeds = []
    for i, body in enumerate(bodies[:200]):
        for choice in (i % 24, 24 + i % 24, 0x80 | (i % 24)):
            seeds.append(bytes([choice]) + body)
    return unique(seeds)


def unique(seeds):
    seen = set()
    return [s for s in seeds if not (s in seen or seen.add(s))]


def line(seed):
    try:
        return json.dumps(seed.decode("utf-8"), ensure_ascii=False)
    except UnicodeDecodeError:
        return json.dumps({"hex": seed.hex()})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", type=Path, help="write a libFuzzer corpus directory per target instead")
    args = parser.parse_args()
    bodies = json_seeds()
    targets = {
        "json": bodies,
        "validation": validation_seeds(),
        "websocket": websocket_seeds(),
        "request_body": request_body_seeds(bodies),
    }
    for name, seeds in targets.items():
        if args.dir:
            folder = args.dir / name
            folder.mkdir(parents=True, exist_ok=True)
            for n, seed in enumerate(seeds):
                (folder / f"{n:05}").write_bytes(seed)
        else:
            out = HERE / "seeds" / f"{name}.seeds"
            out.parent.mkdir(exist_ok=True)
            out.write_text("".join(line(s) + "\n" for s in seeds), encoding="utf-8", newline="\n")
        print(f"{name}: {len(seeds)} seeds")


if __name__ == "__main__":
    main()
