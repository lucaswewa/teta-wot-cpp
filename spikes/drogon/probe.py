"""Send raw HTTP requests to a server and print what comes back.

Usage: python3 probe.py HOST:PORT [CASE ...]

Phase 0 runs it against the Drogon spike and the Rust reference server, to
see where Drogon answers before (or differently from) our handler.
"""

import socket
import sys

CASES = {
    "get": b"GET /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "head": b"HEAD /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "options": b"OPTIONS /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "preflight": b"OPTIONS /x HTTP/1.1\r\nHost: t\r\nOrigin: http://a\r\nAccess-Control-Request-Method: PUT\r\nConnection: close\r\n\r\n",
    "patch": b"PATCH /x HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
    "unknown-method": b"PROPFIND /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "http10": b"GET /x HTTP/1.0\r\n\r\n",
    "no-host": b"GET /x HTTP/1.1\r\nConnection: close\r\n\r\n",
    "encoded-slash": b"GET /a%2Fb HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "dot-segments": b"GET /a/../b HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "double-slash": b"GET //x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "query": b"GET /x?a=1&b=%20 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "absolute-uri": b"GET http://t/x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
    "expect": b"POST /x HTTP/1.1\r\nHost: t\r\nContent-Length: 2\r\nExpect: 100-continue\r\nConnection: close\r\n\r\n{}",
    "chunked-body": b"POST /x HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n2\r\n{}\r\n0\r\n\r\n",
    "bad-content-length": b"POST /x HTTP/1.1\r\nHost: t\r\nContent-Length: abc\r\nConnection: close\r\n\r\n",
    "garbage": b"HELLO\r\n\r\n",
    "ws-plain-get": b"GET /counter/ws HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
}


def send(address, raw):
    host, port = address.split(":")
    with socket.create_connection((host, int(port)), timeout=3) as conn:
        conn.sendall(raw)
        chunks = []
        try:
            while True:
                chunk = conn.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
        except (socket.timeout, ConnectionResetError) as error:
            chunks.append(f"<{type(error).__name__}>".encode())
    return b"".join(chunks)


def main():
    address = sys.argv[1]
    names = sys.argv[2:] or list(CASES)
    for name in names:
        answer = send(address, CASES[name])
        head, _, body = answer.partition(b"\r\n\r\n")
        print(f"== {name}")
        print(head.decode("latin-1") or "<no answer>")
        if body:
            print(f"   body: {body[:120]!r}")


if __name__ == "__main__":
    main()
