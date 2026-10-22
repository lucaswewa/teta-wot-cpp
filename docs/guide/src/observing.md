# Observing Things

Clients needn't poll: a Thing's data properties, its actions' statuses and its events are pushed to them as they change, over server-sent events or over the Thing's WebSocket. Nothing has to be declared for it: every `Prop<T>` is observable, every action's invocations publish their status, and every `Event<T>` reaches its subscribers.

## Server-sent events

```text
$ curl -N -H "Accept: text/event-stream" http://127.0.0.1:5000/oven/temperature
data: 30.0

data: 40.0
```

A property's URL streams its new values when the request accepts `text/event-stream`; an event's URL (`/oven/overheated`) always streams. `{thing}/properties` and `{thing}/events` stream all of them, each event named after its affordance. The TD has the form, with `"subprotocol": "sse"`, so a browser's `EventSource` can find it.

## The WebSocket

`ws://host/{thing}/ws` speaks the dialect of LabThings and teta-wot:

```text
→ {"messageType": "addPropertyObservation", "data": {"temperature": true}}
→ {"messageType": "addActionObservation", "data": {"heat_to": true}}
→ {"messageType": "addEventSubscription", "data": {"overheated": true}}
← {"messageType": "propertyStatus", "data": {"temperature": 30.0}}
← {"messageType": "actionStatus", "data": {"action name": "heat_to", "status": "running"}}
← {"messageType": "event", "data": {"overheated": {"data": …, "timestamp": "…Z"}}}
```

The server acknowledges nothing, but answers a name that can't be observed: 404 if there is none, 403 for a functional property, which is computed when read.

## Events

An event is an `Event<T>` member, declared with `t.event("overheated", &Oven::overheated)`. `emit` never waits, so it can be called from anywhere, a device's thread included; C++ code receives events with `subscribe()`.

## Slow clients

Each subscriber has a buffer of five messages. A client that doesn't keep up misses what doesn't fit (the server logs a warning for each) rather than making the server keep everything for it, and catches up with the latest values once it reads again.

In tests, `TestClient::events` and `TestClient::websocket` open both kinds of stream in-process; the examples `observe-websocket`, `sse-observe` and `events` show each.
