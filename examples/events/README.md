# events

**Concept:** events. A laser's safety interlock trips on its device thread, and the driver emits an event straight from that synchronous code. Clients receive it over server-sent events and over the WebSocket, and C++ code can subscribe too.

**Technologies:** `tw::Event<T>` and `t.event(...)`; a `tw::Device<D>` driver holding a copy of the event (an `Event` is a cheap handle); the SSE route `GET /{thing}/{event}` and the WebSocket's `addEventSubscription`; `Event::subscribe` for C++; `curl`. The C++ port of teta-wot's `events`.

## Run it

```
cmake --build --preset linux-gcc --target example-events
./build/linux-gcc/examples/events/example-events            # an in-process demo, then exit
./build/linux-gcc/examples/events/example-events --serve    # serve on http://127.0.0.1:5000
```

While it serves, watch the event in one terminal (on Windows, write `curl.exe`):

```
curl -N http://127.0.0.1:5000/laser/tripped
```

and trip the interlock in another:

```
curl -X POST http://127.0.0.1:5000/laser/start_emission
curl -X POST http://127.0.0.1:5000/laser/open_door
```

The first terminal prints `data: {"input":"enclosure door","was_emitting":true}`.

## What to look for

- `emit` never waits: it is called on the device's thread, inside a driver call.
- The WebSocket's `event` message carries the data and a timestamp; the SSE stream carries the data alone.
- The TD describes the event's data with `Trip`'s schema, and has an SSE and a WebSocket form.
