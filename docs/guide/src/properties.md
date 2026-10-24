# Properties

A property is a value clients read, and perhaps write: a position, an exposure time, a temperature. There are two kinds.

## Data properties

A data property is a `Prop<T>` member: a value the Thing keeps.

```cpp
tw::Prop<double> exposure{20.0};
// in define:
t.property("exposure", &Camera::exposure).doc("The exposure time, in milliseconds.").gt(0).le(1000).unit("ms");
```

- **Clients** read it with `GET /camera/exposure` and write it with `PUT`. The written value is validated first.
- **The Thing** reads it with `exposure.get()` (a copy) or `exposure.read([](const double& v) { … })`, and changes it with `set(value)` or `update([](double& v) { … })`. Its own writes are checked against the constraints too (a `tw::PropertyError` if they fail), and publish the change to observers.
- **Other code** watches it with `subscribe()`, a `Watcher<T>` that waits for changes.

`Prop<T>` is safe to use from any thread. The value it was constructed with is its default, shown in the TD and restored by `reset()`.

Options:

| Option | Effect |
|---|---|
| `.read_only()` | Clients can't write it (`PUT` is 405); the Thing can |
| `.ge`, `.gt`, `.le`, `.lt`, `.multiple_of` | Numeric constraints: `minimum`, `exclusiveMinimum`, … in the TD |
| `.min_length`, `.max_length`, `.pattern` | String (or list) constraints |
| `.allow_inf_nan(false)` | Refuse infinities and NaN |
| `.unit("mm")`, `.semantic_type("…")` | The TD's `unit` and `@type` |
| `.global_lock(false)` | Writes don't take the global lock |
| `.setting()` | Saved to the settings file ([Settings](settings.md)) |
| `.doc`, `.title`, `.description` | The title (the first paragraph) and description |

Any type with a codec can be a property: numbers, strings, `bool`, `std::optional<T>`, described enums, aggregate structs, `std::vector`, maps, [arrays](arrays.md). Rust's `usize` has no C++ twin; unsigned C++ integers give the same DataSchema (`minimum: 0`).

## Functional properties

A functional property is a `const` member function, computed when it is read:

```cpp
std::string status() const {
    return is_on.get() ? std::format("The light is on at {}% brightness.", brightness.get())
                       : "The light is off.";
}
// in define:
t.property("status", &Light::status).doc("A human-readable status of the light.");
```

It is read-only unless it has a setter, and can have a resetter and a default:

```cpp
t.property("setpoint", &Controller::setpoint)
    .setter(&Controller::set_setpoint)
    .resetter(&Controller::reset_setpoint)
    .default_value(20.0)
    .ge(5)
    .le(30);
```

A getter or setter runs on the request's worker thread, so it may talk to hardware synchronously. A getter that throws answers 500; a setter's `tw::PropertyError` answers as a refused write. See the [`functional-properties`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/functional-properties) example.

## Resetting

`POST /{thing}/{property}/reset` resets a writable property that has a default, or a resetter, to its default. Read-only properties can't be reset.

## Observation

Data properties are observable: their changes are published to observers.

- **Over the WebSocket** (`/{thing}/ws`, `addPropertyObservation`).
- **As server-sent events:** `GET /camera/exposure` with `Accept: text/event-stream`.
- **All of a Thing's properties at once:** `GET /camera/properties` with the same header.

Their TD says `"observable": true`, with a form for each. Functional properties aren't observable, since nothing publishes their changes. See [Using Things](using_things.md).

## Settings

A data property declared with `.setting()` is saved to the Thing's settings file after every change, and loaded when the server starts: see [Settings](settings.md).
