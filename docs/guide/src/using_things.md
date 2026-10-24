# Using Things

## From anything that speaks HTTP

| Operation | Request |
|---|---|
| Read a property | `GET /stage/position` |
| Write a property | `PUT /camera/exposure` with a JSON body (`30.0`): `201` and `null` |
| Reset a property | `POST /camera/exposure/reset` |
| Invoke an action | `POST /stage/move_to` with its input (`{"x": 400, "y": 200, "z": 250}`): `201` and the invocation |
| Follow an invocation | `GET` its `href` (also in `location`) |
| Cancel it | `DELETE` its `href` |
| An invocation's output | `GET {href}/output` |
| Read every property, write several | `GET`, `PUT /stage/properties` |
| The Thing Description | `GET /stage/` |
| Every Thing, every TD | `GET /things/`, `GET /thing_descriptions/` |

Every URL comes from the Thing Description's forms, so a client that reads the TD needs none of this table. The OpenAPI document (`/openapi.json`) describes the same routes for client generators, and `ThingServerBuilder::api_prefix("/api/v1")` moves them all under a prefix. A path with a trailing slash too many or too few is redirected (307), as Starlette does.

## Observing

Clients needn't poll: a Thing's data properties, its actions' statuses and its events are pushed to them as they change. Nothing has to be declared for it: every `Prop<T>` is observable, every action's invocations publish their status, and every `Event<T>` reaches its subscribers.

- **Server-sent events,** which browsers read with `EventSource`:

  ```javascript
  const source = new EventSource("/stage/position");   // Accept: text/event-stream
  source.onmessage = (event) => console.log(JSON.parse(event.data));
  ```

  A property's URL streams its new values when the request accepts `text/event-stream`; an event's URL (`/stage/arrived`) always streams. `/stage/properties` and `/stage/events` stream all of them, each named after its affordance. The TD has the forms, with `"subprotocol": "sse"`.
- **The WebSocket,** `ws://127.0.0.1:5000/stage/ws`, speaks the dialect of LabThings and teta-wot:

  ```text
  → {"messageType": "addPropertyObservation", "data": {"position": true}}
  → {"messageType": "addActionObservation", "data": {"move_to": true}}
  → {"messageType": "addEventSubscription", "data": {"arrived": true}}
  ← {"messageType": "propertyStatus", "data": {"position": {"x": 0, "y": 0, "z": 0}}}
  ← {"messageType": "actionStatus", "data": {"action name": "move_to", "status": "running"}}
  ← {"messageType": "event", "data": {"arrived": {"data": …, "timestamp": "…Z"}}}
  ```

  The server acknowledges nothing, but answers a name that can't be observed: 404 if there is none, 403 for a functional property, which is computed when read. The [`observe-websocket`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/observe-websocket) example has a browser page and a Python script; [`sse-observe`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/sse-observe) uses `EventSource`.

Each subscriber has a buffer of five messages: a client that doesn't keep up misses what doesn't fit, rather than making the server keep everything for it.

## From C++, in-process

Other Things, and tests, use Things directly, without HTTP:

```cpp
const auto stage = *runtime->thing_ref<Stage>("stage");
stage.call(&Stage::move_to, 0, 0, 250);          // an action, validated and typed
const Position here = stage.thing().now();       // the Thing itself
const auto json = stage.read_property("speed");  // as a client would
```

Things get each other through [slots](thing_slots.md); `tw::Server` (an action's parameter, or `ThingCtx::server()`) finds Things by name or type (`thing_ref<T>`, `things_of_type<T>`).

## From LabThings' Python client

`labthings_fastapi.ThingClient` reads a Thing's TD and makes its properties and actions Python attributes:

```python
from labthings_fastapi import ThingClient
stage = ThingClient.from_url("http://127.0.0.1:5000/stage/")
stage.move_to(x=400, y=200, z=0)
```

The simulated microscope's `demo.py` drives a teta-wot-cpp server this way, unchanged from teta-wot's.

## From other Web of Things software

Consumers that follow the W3C specifications read the Thing Description and use its forms. For them, serve the [`wot` wire profile](wire_profiles.md), which follows the W3C WoT Profile. [Eclipse Thingweb node-wot](https://github.com/eclipse-thingweb/node-wot) can:

- read, write, invoke (synchronous actions) and observe;
- observe in the `tetathing` profile only, because it listens for unnamed server-sent events.

The [`node-wot-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/node-wot-consumer) example shows what works in each profile.
