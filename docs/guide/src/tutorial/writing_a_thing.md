# Writing a Thing

This page writes LabThings' tutorial Thing: a light whose brightness can be set, and which can be turned on and off. It is the [`light`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/light) example.

A Thing is a C++ class with a static `define` function, which declares what clients see: its properties, actions and events. Members hold the state; `define` names them, with member pointers, so every type is checked by the compiler.

```cpp
#include <teta/wot.hpp>
namespace tw = teta::wot;

class Light {
  public:
    tw::Prop<std::int64_t> brightness{100};
    tw::Prop<bool> is_on{false};

    void toggle() { is_on.update([](bool& on) { on = !on; }); }

    std::string status() const {
        return is_on.get() ? std::format("The light is on at {}% brightness.", brightness.get())
                           : "The light is off.";
    }

    static void define(tw::ThingSpec<Light>& t) {
        t.doc("A computer-controlled light, our first example Thing.");
        t.property("brightness", &Light::brightness)
            .doc("The brightness of the light, in % of maximum.")
            .ge(0)
            .le(100);
        t.property("is_on", &Light::is_on).read_only().doc("Whether the light is currently on.");
        t.action("toggle", &Light::toggle).doc("Swap the light between on and off.");
        t.property("status", &Light::status).doc("A human-readable status of the light.");
    }
};
```

Serve it with a builder (or from a [configuration file](running.md)):

```cpp
int main() {
    auto server = tw::ThingServer::builder().thing<Light>("light").build();
    server.serve("127.0.0.1", 5000);  // until Ctrl-C
}
```

A port of 0 picks a free one; the overload taking a `std::stop_token` and a callback says which, and stops when the token is.

Then, while it runs:

- <http://127.0.0.1:5000/light/> is its Thing Description;
- <http://127.0.0.1:5000/docs> lets you try it: `PUT` a brightness, `POST` to `toggle`, `GET` the status.

## Properties

`brightness` and `is_on` are *data properties*: values the Thing keeps, which work like variables.

- **`brightness` is an `std::int64_t`, constrained to 0–100** by `ge` and `le`. A client that writes 150 gets LabThings' 422 answer, with pydantic's message: "Input should be less than or equal to 100". The TD says `"minimum": 0, "maximum": 100`.
- **`is_on` is `read_only`.** Clients can't write it (a `PUT` is 405), but the Thing can: `is_on.update(…)` or `is_on.set(…)`.
- **Every data property has a default,** the value it was constructed with, which the TD shows.

`status` is a *functional property*: a `const` member function whose value is computed when it is read. Functional properties are read-only unless they have a setter. See [Properties](../properties.md).

## Actions

`toggle` is an action. Clients invoke it with a `POST`, and the server answers `201 Created` with the invocation at once. The action runs on a worker thread, and the invocation's URL follows it (`pending`, `running`, `completed`). An action can take parameters and return a value, both typed: see [Actions](../actions.md).

Other Things (and tests) call it in-process:

```cpp
const auto light = *server.runtime()->thing_ref<Light>("light");
light.call(&Light::toggle);
```

## On the wire

```text
$ curl http://127.0.0.1:5000/light/status
"The light is off."
$ curl -X POST http://127.0.0.1:5000/light/toggle
{"status":"pending","id":"…","action":"/light/toggle","href":"http://127.0.0.1:5000/action_invocations/…",…}
$ curl -X PUT -d 150 http://127.0.0.1:5000/light/brightness
{"detail":[{"type":"less_than_equal","loc":["body"],"msg":"Input should be less than or equal to 100","input":150,"ctx":{"le":100}}]}
$ curl -i -X PUT -d true http://127.0.0.1:5000/light/is_on
HTTP/1.1 405 Method Not Allowed
allow: GET
…
```

These answers are teta-wot's, byte for byte: a client written for a teta-wot or LabThings server works with this one. [Using Things](../using_things.md) lists every route.

## Testing it

`TestClient` sends requests to the server's HTTP binding in-process, without a socket:

```cpp
auto server = tw::ThingServer::builder().thing<Light>("light").build();
auto client = tw::testing::TestClient::start(server);
assert(client.put_json("/light/brightness", 40).status == 201);
assert(client.get("/light/brightness").json() == 40);
client.stop();
```

See [Testing](../testing.md).

## Where things go

| Python (LabThings) | Rust (teta-wot) | C++ (teta-wot-cpp) |
|---|---|---|
| `class Light(lt.Thing):` with a docstring | `#[derive(Thing)] pub struct Light` with a doc comment | `class Light` with `static void define(tw::ThingSpec<Light>&)` and `t.doc(…)` |
| `brightness: int = lt.property(default=100, ge=0, le=100)` | `#[property(default = 100, ge = 0, le = 100)] brightness: Prop<i64>` | `tw::Prop<std::int64_t> brightness{100};` and `t.property("brightness", …).ge(0).le(100)` |
| `self.brightness` | `self.brightness.get()` | `brightness.get()` |
| `self.is_on = True` | `self.is_on.set(true)?` | `is_on.set(true)` |
| `@lt.action def toggle(self):` | `#[action] async fn toggle(&self)` | `void toggle()` and `t.action("toggle", &Light::toggle)` |
| `@lt.property def status(self) -> str:` | `#[property] async fn status(&self) -> String` | `std::string status() const` and `t.property("status", &Light::status)` |
