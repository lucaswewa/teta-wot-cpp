# counter-server

**Concept:** the first end-to-end milestone: a Thing served over HTTP.

**Technologies:** `ThingServer` (Drogon underneath, through the transport-neutral HTTP binding), `ThingServer::serve` with a port of 0 for any free port, and `tw::testing::TestClient` for the in-process demo. The C++ port of teta-wot's `counter-server`.

## Run it

```
cmake --build --preset linux-gcc --target example-counter-server
./build/linux-gcc/examples/counter-server/example-counter-server                       # a short in-process demo, then exit
./build/linux-gcc/examples/counter-server/example-counter-server --serve               # serve on http://127.0.0.1:5000 until Ctrl-C
./build/linux-gcc/examples/counter-server/example-counter-server --serve --port 0      # any free port
```

Served, it answers exactly as the Rust example does: try `curl -i http://127.0.0.1:5000/counter/` against both.

## What to look for

- The demo's requests are the ones a client makes over HTTP; `TestClient` sends them to the same binding Drogon does, without a socket.
- `POST` answers `201 Created` at once, with the invocation `pending`; polling its `href` shows it finish.
- Ctrl-C stops the server gracefully: requests in progress finish, running invocations are cancelled, then the Things stop.
