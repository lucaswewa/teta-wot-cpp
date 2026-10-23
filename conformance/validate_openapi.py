"""Validates the OpenAPI documents a server serves with openapi-spec-validator.

    python3 conformance/validate_openapi.py --server PATH [--examples PATTERN]

The twin server is started with every scenario configuration in
`conformance/scenarios/http/`, and,
with `--examples`, each example that serves (a pattern with `{name}`, as for
`run.py --examples`); each `/openapi.json` must be a valid OpenAPI 3.1
document. Exits 77 when openapi-spec-validator isn't installed
(`pip install openapi-spec-validator`), which CTest reports as skipped.
"""

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run  # noqa: E402

try:
    from openapi_spec_validator import validate
except ImportError:
    print("openapi-spec-validator isn't installed: skipped")
    sys.exit(77)



def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--server", required=True, help="the twin server")
    parser.add_argument("--examples", help="the examples, a pattern with {name}")
    args = parser.parse_args()
    targets = []
    for path in sorted((HERE / "scenarios" / "http").glob("*.json")):
        scenario = json.loads(path.read_text(encoding="utf-8"))
        targets.append((path.stem, args.server, scenario))
    if args.examples:
        for path in sorted((HERE / "scenarios" / "examples").glob("*.json")):
            executable = args.examples.format(name=path.stem)
            if Path(executable).exists():
                targets.append((path.stem, executable, json.loads(path.read_text(encoding="utf-8"))))
    failures = 0
    for name, executable, scenario in targets:
        server = run.Server(executable, {key: value for key, value in scenario.items() if key != "steps"})
        try:
            _, _, raw = server.request("GET", "/openapi.json")
        finally:
            server.stop()
        document = json.loads(raw)
        try:
            validate(document)
            print(f"valid    {name}: {len(document['paths'])} paths")
        except Exception as error:  # noqa: BLE001 - every kind of invalid document
            failures += 1
            print(f"INVALID  {name}: {error}")
    print(f"{len(targets)} documents, {failures} invalid")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
