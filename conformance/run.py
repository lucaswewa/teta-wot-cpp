"""The parity harness: scripted HTTP scenarios against real servers.

    python3 conformance/run.py capture --server PATH [SCENARIO ...]
    python3 conformance/run.py check   --server PATH [SCENARIO ...]
    python3 conformance/run.py diff    --server PATH --server PATH [SCENARIO ...]

`capture` runs each scenario against the reference server (teta-wot v0.1.0,
`conformance/reference-rs`) and writes its golden fixture to
`conformance/fixtures/http/<scenario>.json`. `check` runs it against any
server and compares the answers with the fixture. `diff` runs it against two
servers and compares their answers with each other. Scenarios are
`conformance/scenarios/http/*.json`; with none named, all of them run.

A server is any executable with teta-wot's command line: it is started as
`PATH -c CONFIG --host 127.0.0.1 --port PORT`, with the scenario's
configuration (and a temporary settings folder), and stopped with Ctrl-C.

Every request is sent over a real socket with `Host: testserver`, so the URLs
the server builds don't depend on the port. Redirects aren't followed.

Steps. Each is `{"name", "method", "path"}` with optional `headers`, `json`
(a body, sent as JSON) or `body` (a string), and `wait` (poll the invocation
the step created until it has finished, and record its final state). Paths
and bodies may use `{last}` (the most recent invocation's ID) and `{inv:N}`
(the N-th, from 0).

Normalisation, as the `wot` repository's fixtures (ADR-0011):

- UUIDs become `<uuid:N>`, numbered by first appearance in the scenario;
- timestamps become `<naive-datetime>` or `<utc-datetime>`;
- in log records, `lineno`, `filename` and a non-null `traceback` are masked;
- in the 201 answer to an invocation, the run state (`status`, times,
  `output`, `log`, `error`) becomes `<race>`, since how far it has run when
  it is serialised is a race; its final state is compared after waiting;
- only the headers in HEADERS are kept, with an `allow`'s methods sorted.
  Header order and the transport headers (`date`, `content-length`,
  `connection`, `transfer-encoding`) aren't contract (phase-00 note).

The body is recorded twice: `body`, parsed, for reading, and `text`, the
exact bytes after normalisation, which is what makes number formatting, key
order and escaping part of the comparison.
"""

import argparse
import difflib
import http.client
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCENARIOS = HERE / "scenarios" / "http"
FIXTURES = HERE / "fixtures" / "http"

HOST = "testserver"
NIL = "00000000-0000-0000-0000-000000000000"
HEADERS = [
    "content-type",
    "location",
    "allow",
    "access-control-allow-origin",
    "access-control-allow-credentials",
    "access-control-allow-methods",
    "access-control-allow-headers",
    "access-control-max-age",
    "access-control-expose-headers",
    "vary",
    "cache-control",
    "www-authenticate",
    "link",
    "etag",
]
RUN_STATE = ["status", "timeStarted", "timeCompleted", "timeEnded", "output", "log", "error"]

UUID = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")
NAIVE = r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,6})?"
UTC = NAIVE + r"(?:Z|\+00:00)"


class Normaliser:
    """Masks what differs from run to run, consistently within a scenario."""

    def __init__(self):
        self.uuids = {}

    def uuid(self, match):
        found = match.group(0)
        if found == NIL:
            return "<nil-uuid>"
        return self.uuids.setdefault(found, f"<uuid:{len(self.uuids)}>")

    def string(self, text):
        if re.fullmatch(UTC, text):
            return "<utc-datetime>"
        if re.fullmatch(NAIVE, text):
            return "<naive-datetime>"
        return UUID.sub(self.uuid, text)

    def text(self, raw):
        """The raw body, with JSON strings that are timestamps masked."""
        raw = re.sub(f'"{UTC}"', '"<utc-datetime>"', raw)
        raw = re.sub(f'"{NAIVE}"', '"<naive-datetime>"', raw)
        return UUID.sub(self.uuid, raw)

    def value(self, value):
        if isinstance(value, str):
            return self.string(value)
        if isinstance(value, list):
            return [self.value(v) for v in value]
        if isinstance(value, dict):
            out = {k: self.value(v) for k, v in value.items()}
            if {"levelname", "levelno", "lineno"} <= out.keys():
                out["lineno"] = "<lineno>"
                out["filename"] = "<filename>"
                if out.get("traceback") is not None:
                    out["traceback"] = "<traceback>"
            return out
        return value


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    """A server process, started with a configuration and stopped with Ctrl-C."""

    def __init__(self, executable, config):
        self.folder = tempfile.TemporaryDirectory()
        config = dict(config)
        config.setdefault("settings_folder", str(Path(self.folder.name) / "settings"))
        config_path = Path(self.folder.name) / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        self.port = free_port()
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        self.log = open(Path(self.folder.name) / "server.log", "wb")
        self.process = subprocess.Popen(
            [executable, "-c", str(config_path), "--host", "127.0.0.1", "--port", str(self.port)],
            stdout=self.log, stderr=subprocess.STDOUT, creationflags=flags)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"{executable} exited with {self.process.returncode}: {self.output()}")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        self.stop()
        raise TimeoutError(f"{executable} didn't listen on {self.port}")

    def output(self):
        self.log.flush()
        return (Path(self.folder.name) / "server.log").read_text(errors="replace")

    def stop(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.close()
        self.folder.cleanup()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        try:
            all_headers = {"Host": HOST, **(headers or {})}
            conn.request(method, path, body=body, headers=all_headers)
            response = conn.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
        finally:
            conn.close()


def record(status, headers, raw, normaliser, race=False):
    kept = {h: headers[h] for h in HEADERS if h in headers}
    if "allow" in kept:
        kept["allow"] = ", ".join(sorted(kept["allow"].split(", ")))
    content_type = headers.get("content-type", "")
    entry = {"status": status, "headers": normaliser.value(kept)}
    if not raw:
        entry["body"] = None
    elif "json" in content_type:
        text = raw.decode("utf-8")
        body = normaliser.value(json.loads(text))
        if race and isinstance(body, dict) and "status" in body:
            for key in RUN_STATE:
                if key in body:
                    body[key] = "<race>"
            entry["body"] = body
        else:
            entry["body"] = body
            entry["text"] = normaliser.text(text)
    elif content_type.startswith("text/"):
        entry["body"] = normaliser.text(raw.decode("utf-8"))
    else:
        entry["body"] = {"hex": raw.hex()}
    return entry


def run(executable, scenario):
    """Runs one scenario against one server; returns what it recorded."""
    server = Server(executable, scenario["config"])
    try:
        normaliser = Normaliser()
        ids = []
        prefix = scenario["config"].get("api_prefix", "")
        steps = []

        def resolve(value):
            if isinstance(value, list):
                return [resolve(v) for v in value]
            if isinstance(value, dict):
                return {k: resolve(v) for k, v in value.items()}
            if not isinstance(value, str):
                return value
            value = value.replace("{last}", ids[-1] if ids else NIL)
            return re.sub(r"\{inv:(\d+)\}", lambda m: ids[int(m.group(1))], value)

        for step in scenario["steps"]:
            headers = dict(step.get("headers", {}))
            body = None
            if "json" in step:
                body = json.dumps(resolve(step["json"]))
                headers.setdefault("Content-Type", "application/json")
            elif "body" in step:
                body = step["body"]
            status, answer_headers, raw = server.request(step["method"], resolve(step["path"]), body, headers)
            created = None
            if status == 201 and "json" in answer_headers.get("content-type", ""):
                parsed = json.loads(raw)
                if isinstance(parsed, dict) and "id" in parsed:
                    created = parsed["id"]
                    ids.append(created)
            entry = {"step": step, "response": record(status, answer_headers, raw, normaliser, race=created is not None)}
            if step.get("wait") and created:
                deadline = time.monotonic() + 15
                while True:
                    final = server.request("GET", f"{prefix}/action_invocations/{created}")
                    if json.loads(final[2]).get("status") not in ("pending", "running"):
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError(created)
                    time.sleep(0.02)
                entry["final"] = record(*final, normaliser)
            steps.append(entry)
        return steps
    finally:
        server.stop()


def load(names):
    paths = [SCENARIOS / f"{n}.json" for n in names] if names else sorted(SCENARIOS.glob("*.json"))
    return [(p.stem, json.loads(p.read_text(encoding="utf-8"))) for p in paths]


def dumps(value):
    return json.dumps(value, indent=2, ensure_ascii=False) + "\n"


def compare(label_a, a, label_b, b):
    """Prints a unified diff; returns whether the two are equal."""
    if a == b:
        return True
    sys.stdout.writelines(difflib.unified_diff(
        dumps(a).splitlines(keepends=True), dumps(b).splitlines(keepends=True), label_a, label_b))
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["capture", "check", "diff"])
    parser.add_argument("--server", action="append", required=True, help="a server executable")
    parser.add_argument("scenarios", nargs="*")
    args = parser.parse_args()
    if args.mode == "diff" and len(args.server) != 2:
        parser.error("diff needs two --server")
    if args.mode != "diff" and len(args.server) != 1:
        parser.error(f"{args.mode} needs one --server")

    failed = []
    for name, scenario in load(args.scenarios):
        if args.mode == "capture":
            fixture = {"config": scenario["config"], "steps": run(args.server[0], scenario)}
            FIXTURES.mkdir(parents=True, exist_ok=True)
            path = FIXTURES / f"{name}.json"
            path.write_text(dumps(fixture), encoding="utf-8", newline="\n")
            print(f"captured {name}: {path.relative_to(HERE.parent)}")
        elif args.mode == "check":
            fixture = json.loads((FIXTURES / f"{name}.json").read_text(encoding="utf-8"))
            ok = compare(f"fixture {name}", fixture["steps"], f"server {name}", run(args.server[0], scenario))
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failed.append(name)
        else:
            a = run(args.server[0], scenario)
            b = run(args.server[1], scenario)
            ok = compare(args.server[0], a, args.server[1], b)
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failed.append(name)
    if failed:
        print(f"{len(failed)} scenario(s) differ: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
