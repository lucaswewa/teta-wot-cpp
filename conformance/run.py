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
A scenario's `files` (`{"thing/Settings-Class.json": "text"}`) are written
into the settings folder before the server starts.

With `--examples`, the scenarios are `conformance/scenarios/examples/*.json`
and the fixtures `conformance/fixtures/examples/`: each scenario is named
after an example, and instead of a configuration it has `serve`, the
arguments that make the example serve (`["--serve"]`), and `port`, where it
listens. A server is then a pattern, `{name}` standing for the example's
name, such as `target/release/{name}` for teta-wot's examples or
`build/linux-gcc/examples/{name}/example-{name}` for teta-wot-cpp's. This
compares each example with its Rust original (plan §6.1).

With `--cli`, the scenarios are `conformance/scenarios/cli/*.json` and the
fixtures `conformance/fixtures/cli/`: each runs the server's command line
with `args`, in which `{config}` is the path of a file holding the
scenario's `config` (with a temporary settings folder), `{settings}` that
folder (escaped for a JSON string, for `-j`) and `{port}` a free port. `occupy` makes something else listen on that port first. With
`steps`, the server is expected to serve: they run once it listens (with
`"fallback": true`, against the fallback server), and then it is stopped
with Ctrl-C. The exit code and the output are recorded, without log lines
(their format isn't contract), with the port, the temporary folder and the
program's name masked, and with `Registered: …` lists masked (the C++ twins
lack the Blob and array probes until Phases 7 and 10).

Every request is sent over a real socket with `Host: testserver`, so the URLs
the server builds don't depend on the port. Redirects aren't followed.

Steps. Each is `{"name", "method", "path"}` with optional `headers`, `json`
(a body, sent as JSON), `body` (a string) or `body_repeat` (`[text, count]`:
the text repeated, for large bodies), `wait` (poll the invocation the step
created until it has finished, and record its final state) and `poll`
(repeat the request until its answer's `status` is no longer `pending` or
`running`, or with `until`, is one of those listed, and record that
answer). Paths and bodies may use `{last}` (the
most recent invocation's ID) and `{inv:N}` (the N-th, from 0).

Observation steps, instead of a request:

- `{"name", "websocket": "/thing/ws", "socket": "a"}` opens a WebSocket
  (recording 101, or the answer if the server refuses the upgrade); then
  steps with `"socket": "a"` and `"send": {...}` (JSON) or `"send_text"`
  send a message, `"receive": N` records the next N messages, `"quiet": S`
  records what arrives within S seconds (usually nothing), `"closed": true`
  records how the server closes the socket, and `"close": true` closes it;
- `{"name", "events": "/thing/property", "stream": "s"}` opens a
  server-sent events stream (recording the answer's status and headers); then
  steps with `"stream": "s"` and `"receive": N` record the next N events
  (`event`, `id`, `data`), and `"ended": true` waits for the stream's end.

The handshake's own headers (`upgrade`, `sec-websocket-accept`, …) are the
transport's, and aren't recorded.

Settings steps, instead of a request:

- `{"name", "file": "thing/Settings-Class.json"}` records that file of the
  settings folder: whether it exists, its text with line ends as `\\n`, and
  whether the scenario or the server wrote it last; for the server's, whether
  its line ends are the platform's (CRLF on Windows, LF elsewhere);
- `{"name", "write_file": "thing/Settings-Class.json", "text": "..."}`
  writes a file there;
- `{"name", "restart": true}` stops the server and starts it again, with the
  same settings folder;
- `{"name", "warnings": true}` records the settings and slot warnings the
  server has logged since it last started, without their log line's format,
  with the settings folder as `<settings>` and paths written with `/`.

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
import base64
import difflib
import http.client
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent

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
NAIVE = r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,9})?"
UTC = NAIVE + r"(?:Z|\+00:00)"

ANSI = re.compile(r"\x1b\[[0-9;]*m")
# A console log line, teta-wot's (`time LEVEL target: message fields`) or
# teta-wot-cpp's (`time LEVEL message`).
LOG_LINE = re.compile(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?Z\s+(?:TRACE|DEBUG|INFO|WARN|ERROR) ")
# The warnings `warnings` steps record, by how they start.
WARNINGS = [
    "Could not load setting ",
    "An extra key ",
    "Error loading settings for ",
    "the configuration key `",
    "`thing_slots` of `",
    "the slots of ",
]


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
        """The raw body, with JSON strings that are timestamps masked, and log
        record locations, as in `value`."""
        raw = re.sub(f'"{UTC}"', '"<utc-datetime>"', raw)
        raw = re.sub(f'"{NAIVE}"', '"<naive-datetime>"', raw)
        raw = re.sub(r'"lineno":\d+', '"lineno":"<lineno>"', raw)
        raw = re.sub(r'"filename":"(?:[^"\\]|\\.)*"', '"filename":"<filename>"', raw)
        raw = re.sub(r'"traceback":"(?:[^"\\]|\\.)*"', '"traceback":"<traceback>"', raw)
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


def wait_listening(process, port, executable, output):
    """Waits until something listens on `port`; raises if the process ends
    first, or nothing listens within 20 s."""
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{executable} exited with {process.returncode}: {output()}")
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"{executable} didn't listen on {port}")


def interrupt(process):
    """Stops a process as Ctrl-C (Ctrl-Break on Windows) would."""
    if process.poll() is None:
        process.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def new_group():
    return subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0


def request(port, method, path, body=None, headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=15)
    try:
        all_headers = {"Host": HOST, **(headers or {})}
        if isinstance(body, str):
            body = body.encode("utf-8")
        conn.request(method, path, body=body, headers=all_headers)
        response = conn.getresponse()
        return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
    finally:
        conn.close()


class Server:
    """A server process, started with a configuration (or, for an example,
    with its `serve` arguments) and stopped with Ctrl-C."""

    def __init__(self, executable, scenario):
        self.executable = str(Path(executable).resolve())
        self.scenario = scenario
        self.folder = tempfile.TemporaryDirectory()
        self.settings = Path(self.folder.name) / "settings"
        self.written = {}
        for name, text in scenario.get("files", {}).items():
            self.write(name, text)
        self.log_path = Path(self.folder.name) / "server.log"
        self.log = None
        try:
            self.start()
        except BaseException:
            self.folder.cleanup()
            raise

    def start(self):
        if "serve" in self.scenario:
            self.port = self.scenario.get("port", 5000)
            command = [self.executable, *self.scenario["serve"]]
        else:
            config = dict(self.scenario["config"])
            config.setdefault("settings_folder", str(self.settings))
            config_path = Path(self.folder.name) / "config.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            self.port = free_port()
            command = [self.executable, "-c", str(config_path), "--host", "127.0.0.1", "--port", str(self.port)]
        self.log = open(self.log_path, "wb")
        self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT, creationflags=new_group())
        try:
            wait_listening(self.process, self.port, self.executable, self.output)
        except BaseException:
            interrupt(self.process)
            self.log.close()
            raise

    def output(self):
        self.log.flush()
        return self.log_path.read_text(encoding="utf-8", errors="replace")

    def restart(self):
        interrupt(self.process)
        self.log.close()
        self.start()

    def write(self, name, text):
        path = self.settings / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))
        self.written[name] = text.encode("utf-8")

    def file(self, name):
        path = self.settings / name
        if not path.exists():
            return {"exists": False}
        raw = path.read_bytes()
        lf = raw.decode("utf-8").replace("\r\n", "\n")
        if raw == self.written.get(name):
            # As the scenario wrote it: its line ends are the scenario's.
            return {"exists": True, "written_by": "scenario", "text": lf}
        crlf = raw.count(b"\r\n")
        native = crlf == lf.count("\n") if os.name == "nt" else crlf == 0
        return {"exists": True, "written_by": "server", "native_newlines": native, "text": lf}

    def warnings(self):
        """The warnings logged since the server started, normalised."""
        found = []
        lines = ANSI.sub("", self.output()).replace("\r\n", "\n").split("\n")
        i = 0
        while i < len(lines):
            line = lines[i]
            starts = [line.find(w) for w in WARNINGS if w in line]
            i += 1
            if not starts:
                continue
            message = line[min(starts):]
            while i < len(lines) and lines[i].startswith("  ") and not LOG_LINE.match(lines[i]):
                message += "\n" + lines[i]
                i += 1
            message = re.sub(r" thing=\S+$", "", message)
            message = message.replace(str(self.settings), "<settings>")
            message = re.sub(r"<settings>\S*?\.json", lambda m: m.group(0).replace("\\", "/"), message)
            found.append(message)
        return found

    def stop(self):
        interrupt(self.process)
        self.log.close()
        self.folder.cleanup()

    def request(self, method, path, body=None, headers=None):
        return request(self.port, method, path, body, headers)


class WebSocketClient:
    """A minimal WebSocket client (RFC 6455): text messages, pings and closes."""

    def __init__(self, port, path, headers):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        lines = [f"GET {path} HTTP/1.1", f"Host: {HOST}", "Upgrade: websocket", "Connection: Upgrade",
                 f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13"]
        lines += [f"{k}: {v}" for k, v in headers.items()]
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            self.buffer += chunk
        head, _, self.buffer = self.buffer.partition(b"\r\n\r\n")
        status_line, *header_lines = head.decode("latin-1").split("\r\n")
        self.status = int(status_line.split()[1])
        self.headers = {}
        for line in header_lines:
            name, _, value = line.partition(":")
            self.headers[name.strip().lower()] = value.strip()
        self.body = b""
        if self.status != 101:
            length = int(self.headers.get("content-length", "0"))
            while len(self.buffer) < length:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.buffer += chunk
            self.body = self.buffer[:length]
            self.sock.close()
        self.closed = self.status != 101

    def send_frame(self, opcode, payload):
        mask = os.urandom(4)
        header = bytes([0x80 | opcode])
        if len(payload) < 126:
            header += bytes([0x80 | len(payload)])
        elif len(payload) < 65536:
            header += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", len(payload))
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def send_text(self, text):
        self.send_frame(0x1, text.encode("utf-8"))

    def read(self, count, deadline):
        while len(self.buffer) < count:
            self.sock.settimeout(max(deadline - time.monotonic(), 0.001))
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("closed without a close frame")
            self.buffer += chunk
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def receive(self, timeout):
        """The next text message, `("close", code, reason)`, or None on a
        timeout."""
        deadline = time.monotonic() + timeout
        message = b""
        while True:
            head = self.read(2, deadline)
            if head is None:
                return None
            opcode, length = head[0] & 0x0F, head[1] & 0x7F
            if length == 126:
                length = struct.unpack(">H", self.read(2, deadline + 5))[0]
            elif length == 127:
                length = struct.unpack(">Q", self.read(8, deadline + 5))[0]
            payload = self.read(length, deadline + 5) if length else b""
            if opcode == 0x9:
                self.send_frame(0xA, payload)
            elif opcode == 0x8:
                self.closed = True
                code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else 1005
                return ("close", code, payload[2:].decode("utf-8"))
            elif opcode in (0x1, 0x0):
                message += payload
                if head[0] & 0x80:
                    return message.decode("utf-8")

    def close(self):
        if not self.closed:
            try:
                self.send_frame(0x8, struct.pack(">H", 1000))
            except OSError:
                pass
        self.sock.close()


# The end of a server-sent events stream.
END = object()


class EventStreamClient:
    """A server-sent events stream, read line by line."""

    def __init__(self, port, path, headers):
        self.conn = http.client.HTTPConnection("127.0.0.1", port, timeout=15)
        self.conn.request("GET", path, headers={"Host": HOST, "Accept": "text/event-stream", **headers})
        self.response = self.conn.getresponse()
        self.status = self.response.status
        self.headers = {k.lower(): v for k, v in self.response.getheaders()}

    def receive(self, timeout):
        """The next event as `{event, id, data}`, END at the end of the
        stream, or None on a timeout."""
        self.conn.sock.settimeout(timeout)
        fields = {}
        while True:
            try:
                line = self.response.fp.readline()
            except (socket.timeout, TimeoutError):
                return None
            if not line:
                return END
            line = line.decode("utf-8").rstrip("\r\n")
            if line == "":
                if "data" in fields:
                    return fields
                fields = {}
                continue
            if line.startswith(":"):
                continue
            name, _, value = line.partition(":")
            value = value[1:] if value.startswith(" ") else value
            fields[name] = fields[name] + "\n" + value if name == "data" and "data" in fields else value

    def close(self):
        self.conn.close()


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


def run_steps(server, scenario, prefix):
    """Runs a scenario's steps against a running server (anything with
    `request`); returns what they recorded."""
    normaliser = Normaliser()
    ids = []
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

    sockets = {}
    streams = {}

    def message(received):
        if received is None:
            return "<timeout>"
        if received is END:
            return "<end>"
        if isinstance(received, tuple):
            return {"close": {"code": received[1], "reason": received[2]}}
        if isinstance(received, str):
            return {"body": normaliser.value(json.loads(received)), "text": normaliser.text(received)}
        data = received["data"]
        try:
            parsed = normaliser.value(json.loads(data))
        except ValueError:
            parsed = data
        return {"event": received.get("event"), "id": normaliser.string(received["id"]) if "id" in received else None,
                "data": parsed, "text": normaliser.text(data)}

    try:
        return run_step_list(server, scenario, prefix, normaliser, ids, steps, resolve, sockets, streams, message)
    finally:
        for client in [*sockets.values(), *streams.values()]:
            client.close()


def run_step_list(server, scenario, prefix, normaliser, ids, steps, resolve, sockets, streams, message):
    for step in scenario["steps"]:
        if "websocket" in step:
            ws = WebSocketClient(server.port, resolve(step["websocket"]), step.get("headers", {}))
            sockets[step["socket"]] = ws
            if ws.status == 101:
                steps.append({"step": step, "status": 101})
            else:
                steps.append({"step": step, "response": record(ws.status, ws.headers, ws.body, normaliser)})
            continue
        if "socket" in step:
            ws = sockets[step["socket"]]
            entry = {"step": step}
            if "send" in step:
                ws.send_text(json.dumps(resolve(step["send"])))
            elif "send_text" in step:
                ws.send_text(step["send_text"])
            elif "receive" in step:
                entry["messages"] = [message(ws.receive(10)) for _ in range(step["receive"])]
            elif "quiet" in step:
                received = []
                while (got := ws.receive(step["quiet"])) is not None:
                    received.append(message(got))
                    if isinstance(got, tuple):
                        break
                entry["messages"] = received
            elif step.get("closed"):
                got = ws.receive(10)
                while isinstance(got, str):
                    got = ws.receive(10)
                entry["closed"] = message(got)
            elif step.get("close"):
                ws.close()
            steps.append(entry)
            continue
        if "events" in step:
            stream = EventStreamClient(server.port, resolve(step["events"]), step.get("headers", {}))
            streams[step["stream"]] = stream
            steps.append({"step": step, "response": record(stream.status, stream.headers, b"", normaliser)})
            continue
        if "stream" in step:
            stream = streams[step["stream"]]
            entry = {"step": step}
            if "receive" in step:
                entry["events"] = [message(stream.receive(10)) for _ in range(step["receive"])]
            elif step.get("ended"):
                got = stream.receive(10)
                while isinstance(got, dict):
                    got = stream.receive(10)
                entry["ended"] = message(got)
            steps.append(entry)
            continue
        if "file" in step:
            steps.append({"step": step, "file": server.file(step["file"])})
            continue
        if "write_file" in step:
            server.write(step["write_file"], step["text"])
            steps.append({"step": step})
            continue
        if step.get("restart"):
            server.restart()
            steps.append({"step": step})
            continue
        if step.get("warnings"):
            steps.append({"step": step, "warnings": server.warnings()})
            continue
        headers = dict(step.get("headers", {}))
        body = None
        if "json" in step:
            body = json.dumps(resolve(step["json"]))
            headers.setdefault("Content-Type", "application/json")
        elif "body" in step:
            body = step["body"]
        elif "body_repeat" in step:
            text, count = step["body_repeat"]
            body = text * count
        status, answer_headers, raw = server.request(step["method"], resolve(step["path"]), body, headers)
        deadline = time.monotonic() + 15

        def polling(raw):
            status = json.loads(raw).get("status")
            return status not in step["until"] if "until" in step else status in ("pending", "running")

        while step.get("poll") and polling(raw):
            if time.monotonic() > deadline:
                raise TimeoutError(step["name"])
            time.sleep(0.02)
            status, answer_headers, raw = server.request(step["method"], resolve(step["path"]), body, headers)
        created = None
        if status == 201 and "json" in answer_headers.get("content-type", ""):
            parsed = json.loads(raw)
            if isinstance(parsed, dict) and "id" in parsed:
                created = parsed["id"]
            elif isinstance(parsed, dict) and "status" in parsed:
                # An ActionStatus (the `wot` profile) has no `id`: it is in `location`.
                found = UUID.search(answer_headers.get("location", ""))
                created = found.group(0) if found else None
            if created:
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


def run(executable, scenario):
    """Runs one scenario against one server; returns what it recorded."""
    server = Server(executable, scenario)
    try:
        return run_steps(server, scenario, scenario.get("config", {}).get("api_prefix", ""))
    finally:
        server.stop()


class Listening:
    """A server's port, for steps run against the command line's server."""

    def __init__(self, port):
        self.port = port

    def request(self, method, path, body=None, headers=None):
        return request(self.port, method, path, body, headers)


def output_lines(text, replacements):
    """A command line's output, without log lines, masked."""
    lines = []
    skipping = False
    for line in ANSI.sub("", text).replace("\r\n", "\n").split("\n"):
        if LOG_LINE.match(line):
            skipping = True
            continue
        if skipping and line.startswith("  "):
            continue  # a log record's continuation
        skipping = False
        for old, new in replacements:
            line = line.replace(old, new)
        line = re.sub(r"Registered: .*", "Registered: <names>", line)
        # Each says which language a Thing's configuration type is in.
        line = re.sub(r"a (?:Rust|C\+\+) Thing's configuration", "a <language> Thing's configuration", line)
        line = re.sub(r"<folder>\S*", lambda m: m.group(0).replace("\\", "/"), line)
        # The operating system's words for an error.
        line = re.sub(r"^(Error: Could not find configuration file .*?) \(.*\(os error \d+\)\)$", r"\1 (<os error>)",
                      line)
        line = re.sub(r"(can't (?:listen on|resolve) \S*): .*$", r"\1: <os error>", line)
        lines.append(line)
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def run_cli(executable, scenario):
    """Runs one command-line scenario; returns what it recorded."""
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory() as folder:
        folder_path = Path(folder)
        port = free_port()
        config_path = folder_path / "config.json"
        if "config" in scenario:
            config = dict(scenario["config"])
            config.setdefault("settings_folder", str(folder_path / "settings"))
            config_path.write_text(json.dumps(config), encoding="utf-8")
        settings = json.dumps(str(folder_path / "settings"))[1:-1]
        args = [a.replace("{config}", str(config_path)).replace("{port}", str(port)).replace("{settings}", settings)
                for a in scenario["args"]]
        occupied = None
        if scenario.get("occupy"):
            occupied = socket.socket()
            occupied.bind(("127.0.0.1", port))
            occupied.listen()
        out_path, err_path = folder_path / "stdout.txt", folder_path / "stderr.txt"
        with open(out_path, "wb") as out, open(err_path, "wb") as err:
            process = subprocess.Popen([executable, *args], stdout=out, stderr=err, creationflags=new_group())
            steps = None
            try:
                if "steps" in scenario:
                    wait_listening(process, port, executable, lambda: err_path.read_text(errors="replace"))
                    steps = run_steps(Listening(port), scenario, "")
                    interrupt(process)
                else:
                    process.wait(timeout=30)
            finally:
                interrupt(process)
                if occupied:
                    occupied.close()
        replacements = [
            (json.dumps(str(folder_path))[1:-1], "<folder>"),
            (str(folder_path), "<folder>"),
            (f":{port}", ":<port>"),
            (Path(executable).name, "<server>"),
        ]
        recorded = {
            "exit": process.returncode,
            "stdout": output_lines(out_path.read_bytes().decode("utf-8", errors="replace"), replacements),
            "stderr": output_lines(err_path.read_bytes().decode("utf-8", errors="replace"), replacements),
        }
        if steps is not None:
            text = json.dumps(steps)
            for old, new in replacements:
                text = text.replace(json.dumps(old)[1:-1], json.dumps(new)[1:-1])
            recorded["steps"] = portable_paths(json.loads(text))
        return recorded


def portable_paths(value):
    """Paths under the temporary folder written with `/`, also where they are
    escaped for JSON (in the fallback page's configuration)."""
    if isinstance(value, str):
        return re.sub(r"<folder>[^\s&\"'<>]*", lambda m: m.group(0).replace("\\\\", "/").replace("\\", "/"), value)
    if isinstance(value, list):
        return [portable_paths(v) for v in value]
    if isinstance(value, dict):
        return {k: portable_paths(v) for k, v in value.items()}
    return value


def load(folder, names):
    paths = [folder / f"{n}.json" for n in names] if names else sorted(folder.glob("*.json"))
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
    parser.add_argument("--server", action="append", required=True,
                        help="a server executable (with --examples, a pattern with {name})")
    kinds = parser.add_mutually_exclusive_group()
    kinds.add_argument("--examples", action="store_true", help="compare examples, not the twin servers")
    kinds.add_argument("--cli", action="store_true", help="compare the command line: exit codes, output, fallback")
    parser.add_argument("scenarios", nargs="*")
    args = parser.parse_args()
    if args.mode == "diff" and len(args.server) != 2:
        parser.error("diff needs two --server")
    if args.mode != "diff" and len(args.server) != 1:
        parser.error(f"{args.mode} needs one --server")

    kind = "examples" if args.examples else "cli" if args.cli else "http"
    scenarios, fixtures = HERE / "scenarios" / kind, HERE / "fixtures" / kind
    runner = run_cli if args.cli else run
    failed = []
    for name, scenario in load(scenarios, args.scenarios):
        servers = [server.format(name=name) if args.examples else server for server in args.server]
        if args.mode == "capture":
            if args.cli:
                setup = {k: v for k, v in scenario.items() if k != "steps"}
                fixture = {**setup, **runner(servers[0], scenario)}
            else:
                setup = {key: scenario[key] for key in ("config", "serve", "port", "files") if key in scenario}
                fixture = {**setup, "steps": runner(servers[0], scenario)}
            fixtures.mkdir(parents=True, exist_ok=True)
            path = fixtures / f"{name}.json"
            path.write_text(dumps(fixture), encoding="utf-8", newline="\n")
            print(f"captured {name}: {path.relative_to(HERE.parent)}")
        elif args.mode == "check":
            fixture = json.loads((fixtures / f"{name}.json").read_text(encoding="utf-8"))
            expected = {k: fixture[k] for k in ("exit", "stdout", "stderr", "steps") if k in fixture} if args.cli \
                else fixture["steps"]
            ok = compare(f"fixture {name}", expected, f"server {name}", runner(servers[0], scenario))
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failed.append(name)
        else:
            a = runner(servers[0], scenario)
            b = runner(servers[1], scenario)
            ok = compare(servers[0], a, servers[1], b)
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failed.append(name)
    if failed:
        print(f"{len(failed)} scenario(s) differ: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
