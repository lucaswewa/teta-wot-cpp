# Your first Thing

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

- `Prop<T>` members are *data properties*: clients read them, write them (unless `read_only`), and observe them. Constraints such as `ge` and `le` are checked on every write, and appear in the Thing Description.
- A `const` member function is a *functional property*: its value is computed on each read. Without a setter it is read-only.
- A member function registered with `action` is an *action*: a client's `POST` starts an invocation, which runs on a worker thread.
- `.doc()` gives the title (the first paragraph) and the description, as teta-wot reads doc comments.

## Serving it

```cpp
int main() {
    auto server = tw::ThingServer::builder().thing<Light>("light").build();
    server.serve("127.0.0.1", 5000);  // until Ctrl-C
}
```

`serve` starts the Things, serves until the process is asked to stop (Ctrl-C, or SIGTERM on Linux), then shuts down gracefully: requests in progress finish, running invocations are cancelled, and the Things stop. A port of 0 picks a free one; the overload taking a `std::stop_token` and a callback tells you which, and stops when you say.

The light is now at `http://127.0.0.1:5000/light/`:

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

These answers are teta-wot's, byte for byte: a client written for a teta-wot server works with this one. The routes:

| Route | What |
|---|---|
| `GET /light/` | The Thing Description |
| `GET`, `PUT /light/{property}` | Read, write (`201` and `null`) |
| `POST /light/{property}/reset` | Back to the default |
| `GET /light/properties` | Every property; `PUT` writes several |
| `POST /light/{action}` | Invoke: `201`, with the invocation's URL in `location` |
| `GET /light/{action}`, `GET /action_invocations` | Invocations |
| `GET`, `DELETE /action_invocations/{id}` | An invocation; cancel it |
| `GET /action_invocations/{id}/output` | Its output |
| `GET /light/{property}` with `Accept: text/event-stream` | Server-sent events of a data property |
| `GET /things/`, `GET /thing_descriptions/` | Every Thing |

`ThingServerBuilder::api_prefix("/api/v1")` moves them all under a prefix.

## Testing it

`TestClient` sends requests to the server's HTTP binding in-process, without a socket:

```cpp
auto server = tw::ThingServer::builder().thing<Light>("light").build();
auto client = tw::testing::TestClient::start(server);
assert(client.put_json("/light/brightness", 40).status == 201);
assert(client.get("/light/brightness").json() == 40);
client.stop();
```

Answers carry `Host: testserver`, so absolute URLs are `http://testserver/…`; `with_host` changes it.

## Routes of your own

An *endpoint* is a route of a Thing outside its Thing Description, such as a file download:

```cpp
tw::http::Response readings_csv() const {
    return tw::http::Response::bytes("seconds,celsius\n0,20.5\n", "text/csv");
}
// in define:
t.endpoint("GET", "readings.csv", &Logger::readings_csv).doc("The readings as a CSV file.");
```

It takes the `tw::http::Request` (or nothing) and returns a `tw::http::Response`, and runs on a worker thread.

## With your own Drogon

An application that already serves its own routes with Drogon can serve Things beside them: `tw::drogon_adapter::mount(binding)` (`<teta/wot/drogon.hpp>`, target `teta::wot-drogon`) makes the Things' HTTP binding Drogon's default handler. The application keeps its listeners and its shutdown, and starts and stops the runtime itself. The [`drogon-integration`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/drogon-integration) example shows how.

The [examples](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples) `quickstart-counter`, `light`, `counter-server`, `http-walkthrough`, `api-prefix` and `custom-endpoint` go further.
