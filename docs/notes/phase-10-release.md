# Phase 10: Arrays, services, hardening, release

Status: **in progress**. Plan: [§5, Phase 10](../PLAN.md#phase-10-arrays-services-hardening-release-m). The last phase is delivered in parts, each reviewed and merged on its own: arrays; the capstone `simulated-microscope`; the Windows service and the systemd unit, with `microscope-service` (after the microscope, whose library it serves); hardening (fuzz targets, the shutdown-under-load test, the soak tool); the full guide, the vcpkg port and the v0.1.0 release.

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

## Part 2: the simulated microscope (done)

| Deliverable | Where |
|---|---|
| The capstone example, a port of teta-wot's `simulated-microscope`: a `Stage` (moves in 20 ms ticks at a `speed` setting, cancellable, `arrived`), a `Camera` (the stage in a slot; a thread rendering a field of cells into an MJPEG preview, blurred away from z = 250; `capture` as a JPEG Blob; `sharpness`; an `exposure` setting; `captured`) and an `Autofocus` (two slots; a z sweep returning its curve as an `NdArray<double, 2>`; `last_focus`; `focused`); `registry()`; the in-process demonstration; teta-wot's command line otherwise | `examples/simulated-microscope/` |
| The microscope's Things as a library, `simulated-microscope-lib`, for `microscope-service` (part 3) and the soak tool (part 4); `microscope.json` compiled in as `microscope::config` | `examples/simulated-microscope/microscope.hpp`, `microscope.cpp`, `CMakeLists.txt` |
| `microscope.json` and `demo.py`, teta-wot's files byte for byte; CTest runs `demo.py --spawn` against the example (`example.simulated-microscope.demo-py`, skipped without `labthings-fastapi`, which CI installs) | `examples/simulated-microscope/`, `.github/workflows/ci.yml` |
| Greyscale JPEGs in the examples' encoder (one component, the luminance tables) | `examples/common/jpeg.hpp` |
| The example scenario `simulated-microscope`, served from `microscope.json`'s Things through the command line; `run.py`: `ignore_body` for MJPEG frames (their part headers, without the JPEG data) | `conformance/` |

**The gate's part**: the example scenario equals the reference's: the three TDs and the OpenAPI document, moves with their logs and `arrived`, refused inputs, writes and bounds, the sharpness out of focus (`0.000904524376824333`) and in focus (`25.939508228841518`), the settings file, the autofocus's 21-point curve and its log of every move, `focused`, a capture, the preview's frames and viewer, and a cancelled move; `run.py diff` against teta-wot's example agrees on repeated runs. The demonstration prints what teta-wot's prints, and `demo.py` (LabThings' `ThingClient`) prints the same against both, the JPEGs' sizes aside; the captured images look the same. Cancelling an autofocus mid-sweep leaves the stage where it was in both.

**Findings**:

- **Nothing in the library needed changing**: slots, settings, streams, Blobs, events, arrays, functional properties, nested action calls and the command line, each ported in an earlier phase, compose as teta-wot's do.
- **The same numbers, bit for bit**: the rendering, blur and sharpness are ported operation for operation (Rust's `div_euclid`, `rem_euclid`, wrapping multiplications, `round`, the running sums in the same order), and the doubles come out identical. That needs the compiler not to fuse multiplies and adds: MSVC's `/fp:precise` doesn't, and the library is compiled with `-ffp-contract=off` elsewhere.
- **The invocation logs are on the wire**, so the stage's log line formats a `Position` as Rust's `Debug` does (`Position { x: 0, y: 0, z: 0 }`), and the autofocus's `{:.3}` is `std::format`'s `{:.3f}` (both round the exact binary value). The demonstration also writes Rust's `Debug` of an event (`Some(Number(250))`) and of the start order.
- **Nested calls**: the autofocus moves the stage through its action (`stage->call(&Stage::move_to, ...)`), which runs in the autofocus's invocation: its moves are logged there, and cancelling the autofocus ends the move in progress, as Rust's task-local cancellation does.
- **The JPEGs differ**, as in Phase 7: this project's encoder isn't `jpeg-encoder`, so the scenario doesn't compare JPEG data, and the demonstration's byte counts differ (2712 and 2796 bytes for the same capture).

**Deviation**: `demo.py`'s comments still name Cargo's commands, since the file is teta-wot's unchanged; the example's README gives the C++ ones.
