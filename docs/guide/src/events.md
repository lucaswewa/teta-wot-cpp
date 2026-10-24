# Events

An event is a notification a Thing sends when something happens: the stage arrived, an image was captured, an interlock tripped, as the Web of Things describes them.

```cpp
tw::Event<Position> arrived;
// in define:
t.event("arrived", &Stage::arrived).doc("The stage arrived where it was sent.");
```

The Thing emits it with `arrived.emit(position)`. That never waits, and works from any thread, including a driver's: an `Event<T>` can be copied and given to a thread, and the copies are the same event (the [`events`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/events) example does).

`T`'s JSON Schema is the TD's `data` for the event; `Event<std::monostate>` carries no data (Rust's `Event<()>`).

## Subscribing

- **Server-sent events:** `GET /stage/arrived` streams each event's data as JSON. `curl -N http://127.0.0.1:5000/stage/arrived` shows them.
- **All of a Thing's events at once:** `GET /stage/events`, each named after its event.
- **The WebSocket:** `{"messageType": "addEventSubscription", "data": {"arrived": {}}}` on `/stage/ws`. Each event arrives as `{"messageType": "event", "data": {"arrived": {"data": …, "timestamp": "…"}}}`.
- **In C++:** `arrived.subscribe()` returns a receiver of the events emitted from then on, in-process.

The TD describes each event, with a `subscribeevent` form for each transport.

## Slow subscribers

Each subscriber has a buffer of five messages. One that doesn't keep up misses what doesn't fit (the server logs a warning for each) rather than making the server keep everything for it, and catches up once it reads again. A C++ receiver that falls behind misses the oldest events.

## Events or observed properties?

- **A property** has a value at any time: observing it gives its changes, and a client that connects late reads the current value.
- **An event** happens: a client that wasn't subscribed misses it. It suits things that are over when they are reported, and that no property holds.
