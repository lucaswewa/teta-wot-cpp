# Custom routes and Drogon

## Routes of your own

An *endpoint* is a route of a Thing outside its Thing Description, such as a file download:

```cpp
tw::http::Response readings_csv() const {
    return tw::http::Response::bytes("seconds,celsius\n0,20.5\n", "text/csv");
}
// in define:
t.endpoint("GET", "readings.csv", &Logger::readings_csv).name("readings_csv").doc("The readings as a CSV file.");
```

It is served at `/{thing}/{path}` (`/logger/readings.csv`), takes the `tw::http::Request` (or nothing) and returns a `tw::http::Response`, and runs on a worker thread. It appears in the OpenAPI document, named with `.name` (teta-wot names it after its handler function), and `.link(rel, media_type)` links it from the TD. Credentials, when the server requires them, protect it too. See the [`custom-endpoint`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/custom-endpoint) example.

## With your own Drogon

The HTTP binding (`teta::wot-http`) doesn't depend on any web framework; `teta::wot-drogon` serves it with Drogon, and `ThingServer` uses that. An application that already serves its own routes with Drogon can serve Things beside them: `tw::drogon_adapter::mount(binding, drogon::app())` (`<teta/wot/drogon.hpp>`) makes the Things' binding Drogon's default handler, and registers their WebSockets. The application keeps its listeners and its shutdown, and starts and stops the runtime itself. The [`drogon-integration`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/drogon-integration) example shows how.

Drogon serves one application per process, so a process serves one `ThingServer` (or one mounted binding) in its lifetime; tests use `TestClient`, which needs no Drogon at all.
