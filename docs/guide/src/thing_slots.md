# Thing slots

Things often use other Things. An autofocus moves a stage and asks a camera how sharp its image is. teta-wot-cpp connects them with slot members, filled when the server is built.

```cpp
class Autofocus {
  public:
    tw::Slot<Stage> stage;
    tw::Slot<Camera> camera;

    Focus run(tw::ActionCtx& ctx, std::int64_t range, std::int64_t steps) {
        const Position here = stage->thing().now();
        for (std::int64_t i = 0; i < steps; ++i) {
            ctx.check_cancelled();
            stage->call(&Stage::move_to, here.x, here.y, z_at(here, range, steps, i));
            const double sharpness = camera->thing().measure();
            …
        }
    }

    static void define(tw::ThingSpec<Autofocus>& t) {
        t.slot("stage", &Autofocus::stage);
        t.slot("camera", &Autofocus::camera);
        …
    }
};
```

## Kinds of slot

| Member | Filled with |
|---|---|
| `Slot<T>` | Exactly one Thing |
| `OptSlot<T>` | One Thing, or none (`lamp.get()` is null) |
| `SlotMap<T>` | Every matching Thing, by name, in configuration order |

`T` is a Thing type, or an interface. A slot of a Thing type holds a `ThingRef<T>`, through which the Thing:

- calls the other Thing's actions in-process: `stage->call(&Stage::move_to, …)`, validated and under the global lock as a client's request would be, in the calling invocation's context;
- reaches the other Thing itself: `camera->thing().measure()`, a plain C++ call, for its members and plain member functions.

## How slots are filled

- **By default, by type:** the one Thing of type `T` on the server. If there are none, or several, the server can't be built.
- **`t.slot(…).defaults_to("camera")`** names the Thing; `.defaults_to({"a", "b"})` names several (for a `SlotMap`); `.defaults_to_nothing()` leaves an `OptSlot` empty.
- **A configuration file's `thing_slots`** overrides the defaults, per Thing:

  ```json
  {"things": {
      "autofocus": {"class": "microscope.autofocus:Autofocus", "thing_slots": {"camera": "camera2"}},
      "camera": "microscope.camera:Camera",
      "camera2": "microscope.camera:Camera",
      "stage": "microscope.stage:Stage"
  }}
  ```

Slots also set the start order: a Thing starts after the Things in its slots, and stops before them. A slot's type must be complete where the slot is declared, so two Things that use each other connect one way through an interface.

## Interfaces

A slot can name what it needs rather than a type, so that a configuration can swap one implementation for another: a simulated camera for a real one. The interface is an abstract class, and each Thing that provides it says so in `define`:

```cpp
/// What an autofocus needs from a camera.
class CameraApi {
  public:
    virtual ~CameraApi() = default;
    virtual double sharpness(std::int64_t z) = 0;
};

class RealCamera : public CameraApi {
  public:
    double sharpness(std::int64_t z) override { … }
    static void define(tw::ThingSpec<RealCamera>& t) { t.provides<CameraApi>(); }
};

class Autofocus {
  public:
    tw::Slot<CameraApi> camera;
    static void define(tw::ThingSpec<Autofocus>& t) { t.slot("camera", &Autofocus::camera).defaults_to("camera"); }
};
```

`t.provides<CameraApi>()` hands out the Thing itself, so calls through the interface are plain C++ calls. `t.provides<CameraApi>(make)` hands out what `make(ThingRef<T>)` returns: usually an adapter that calls the Thing's actions through the `ThingRef`, so calls through the interface are validated and locked as clients' requests are. A slot of an interface holds a `std::shared_ptr<CameraApi>`: `camera->sharpness(z)`.

The [`thing-slots`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/thing-slots) example serves this with two configuration files, one simulated and one "real".

## Services

What isn't a Thing, such as a shared connection, a data store, or a clock that tests can control, can be registered with the server as a service (`ThingServerBuilder::service(std::make_shared<DataStore>())`), and reached from actions with a `tw::Dep<DataStore>` parameter, or from anywhere with the `tw::Server` (`server.service<DataStore>()`). See the [`dependency-injection`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/dependency-injection) example.
