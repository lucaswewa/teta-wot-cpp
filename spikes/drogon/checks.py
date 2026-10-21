"""Phase 0 checks of the Drogon spike: streaming, WebSocket, worker-thread
completion and graceful drain. Usage: python3 checks.py PORT PID

Each check prints `ok` or `FAIL` with what it saw. The drain check sends
SIGINT to PID, so it runs last.
"""

import base64
import os
import signal
import socket
import sys
import threading
import time

PORT = int(sys.argv[1])
PID = int(sys.argv[2])


def request(raw, timeout=5):
    with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as conn:
        conn.sendall(raw)
        data = b""
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                return data
            data += chunk


def report(name, passed, detail):
    print(f"{'ok  ' if passed else 'FAIL'} {name}: {detail}")


def check_sse():
    started = time.monotonic()
    answer = request(b"GET /sse HTTP/1.1\r\nHost: t\r\nAccept: text/event-stream\r\nConnection: close\r\n\r\n")
    elapsed = time.monotonic() - started
    head, _, body = answer.partition(b"\r\n\r\n")
    events = body.count(b"data: ")
    passed = b"text/event-stream" in head and b"chunked" in head and events == 3 and elapsed > 0.25
    report("sse", passed, f"{events} events over {elapsed:.2f} s, chunked={b'chunked' in head}")


def check_mjpeg():
    answer = request(b"GET /mjpeg HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n")
    head, _, body = answer.partition(b"\r\n\r\n")
    frames = body.count(b"Content-Type: image/jpeg")
    report("mjpeg", b"multipart/x-mixed-replace" in head and frames == 3, f"{frames} frames")


def check_worker_completion():
    started = time.monotonic()
    answer = request(b"GET /slow?ms=200 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n")
    elapsed = time.monotonic() - started
    report("worker completion", answer.startswith(b"HTTP/1.1 200") and elapsed >= 0.2, f"{elapsed:.2f} s")


def check_loops_not_blocked():
    # Many slow requests at once, on 2 I/O threads: they overlap.
    results = []

    def one():
        results.append(request(b"GET /slow?ms=300 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"))

    threads = [threading.Thread(target=one) for _ in range(20)]
    started = time.monotonic()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.monotonic() - started
    passed = all(r.startswith(b"HTTP/1.1 200") for r in results) and elapsed < 1.0
    report("20 concurrent slow requests", passed, f"{elapsed:.2f} s")


def check_body_limit():
    body = b"x" * (3 * 1024 * 1024)
    answer = request(b"POST /body HTTP/1.1\r\nHost: t\r\nConnection: close\r\nContent-Length: %d\r\n\r\n" % len(body) + body)
    report("body over 2 MiB", answer.startswith(b"HTTP/1.1 413"), answer.split(b"\r\n")[0].decode())


def ws_frame(text):
    payload = text.encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes([0x81, 0x80 | len(payload)]) + mask + masked


def read_ws_text(conn):
    header = conn.recv(2)
    length = header[1] & 0x7F
    data = b""
    while len(data) < length:
        data += conn.recv(length - len(data))
    return data.decode()


def check_websocket():
    key = base64.b64encode(os.urandom(16)).decode()
    with socket.create_connection(("127.0.0.1", PORT), timeout=3) as conn:
        conn.sendall(
            (f"GET /counter/ws HTTP/1.1\r\nHost: t\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            head += conn.recv(1)
        hello = read_ws_text(conn)
        conn.sendall(ws_frame("ping"))
        echo = read_ws_text(conn)
    passed = head.startswith(b"HTTP/1.1 101") and hello == "hello" and echo == "echo: ping"
    report("websocket on a regex path", passed, f"{head.split(b'\r\n')[0].decode()}, {hello!r}, {echo!r}")


def check_drain():
    results = {}

    def slow():
        results["slow"] = request(b"GET /slow?ms=1500 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n")

    in_flight = threading.Thread(target=slow)
    in_flight.start()
    time.sleep(0.3)
    os.kill(PID, signal.SIGINT)
    time.sleep(0.3)
    try:
        late = request(b"GET /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", timeout=2)
        results["late"] = late.split(b"\r\n")[0].decode() or "<closed without an answer>"
    except OSError as error:
        results["late"] = f"<{type(error).__name__}>"
    in_flight.join()
    finished = results["slow"].startswith(b"HTTP/1.1 200")
    report("drain: in-flight request finishes", finished, results["slow"].split(b"\r\n")[0].decode())
    report("drain: new connection refused", "HTTP" not in results["late"], results["late"])


for check in (check_sse, check_mjpeg, check_worker_completion, check_loops_not_blocked,
              check_body_limit, check_websocket, check_drain):
    try:
        check()
    except Exception as error:  # a crash is a failed check
        report(check.__name__, False, repr(error))
