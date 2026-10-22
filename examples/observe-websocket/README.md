# observe-websocket

**Concept:** observing a Thing over its WebSocket, `/{thing}/ws`: a property's new values and an action's status changes are pushed to the client as they happen, instead of polled.

**Technologies:** the WebSocket route of the HTTP binding (`addPropertyObservation`, `addActionObservation`, their messages and refusals); a static page served by a custom endpoint, using the browser's `WebSocket`; `tw::testing::TestClient::websocket` for the in-process demo. The C++ port of teta-wot's `observe-websocket`.

## Run it

```
cmake --build --preset linux-gcc --target example-observe-websocket
./build/linux-gcc/examples/observe-websocket/example-observe-websocket                 # an in-process demo, then exit
./build/linux-gcc/examples/observe-websocket/example-observe-websocket --serve         # serve on http://127.0.0.1:5000
```

While it serves, open <http://127.0.0.1:5000/oven/monitor.html> and press **Start**. The demonstration prints the same lines as the Rust example, and exits with a non-zero status if the oven doesn't heat as expected.

## What to look for

- The dialect acknowledges nothing, but answers a bad request; a connection's messages are handled in order, so an answer also means the subscriptions before it are in place.
- `safe_to_open` is a functional property, computed when read: it can't be observed (403).
- An action's statuses (`pending`, `running`, `completed`) arrive between the property changes it makes, in order.
- The TD has the WebSocket's form (`ws://…/oven/ws`) beside the SSE one.
- `conformance/scenarios/examples/observe-websocket.json` compares the served example with teta-wot's, message by message.
