# Examples

The repository's [`examples/`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples) folder has 38 small programs: C++ ports of teta-wot's 35, and three of this project's own (`cmake-consumer`, `compile-errors`, `drogon-integration`). Each teaches one concept with the public API only, and has a README saying what it shows, how to run it, and what to look for. CI runs every one, and compares each one that serves with its Rust original on the wire.

Build them with the repository (they are on in a top-level build), and run `example-<name>`: most run a short demonstration in-process and exit, and serve with `--serve`.

| To learn | Start with |
|---|---|
| A first Thing | [`quickstart-counter`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/quickstart-counter), [`light`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/light) |
| Thing Descriptions and DataSchemas | [`td_builder`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/td_builder), [`schema-to-dataschema`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/schema-to-dataschema), [`builder_thing`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/builder_thing) |
| Properties | [`property-cells`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/property-cells), [`functional-properties`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/functional-properties) |
| Actions and cancellation | [`cancellable-action`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cancellable-action), [`invocation-logs`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/invocation-logs) |
| Hardware drivers | [`device-actor`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/device-actor), [`global-lock`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/global-lock) |
| Composing Things | [`thing-slots`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/thing-slots), [`dependency-injection`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/dependency-injection), [`settings`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/settings) |
| Configuration files and the command line | [`config-and-cli`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/config-and-cli) |
| Serving | [`counter-server`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/counter-server), [`http-walkthrough`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/http-walkthrough), [`api-prefix`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/api-prefix), [`custom-endpoint`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/custom-endpoint), [`drogon-integration`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/drogon-integration) |
| Observation and events | [`observe-websocket`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/observe-websocket), [`sse-observe`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/sse-observe), [`events`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/events) |
| Binary data | [`blob-camera`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/blob-camera), [`mjpeg-stream`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/mjpeg-stream), [`ndarray-property`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/ndarray-property) |
| API documentation | [`openapi-export`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/openapi-export), [`docs-offline`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/docs-offline) |
| The W3C Web of Things | [`strict-profile`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/strict-profile), [`discovery`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/discovery), [`node-wot-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/node-wot-consumer), [`bearer-security`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/bearer-security), [`units-and-semantics`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/units-and-semantics) |
| Deployment | [`graceful-shutdown`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/graceful-shutdown), [`microscope-service`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/microscope-service), [`cmake-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cmake-consumer) |
| What the compiler says | [`compile-errors`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/compile-errors) |
| Everything together | [`simulated-microscope`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/simulated-microscope) |

The [catalogue](https://github.com/lucaswewa/teta-wot-cpp/blob/main/examples/README.md) lists all of them.

## The simulated microscope

The capstone composes three Things with slots:

- **`Stage`** moves in x, y and z at a speed that is a setting, with cancellable moves and an `arrived` event.
- **`Camera`** renders what it sees through the stage into an MJPEG preview, blurred away from the focal plane. It captures JPEG Blobs, measures sharpness, and saves its exposure as a setting.
- **`Autofocus`** sweeps the stage's z through the camera's sharpness, returns the focus curve as an array, and emits a `focused` event.

```text
example-simulated-microscope -c examples/simulated-microscope/microscope.json
python examples/simulated-microscope/demo.py
```

Its image arithmetic is teta-wot's, operation for operation, so its sharpnesses and focus curve are the same numbers as the Rust example's.
