# sse-observe

**Concept:** observing a property in the browser with `EventSource`. A thermometer's reading is pushed as server-sent events, and the page finds the stream in the Thing Description, as any TD consumer would.

**Technologies:** the SSE form of an observable property (`"subprotocol": "sse"`); `GET` with `Accept: text/event-stream`; the browser's `EventSource`; a reading thread started by `on_start` and stopped by `on_stop`; `tw::testing::TestClient::events` for the in-process demo. The C++ port of teta-wot's `sse-observe`.

## Run it

```
cmake --build --preset linux-gcc --target example-sse-observe
./build/linux-gcc/examples/sse-observe/example-sse-observe            # an in-process demo, then exit
./build/linux-gcc/examples/sse-observe/example-sse-observe --serve    # serve on http://127.0.0.1:5000
```

While it serves, open <http://127.0.0.1:5000/thermometer/index.html>. Or watch the raw stream (on Windows, write `curl.exe`):

```
curl -N -H "Accept: text/event-stream" http://127.0.0.1:5000/thermometer/temperature
```

## What to look for

- Each `temperature.set(...)` reaches every stream: one `data:` line per reading.
- Without `Accept: text/event-stream`, the same URL answers the current value.
- `on_stop` stops the reading thread before the Thing goes; streams end when the server shuts down.
