# Testing

`tw::testing` (in `teta::wot`, `<teta/wot/testing.hpp>`) tests Things without a network.

## An in-process client

`TestClient` sends HTTP requests straight to a server's HTTP binding, in the same process: no port, no sockets, the exact answers a client would get.

```cpp
TEST(Light, TogglingTurnsItOn) {
    auto server = tw::ThingServer::builder().thing<Light>("light").build();
    auto client = tw::testing::TestClient::start(server);

    client.post_json("/light/toggle");
    // … wait for the invocation (see below), then:
    EXPECT_EQ(client.get("/light/is_on").json(), true);

    const auto refused = client.put_json("/light/brightness", 150);
    EXPECT_EQ(refused.status, 422);
    client.stop();
}
```

- `get`, `put_json`, `post_json` and `del` send requests; `request` sends anything, with headers and a body.
- `events(path)` opens a server-sent events stream; `next()` reads its data, and `next_message()` its name and ID too.
- `websocket(path)` opens a WebSocket, served in-process: `send_json`, `receive_json`.
- `stream(path)` reads a long answer, such as MJPEG, chunk by chunk.
- `with_host` and `with_header` set the `Host` (`testserver` by default, so absolute URLs are `http://testserver/…`) and any header (such as credentials) for every request.
- `runtime()` reaches the Things themselves, for in-process calls.

## A harness for one Thing

`Harness` starts one Thing, with stand-ins for its slots and a temporary settings folder (LabThings' `create_thing_without_server`):

```cpp
auto harness = tw::testing::Harness<Autofocus>::builder("autofocus")
                   .thing<Stage>("stage")
                   .thing("camera", std::make_shared<FakeCamera>())
                   .start();
const auto focus = harness.thing().call(&Autofocus::run, 1000, 21);  // in-process, typed
const auto td = harness.client().get("/autofocus/");                // or over HTTP
harness.stop();
```

## Actions without a server

`tw::ActionCtx::detached()` makes an invocation context for calling action code directly, and `tw::Server::detached()` a server with nothing on it.

## Waiting for invocations

A `POST` answers before the action finishes. Poll the invocation's `href`, or call the action in-process, where the call returns its output:

```cpp
const auto light = *client.runtime()->thing_ref<Light>("light");
light.call(&Light::toggle);
```

## Beyond unit tests

The repository tests itself in more ways, which applications can borrow: the conformance harness (`conformance/run.py`) replays scripted HTTP, WebSocket and SSE sessions against any server and compares the answers; the fuzz targets (`fuzz/`) throw arbitrary inputs at the binding; the soak tool (`tools/soak`) runs a server under load for hours and watches its memory.
