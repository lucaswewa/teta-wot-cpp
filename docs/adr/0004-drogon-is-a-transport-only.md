# ADR-0004: Drogon is a transport only

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D4

## Context

Wire parity is byte-level: status lines, headers, bodies, 404/405 answers, redirects, CORS. Drogon has its own router, 404 page, OPTIONS and HEAD handling. teta-wot's tests use an in-process client that calls the router without sockets, and teta-wot-cpp wants the same.

## Decision

- **The HTTP binding is transport-neutral**: a dispatcher from a request to a response (whose body may be a stream), with our own route table. A thin adapter connects it to Drogon; the in-process `TestClient` calls the dispatcher directly.
- **The adapter** (Phase 4) uses what the Phase 0 spike proved:
  - `setDefaultHandler` receives every request, for every method (unknown ones too), with no Drogon routes registered; `getOriginalPath()` gives the undecoded path;
  - WebSocket controllers are registered as instances with `registerController`, then by regex (`registerWebSocketControllerRegex`), so `/{thing}/ws` works for any Thing; a plain `GET` there reaches the default handler;
  - streamed bodies (SSE, MJPEG) use `newAsyncStreamResponse`, fed from any thread. Drogon won't start a stream on a connection it is going to close (a request with `Connection: close`, or HTTP/1.0): a pre-sending advice keeps such connections open, adds `connection: close`, and the stream shuts the connection after its last chunk;
  - Thing code runs on worker threads, which complete responses through Drogon's callback;
  - the body size limit is lifted (`setClientMaxBodySize`): teta-wot accepts bodies of any size;
  - the `server` header is off; the `date` header is on, as in teta-wot;
  - shutdown drains: a new-connection advice closes new connections, in-flight requests finish, then the loops quit; Drogon's own signal handling is off.

## Consequences

- Header order and the transport headers (`date`, `content-length`, `connection`, `transfer-encoding`) can't match hyper's byte for byte and aren't contract; the fixtures compare the headers clients rely on (ADR-0011).
- Malformed requests are answered by Drogon's parser before our handler: the same status (400) as hyper, with different headers. Recorded in `docs/wire-contract.md`.
- A connection opened during shutdown is accepted and then closed, where teta-wot refuses it; clients see a failed request either way.
- If a later Drogon fixes streaming on closing connections, the pre-sending advice can go; worth proposing upstream.
