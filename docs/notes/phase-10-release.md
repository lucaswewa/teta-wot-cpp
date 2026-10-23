# Phase 10: Arrays, services, hardening, release

Status: **in progress**. Plan: [§5, Phase 10](../PLAN.md#phase-10-arrays-services-hardening-release-m). The last phase is delivered in parts, each reviewed and merged on its own: arrays; the Windows service and the systemd unit, with `microscope-service`; the capstone `simulated-microscope`; hardening (fuzz targets, the shutdown-under-load test, the soak tool); the full guide, the vcpkg port and the v0.1.0 release.

## Part 1: arrays (done)

| Deliverable | Where |
|---|---|
| `NdArray<T, Rank>`, a port of teta-wot-core's `array` (its `NdArray<A, D>` wraps the `ndarray` crate; this holds a shape and its elements): nested lists on the wire, a fixed rank or `dynamic_rank`, LabThings' `NestedListOfNumbersModel` for any rank, numpy's message for ragged lists as a `value_error`; `from_shape`, `zeros`, `filled`, `at`, `map`, `sum` | `include/teta/wot/ndarray.hpp`, `src/core/ndarray.cpp` |
| The `NDArrayProbe` twin: all nine twins are now C++ Things whose TDs equal the reference's | `conformance/twins/` |
| Scenarios and fixtures: `arrays`, `arrays-wot`; `tds`, `tds-wot`, and `ndarray_probe` in `openapi` and `openapi-wot`, now run against the C++ twin server; example scenario `ndarray-property` | `conformance/`, `tests/CMakeLists.txt` |
| The `ndarray-property` example | `examples/ndarray-property/` |

**The gate's part**: the array scenarios, ragged arrays included (ragged and mixed lists at every depth, numbers and lists mixed, strings, objects, scalars, empty and deep arrays, through properties, several properties at once and action inputs and outputs), equal the reference's in both profiles; every twin's TD and OpenAPI document equals the reference's; `ndarray-property` prints what teta-wot's prints and answers as it does. teta-wot-core's `array.rs` tests are ported, with tests of the array itself (5).

**Findings**:

- **The C++ validator already handled the array schema** (Phase 2 ported teta-wot's special case for unions of nested numbers); only the type and its codec were missing.
- **A schema's root `title`**: `NdArray<>`'s is `NestedListOfNumbersModel`, which teta-wot's `for_type` drops at the root, like a Blob's (Phase 8); the codec names it, so the same rule applies.
- **teta-wot's `ndarray-property` example installs no log subscriber when it serves**, and without one teta-wot records nothing in invocation logs; teta-wot-cpp always records them. Its example scenario leaves out the log of a failed invocation.

**Deviation**: there is no `TETA_WOT_NDARRAY` option. teta-wot's feature exists to avoid depending on the `ndarray` crate; `NdArray` depends on nothing, so it is always there.
