# thing-slots

**Concept:** composing Things. An autofocus uses a stage, a camera and, if there is one, a lamp, without knowing how they are made. The configuration decides which Things fill its slots, so a simulated camera can be swapped for a real one without changing code.

**Technologies:** `tw::Slot<T>`, `tw::OptSlot<T>`, `tw::SlotMap<T>` and slots of an interface (`tw::Slot<CameraApi>`), declared with `t.slot(...)` and `.defaults_to(...)`; `t.provides<I>()`; `thing_slots` in a configuration; `ThingServer::from_config` with a `ThingRegistry`. The C++ port of teta-wot's `thing-slots`, where the interface is a trait declared with `#[wot::interface]`.

## Run it

```
cmake --build --preset linux-gcc --target example-thing-slots
./build/linux-gcc/examples/thing-slots/example-thing-slots
```

It serves two configurations in turn (in-process, with `TestClient`), focuses with each, then shows a configuration that is refused. It prints the same lines as the Rust example, and exits with a non-zero status if a result isn't as expected.

| Slot | Member | Filled by |
|---|---|---|
| `stage` | `Slot<Stage>` | the one `Stage` (found by type) |
| `camera` | `Slot<CameraApi>` | the Thing named `camera` (the slot's default), or as configured |
| `lamp` | `OptSlot<Lamp>` | a `Lamp` if there is one, otherwise nothing |
| `cameras` | `SlotMap<CameraApi>` | every Thing providing `CameraApi`, by name |

## What to look for

- **Swapping by configuration.** The first configuration has a `SimCamera` named `camera`. The second has no Thing of that name; its `"thing_slots": {"camera": "real"}` connects the `RealCamera` instead. The same code focuses at 3, then at -2.
- **Interfaces.** `CameraApi` is an abstract class. `SimCamera` provides it through an adapter that calls its own action through a `ThingRef` (`t.provides<CameraApi>(make)`), so calls through the interface are validated and locked like HTTP requests; `RealCamera` implements it itself (`t.provides<CameraApi>()`), so calls are plain C++ calls.
- **What a slot holds.** A slot of a Thing type holds a `ThingRef<T>` (`stage->call(&Stage::move_to, z)`); a slot of an interface holds a `std::shared_ptr<I>` (`camera->sharpness(z)`).
- **Start order.** The autofocus is listed first, but starts last: Things start after the Things their slots use. With a cycle, configuration order is kept, with a warning.
- **Refusals.** A slot that can't be connected stops the server from being built, with teta-wot's message.
