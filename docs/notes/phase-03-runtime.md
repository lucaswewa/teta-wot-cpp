# Phase 3: Runtime core

Status: **done** (2026-10-22). Plan: [§5, Phase 3](../PLAN.md#phase-3-runtime-core-l).

## What was built

| Deliverable | Where |
|---|---|
| Cooperative cancellation: a resettable token whose observation consumes it (`check`, `sleep`, `cancelled`, `wait_for`), with callbacks for waits that combine it with something else | `cancel.hpp` |
| The global lock: reentrant per invocation (its root's ID), 50 ms acquisition timeout, `GlobalLockBusy` | `lock.hpp` |
| Logging (ADR-0010): `tw::log::info(...)` and the other levels, with `std::format` and the call's location; per-invocation capture (the last 1000 records, in Python's record shape); a console sink; an spdlog sink, header-only, for driver code that logs with spdlog | `log.hpp`, `log_spdlog.hpp` |
| The message broker: per-subscriber buffers of 5, drops counted and warned about, one subscription for several affordances, closing ends every stream | `broker.hpp` |
| Invocations: `pending` → `running` → `completed`/`error`/`cancelled`, retention and lazy expiry, records, the manager (with teta-wot's two cancel refusals) | `invocation.hpp` |
| The worker pool: Thing code runs on its threads; it grows when a task finds no idle thread, up to 1024 | `pool.hpp` |
| Invocation scopes (thread-local: the invocation's ID, lock owner, cancel token and log) and `ActionCtx` (`sleep`, `check_cancelled`, `cancelled`, `hold_global_lock`, `spawn_child`, `blocking`, `server`) with child invocations that pass cancellation on | `context.hpp` |
| `Prop<T>` (constraints, `set`/`update`/`reset`, `Watcher<T>`), functional properties, `PropertyError` with teta-wot's problems, `PropertyEntry` (what clients reach: validation under `body`, `null` as a missing body, read-only, the lock) | `property.hpp` |
| `Event<T>` (emit from any thread to the broker and to C++ receivers), `EventEntry` | `event.hpp` |
| Device actors: a driver created, used and destroyed on its own thread; a bounded mailbox; `call`, `post`, timeouts, faults, `reset`, `abort` | `device.hpp` |
| `ActionError` (ADR-0009): what an exception means for an invocation, its problem's title, its message chain and traceback; `HandledError`; `Expected<T>` (`tl::expected`) | `action.hpp` |
| `ThingSpec<T>`, the definition API: data and functional properties with their options and constraints, actions with named parameters (`t.action<"x", "y">(...)`), parameter defaults and descriptions, `tw::whole_input`, injected `ActionCtx&`, `Server` and `Dep<S>`, events, devices, lifecycle hooks, `thing_state` | `thing.hpp` |
| The runtime: `RuntimeBuilder` (lock, timeout, log level, services, application configuration, clock), name checks, `Runtime` (start in order, stop in reverse, shutdown with a grace period), `ThingHandle` (registry and TD), `ActionEntry` (`invoke`, in-process `call`, `validate_input`), `ThingRef<T>` (typed in-process calls through member pointers), `Server`, `Dep<S>` | `runtime.hpp`, `server.hpp` |
| Codecs decode: `Codec<T>::decode` for every built-in codec, with serde's error messages | `codec.hpp` |
| The twins as C++ Things (seven of nine), for the Phase 4 twin server | `conformance/twins/` |
| The compile-fail suite: nine declarations that mustn't compile, each checked for its message | `tests/compile_fail/` |
| Eight examples | `examples/` |

## The gate

- **teta-wot-core's tests are ported and pass**: `cancel` (12 tests), `property` (15), `runtime` (20, with tests of `Expected` outputs and name checks added), `device` (12, with `post` and Rust's duration format added), and `validate` (Phase 2). teta-wot's `macros.rs` tests are ported in-process (`tests/core/thing_spec_test.cpp`, 11 tests, with child invocations, services, typed in-process calls and error titles): 172 unit tests in all, besides the examples and the compile-fail cases.
- **The definitions give the reference's TDs**: the seven twins are now C++ Things defined with `ThingSpec`, and their TDs, assembled by their runtime, equal the reference's byte for byte in both wire profiles (`tests/td/twin_tds_test.cpp`, which in Phase 1 compared hand-written descriptions). The `validation` scenario (Phase 2) replays in-process on the twins: every write, read and invocation the scenario makes gets the reference's outcome, 422s byte for byte.
- **TSan clean**: the whole suite, and fifteen repetitions of the threaded tests, report no race (one suppression, for `std::exception_ptr`'s reference count in libstdc++, which isn't built with TSan; `tests/tsan.supp`). CI runs the TSan build. ASan and UBSan are clean too.
- **The compile-fail suite**: each of the nine cases fails to build with its message (`ctest -L compile_fail`).
- **The examples**: `builder_thing`'s TD equals the Rust example's byte for byte (and its invocation record but for the ID and times); `property-cells`, `global-lock` and `device-actor` print what the Rust examples print; `cancellable-action` too, but for a count that depends on timing; `invocation-logs` prints the same records but for the times and line numbers. `functional-properties` runs in-process (its Rust counterpart goes through HTTP). `compile-errors` lists the compile-fail cases.

## Findings

- **What was `async` is blocking.** An action, a getter or a device call is ordinary code on a worker thread; waits that Rust `select!`s (a child's end or the parent's cancellation) wait on a condition variable woken by either, through cancel-token callbacks. Rust's paused-clock tests became an injectable clock for retention (`RuntimeBuilder::clock`) and short real timeouts elsewhere.
- **Same-value writes notify**, as `watch::Sender::send_replace` does: a `Watcher` sees a version, not a value.
- **An invocation's `input` is the decoded input written back**, with defaults filled in, as serde serialises the generated input struct (`{}` for `add` is recorded as `{"step": 2}`). `#[param(default)]` is `.param(name).defaulted()`: the schema gets the type's default value, as schemars writes it.
- **Error titles** come from the exception's type, as teta-wot's come from the Rust error type: `DeviceError`, `ValidationError`, a Thing's own `MotorJammed`. The standard library's exceptions are `Error`, as an `anyhow::Error` is; `std::throw_with_nested` wraps an exception in a type of its own, so the title is the wrapped exception's. The message joins the causes (`a: b`, anyhow's `{:#}`) and the traceback lists them as anyhow's `{:?}` does.
- **Panics.** C++ has no panic to catch. `throw "message"` (anything that isn't a `std::exception`) is an action's panic: `Panicked`, "The action panicked: message". In a device call, a `std::exception` is a driver error (the device stays ready) and `DeviceFault` (or a non-`std::exception`) faults it. A crash in driver code can't be caught, unlike a Rust panic (ADR-0009).
- **Linux keeps 15 bytes of a thread's name**, so `device:<thing>:<name>` is truncated in the operating system; `device_thread_name()` gives the full name on a device thread.
- **TSan and `exception_ptr`**: libstdc++'s reference counting of an exception passed between threads is atomic, but isn't instrumented, so TSan reports the destroying thread's free against the reading thread's load. It is suppressed narrowly.

## Deviations from the plan

- **Not yet in the definition API:** settings (`.setting()` is accepted and changes nothing until Phase 5), slots and interfaces (Phase 5), custom endpoints (Phase 4), MJPEG streams and Blobs (Phase 7), `NdArray` (Phase 10), building a Thing from its configuration (Phase 5).
- **`#[input]`** is `t.action("name", &T::f, tw::whole_input)` rather than `.input()`: the input mode decides the action's schema at compile time.
- **Typed in-process calls** go through the member function: `thing_ref.call(&Stage::move_to, 0, 0, 250)`, in place of the generated `{Thing}Actions` trait; actions defined by a lambda are called by name (`call_action`).
- **Devices**: `call` blocks its caller (every caller is a thread), so Rust's `call_blocking`, `try_call` and `try_call_blocking` are one `call`; `post` queues a call and returns its future.
- **C++ event receivers** count the events they missed (`missed()`), where a Rust receiver gets a `Lagged` error.
- **Log events have no structured fields**: they are formatted into the message (`strokes={}` gives teta-wot's `strokes=3`). The console sink writes to standard error.
- **The worker pool grows but doesn't shrink.**
- **Affordance names**: teta-wot allows Unicode letters and digits; any code point beyond ASCII is accepted here.
- **`functional-properties`, `cancellable-action` and `invocation-logs`** run in-process, as the plan says for Phase 3; `functional-properties` prints the outcomes of calls where the Rust example prints HTTP statuses, until `TestClient` (Phase 4).
