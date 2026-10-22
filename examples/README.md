# Examples

Each example teaches one concept with the public API only. Run without arguments, an example runs a short demonstration and exits (CTest runs every one, label `example`); examples that serve take `--serve`. The [plan's §6](../docs/PLAN.md#6-examples) lists all 38, by the phase that delivers them; this table lists those that exist.

| Example | Phase | Concept |
|---|---|---|
| [`cmake-consumer`](cmake-consumer/README.md) | 0 | Using teta-wot-cpp from another CMake project |
| [`td_builder`](td_builder/README.md) | 1 | A W3C Thing Description built by hand, validated against the W3C schema |
| [`schema-to-dataschema`](schema-to-dataschema/README.md) | 1 | How C++ types become TD DataSchemas |
| [`builder_thing`](builder_thing/README.md) | 3 | A Thing defined with ThingSpec, driven through the registry |
| [`property-cells`](property-cells/README.md) | 3 | `Prop<T>`: validation, notification, read-only |
| [`functional-properties`](functional-properties/README.md) | 3 | Getters, setters, resetters |
| [`cancellable-action`](cancellable-action/README.md) | 3 | Cooperative cancellation and child invocations |
| [`global-lock`](global-lock/README.md) | 3 | Actions and writes one at a time |
| [`device-actor`](device-actor/README.md) | 3 | A synchronous driver on its own thread |
| [`invocation-logs`](invocation-logs/README.md) | 3 | Per-invocation logs, and the spdlog sink |
| [`compile-errors`](compile-errors/README.md) | 3 | What the compiler says when a Thing is declared wrongly |

Add an example with `teta_wot_add_example(<name> SOURCES main.cpp)` in `examples/<name>/CMakeLists.txt`, a `README.md` with **Concept**, **Technologies**, how to run it and what to look for, and a line in `examples/CMakeLists.txt` and here.
