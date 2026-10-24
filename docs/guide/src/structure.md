# Structure

- **Things.** Each is an instance of a C++ class with a static `define`, served under a name: `/{name}/` is its Thing Description, and its affordances are below it. There is one instance of each Thing, created once, started once and stopped once.
- **Affordances:**
  - [properties](properties.md), values clients read and write;
  - [actions](actions.md), operations clients invoke;
  - [events](events.md), notifications clients subscribe to.

  They are declared in `define`, and described in the Thing Description from their types and `doc` strings.
- **The runtime** holds the Things, and everything they share:
  - the message broker that carries changes and events to observers;
  - the invocations of actions, and the worker pool they run on;
  - the global lock;
  - the settings files;
  - services.
- **The server** serves the runtime over HTTP, WebSockets and server-sent events, and runs its lifecycle (start, serve, shut down gracefully).

## A Thing's type

```cpp
class Stage {
  public:
    tw::Prop<Position> position;
    tw::Prop<double> speed{2000.0};
    tw::Event<Position> arrived;

    Position move_to(std::int64_t x, std::int64_t y, std::int64_t z);

    static void define(tw::ThingSpec<Stage>& t) {
        t.doc("A motorised stage, simulated.\n\nIt moves the slide under the objective in x and y, and the focus in z.");
        t.property("position", &Stage::position).read_only().doc("Where the stage is, in steps.");
        t.property("speed", &Stage::speed).setting().gt(0).le(100000).doc("How fast the stage moves, in steps per second.");
        t.event("arrived", &Stage::arrived).doc("The stage arrived where it was sent.");
        t.action<"x", "y", "z">("move_to", &Stage::move_to).global_lock(false).doc("Move to a position.");
    }
};
```

`define` declares, with member pointers:

| Declaration | Member | What it is |
|---|---|---|
| `t.property(name, &T::member)` | `Prop<V>` | A data property (`.setting()` saves it) |
| `t.property(name, &T::getter)` | a `const` member function | A functional property (`.setter(…)`, `.resetter(…)`) |
| `t.action(name, &T::function)` | a member function | An action |
| `t.event(name, &T::member)` | `Event<V>` | An event |
| `t.stream(name, &T::member)` | `MjpegStream` | An MJPEG stream |
| `t.slot(name, &T::member)` | `Slot<X>`, `OptSlot<X>`, `SlotMap<X>` | Other Things it uses |
| `t.device(name, &T::member)` | `Device<D>` | A driver on a thread of its own |
| `t.endpoint(method, path, &T::handler)` | a member function | A route outside the TD ([Custom routes](endpoints.md)) |
| `t.on_start(&T::f)`, `t.on_stop(&T::f)` | member functions | Lifecycle hooks, taking a `tw::ThingCtx&` or nothing |

Other members are private state. The class is default-constructed, or, with `using Config = …;` and a constructor taking it, built from its configuration's `kwargs` ([Configuration](configuration.md)). The Thing's title is its type's name (`t.title` changes it), and `t.semantic_type`, `t.context` and `t.class_name` annotate it.

`define` is the only declaration: there are no macros, and the compiler checks every member pointer, parameter and return type. A type that can't become JSON, a parameter count that doesn't match the names given, or a member of the wrong class is a compile error that names the problem (the [`compile-errors`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/compile-errors) example shows them). The [`builder_thing`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/builder_thing) example drives a Thing through the runtime's registry, as the HTTP binding does.

## Lifecycle

1. **Create.** The server builds every Thing from its configuration (`kwargs`), fills its slots, and connects its devices.
2. **Start.** Things start in order: a Thing starts after the Things in its slots. Each Thing's settings are loaded, its devices open, then its `on_start` hook runs. If one throws, the Things already started are stopped, and the server reports which Thing failed (or serves its fallback page).
3. **Serve,** until the stop signal.
4. **Shut down:**
   1. new connections are refused and requests in progress finish;
   2. unfinished invocations are cancelled;
   3. the server waits up to the grace period (5 s by default, `shutdown_grace`) for both;
   4. the Things stop in reverse order (`on_stop`), and their devices close.

## Libraries

Applications link `teta::wot`. Behind it:

| Library | Contents |
|---|---|
| `teta::wot-json` | serde_json's parser and writer |
| `teta::wot-td` | The Thing Description model, DataSchemas from C++ types |
| `teta::wot-td-validation` | Validating TDs against the W3C JSON Schema |
| `teta::wot-core` | The runtime: Things, properties, actions, invocations, events, slots, settings, devices, Blobs, streams, arrays, validation |
| `teta::wot-http` | The HTTP binding: routes, answers, observation, OpenAPI, discovery, security; independent of any web framework |
| `teta::wot-drogon` | The binding served by Drogon |
| `teta::wot-server` | The server: configuration, registry, command line, lifecycle, Windows service, systemd, test client |
