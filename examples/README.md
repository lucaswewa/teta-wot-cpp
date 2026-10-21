# Examples

Each example teaches one concept with the public API only. Run without arguments, an example runs a short demonstration and exits (CTest runs every one, label `example`); examples that serve take `--serve`. The [plan's §6](../docs/PLAN.md#6-examples) lists all 38, by the phase that delivers them; this table lists those that exist.

| Example | Phase | Concept |
|---|---|---|
| [`cmake-consumer`](cmake-consumer/README.md) | 0 | Using teta-wot-cpp from another CMake project |
| [`td_builder`](td_builder/README.md) | 1 | A W3C Thing Description built by hand, validated against the W3C schema |
| [`schema-to-dataschema`](schema-to-dataschema/README.md) | 1 | How C++ types become TD DataSchemas |

Add an example with `teta_wot_add_example(<name> SOURCES main.cpp)` in `examples/<name>/CMakeLists.txt`, a `README.md` with **Concept**, **Technologies**, how to run it and what to look for, and a line in `examples/CMakeLists.txt` and here.
