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

## Part 3: the Windows service and the systemd unit (done)

| Deliverable | Where |
|---|---|
| `tw::service`, a port of teta-wot-server's `service.rs` on the Win32 API: `run_as_service` (the SCM's *stop* and *shutdown* as the stop signal; *start pending*, *running*, *stop pending* with a wait hint, *stopped* with the exit code; `NotAService` outside the SCM), `install` (LocalSystem, on demand or automatic, with a description) and `uninstall` (stops, then deletes); `command_line`, quoting arguments as `CommandLineToArgvW` reads them | `include/teta/wot/service.hpp`, `src/server/service.cpp` |
| `tw::systemd`, the Linux counterpart teta-wot doesn't have: `notify` (`sd_notify`'s protocol, a datagram to `$NOTIFY_SOCKET`, path or abstract, without libsystemd), `notify_ready`, `unit_file` (`Type=notify`) and `exec_word` (`ExecStart=` quoting); `ThingServer` (and the fallback server) send `READY=1` once listening and `STOPPING=1` when shutting down | `include/teta/wot/systemd.hpp`, `src/server/systemd.cpp`, `src/server/thing_server.cpp` |
| `tw::cli::arguments`: the process's arguments as UTF-8 (on Windows, from the wide command line), which `serve_from_cli` used internally | `include/teta/wot/cli.hpp` |
| The `microscope-service` example: `install`, `uninstall`, `run` (what the SCM or systemd starts; a Windows service logs to a file), `console`, and without arguments the stop path demonstrated without the SCM; `service_test.ps1` (teta-wot's, with the C++ executable's path) and `service_test.sh` | `examples/microscope-service/`, `examples/common/http_client.hpp` |
| CI runs `service_test.ps1` on Windows (the runners are administrators), and `service_test.sh` on `linux-gcc` once the Linux jobs are back | `.github/workflows/ci.yml` |
| Tests: command lines (and, on Windows, read back with `CommandLineToArgvW`), a process the SCM didn't start, a missing service's uninstall, `ExecStart=` words, unit files, notification addresses; on Linux, notifications received on a path socket and an abstract one (8 tests on Windows, 9 on Linux) | `tests/server/service_test.cpp` |

**The gate's part**: the stop during a long action: the example's demonstration serves the microscope through the command line as the service does, starts a 50-second move, raises the stop signal, and the server stops at once with exit code 0, the move cancelled and logged. `service_test.ps1` does the same through the SCM, checking the exit code and the log, on CI.

**Findings**:

- **Windows takes one service dispatcher per process**, even after an attempt that failed because the SCM didn't start the process: a second `StartServiceCtrlDispatcher` fails with `ERROR_SERVICE_ALREADY_RUNNING`. teta-wot's `AlreadyRunning` covers only a call while another is running; here a second call throws `AlreadyRunning` whatever became of the first.
- **No library changes for the stop itself**: the SCM's control handler raises a `std::stop_source`, whose callback only wakes the server's stopper thread, so the handler returns at once, as the SCM wants; the shutdown is Phase 5's.
- **Logging to a file needs no new API**: the console sink writes to standard error, which the service's `run` reopens on the log file. teta-wot installs a file subscriber, and its `logging::init` then fails to install its own, so a teta-wot service records no invocation logs; teta-wot-cpp's does.

**Deviations**:

- The systemd unit, `tw::systemd` and `service_test.sh` are this project's own (plan §7). **They haven't been compiled or run yet**: there is no Linux machine here, and CI runs only `windows-msvc` for now. Only the portable parts (unit files, `ExecStart=` quoting, notification addresses) are tested; the socket code and `service_test.sh` wait for the Linux jobs.
- There is no `TETA_WOT_WINDOWS_SERVICE` option: like `NdArray`, the service needs nothing beyond the operating system (`advapi32`).

## Part 4: hardening (done)

| Deliverable | Where |
|---|---|
| Four fuzz targets, each `fuzz::one_input` with its invariants: `json` (parse, dump and dump_pretty round trips), `validation` (`{"schema", "input"}` documents through the TD model and the validator, each tried again with awkward values; idempotent coercion), `websocket` (the dialect's parser: teta-wot's proptest invariants) and `request_body` (any body to every kind of route of a probe Thing, both wire profiles, eight content types: no 500, JSON where it says JSON) | `fuzz/` |
| A driver for them where libFuzzer isn't used: the seeds, then mutations (bit flips, interesting bytes, insertions, deletions, splices, a JSON dictionary, awkward numbers) from a fixed random seed; CTest runs each briefly (label `fuzz`). With Clang, `TETA_WOT_LIBFUZZER` links them with libFuzzer instead | `fuzz/driver.cpp`, `fuzz/entry.cpp`, `fuzz/CMakeLists.txt` |
| Seeds from the repository's corpora (which teta-wot produced) and the scenarios, written by `make_seeds.py` | `fuzz/seeds/`, `fuzz/make_seeds.py` |
| The soak tool, a port of teta-wot's: the simulated microscope as its own process, every kind of client on its own thread (properties, moves and cancelled moves, autofocus runs, captures and their downloads, two MJPEG viewers, two SSE streams, a WebSocket), a report each minute (with the server's private bytes and handles from the Win32 API, or `/proc` on Linux) and the same verdict | `tools/soak/` |
| The shutdown-under-load test: the soak tool's `--stop-under-load`, which ends a run by stopping the server as Ctrl-C does while every client is at work and a long move runs, and fails unless it exits with 0 within 15 seconds, the move cancelled (CTest `tools.shutdown-under-load`) | `tools/soak/main.cpp`, `tools/soak/CMakeLists.txt` |
| CI soaks the microscope for 3 minutes | `.github/workflows/ci.yml` |

**The gate's part**: the fuzz targets run in CI (briefly) and ran here for nearly three million inputs with several seeds, without a failure but the one below; the shutdown-under-load test passes; a 20-minute soak passes (about 20,000 requests a minute, no error, no stream ended, private bytes level after the invocations' retention). The 24-hour soak is the release's (part 5).

**Findings**:

- **A crash at exit after serving under load**, found by the shutdown-under-load test on its first run: Drogon keeps the server's handlers, and so its runtime, until the process exits; the runtime's invocations then free their Blobs, which unregister from the Blob registry, a function-local static already destroyed (it was made after Drogon's router). The registry, the log's settings and the console sink's lock are now never destroyed. A run without Blobs left in invocations didn't crash, which is why the examples' scenarios, which stop servers too, never saw it.
- **The JSON round trip isn't exact, as in serde_json**: the fuzzer's first finding was `-94.7862E203`, which parses to a double whose shortest text parses to its neighbour. serde_json's parser without `float_roundtrip` does the same (Phase 4), so the invariant allows floats two ULPs apart.
- **teta-wot v0.1.0 has less hardening than its changelog says**: the WebSocket parser's proptest (ported in Phase 6, and now a fuzz target too) and the soak tool; no fuzzing of validation or request bodies, and no shutdown-under-load test. teta-wot-cpp's are its own.
- **The memory of a soak rises for its first minutes**: the server keeps finished invocations for 300 s, as teta-wot does, so a run shorter than about 20 minutes can't judge a leak; the 3-minute CI run judges errors and streams only.
- **Part 3's service test, on CI, found two bugs** (fixed in part 3's pull request): the Windows service reopened its standard streams on the log with `_wfreopen_s`, which opens files without sharing, so the second failed, its stream was left closed, and the first write to it ended the process; and Drogon makes 256 upload folders under `./uploads` when it starts, which for a service is `System32` (and for every run, the working directory). The streams are reopened with sharing (or onto the null device), and Drogon's uploads go to a temporary folder, removed after serving.

**Deviations**:

- libFuzzer runs need Clang (Linux CI, which is off for now); the driver runs everywhere, without coverage feedback.
- The soak tool measures the server with the Win32 API (or `/proc`) rather than PowerShell, and has `--stop-under-load`, which teta-wot's hasn't.
