# Phase 5: Composition, settings, configuration, lifecycle

Status: **done** (2026-10-22). Plan: [Â§5, Phase 5](../PLAN.md#phase-5-composition-settings-configuration-lifecycle-ml). Things now persist their settings, use each other through slots, and are served from configuration files with teta-wot's command line.

## What was built

| Deliverable | Where |
|---|---|
| Settings: `.setting()` on data and functional properties; the settings store (`Settings-{Class}.json`, keys sorted, four-space indentation, CRLF on Windows, written atomically and flushed to disk); saving after every change once loaded; loading before any Thing starts, with teta-wot's warnings; `to_settings_json`; `ThingHandle::settings_file` | `settings.hpp`, `src/core/settings.cpp`, `thing.hpp`, `runtime.hpp` |
| Slots: `Slot<T>`, `OptSlot<T>`, `SlotMap<T>`, declared with `t.slot(...)` and `.defaults_to(...)`; `SlotSelection`; connection with teta-wot's messages; the start order (dependencies first, configuration order on a cycle, with a warning); `RuntimeBuilder::thing_slots` | `slots.hpp`, `runtime.hpp`, `src/core/runtime.cpp` |
| Interfaces: `t.provides<I>()` (the Thing is an `I`) and `t.provides<I>(make)` (an adapter over a `ThingRef<T>`); `ThingHandle::provided<I>()` | `thing.hpp`, `runtime.hpp` |
| Services checked when the runtime is built: a Thing whose actions take a `Dep<S>` that isn't registered can't be built | `thing.hpp` (`ActionHandler::services`), `src/core/runtime.cpp` |
| Things built from their configuration: `ConfiguredThing` (a `Config` type and a constructor taking it) and `FromConfig`; `thing_from_config`, `thing_from_args`, with serde's messages for Things without a configuration | `runtime.hpp`, `thing_server.hpp` |
| The configuration model: `ServerConfig`, `ThingConfig`, `SecurityConfig`, `ConfigError` with pydantic's text | `config.hpp`, `src/server/config.cpp` |
| The registry: `ThingRegistry` (`add<T>(names)`, normalised names, the short C++ name, the last registration winning) | `registry.hpp`, `src/server/registry.cpp` |
| `ThingServer::from_config`; `ThingServerBuilder::mdns`; the listen check before the Things start | `thing_server.hpp`, `src/server/thing_server.cpp` |
| The command line: `tw::cli::serve_from_cli`, `serve_from_args`, `serve_until`, with clap's arguments, help and messages, and teta-wot's exit codes; arguments read as UTF-8 on Windows | `cli.hpp`, `src/server/cli.cpp` |
| The fallback server: `http::FallbackPage`, `http::fallback_answer` (transport-neutral), `serve_fallback` (on Drogon); the Drogon adapter serves any handler | `http/fallback.hpp`, `src/http/fallback.cpp`, `drogon.hpp`, `src/server/thing_server.cpp` |
| `testing::Harness`: one Thing under test, with stand-ins for its slots and a temporary settings folder (deferred from Phase 4) | `testing.hpp`, `src/server/testing.cpp` |
| The twin server on the library's registry and command line | `conformance/twins/`, `conformance/twin-server-cpp/` |
| `run.py`: settings steps (`file`, `write_file`, `restart`, `warnings`) and a scenario's `files`; `--cli` mode (exit codes, output without log lines, the fallback page) | `conformance/run.py` |
| Scenarios and fixtures: `settings` and `settings-load`; 21 command-line scenarios | `conformance/scenarios/http/`, `conformance/scenarios/cli/`, `conformance/fixtures/` |
| Five examples: `thing-slots`, `dependency-injection`, `settings`, `config-and-cli`, `graceful-shutdown` | `examples/` |

## The gate

- **Settings files are byte-identical** to the reference's: the `settings` scenario (every kind of setting: integers, floats down to `1e-7` and up to `1e+20`, Unicode and control characters, lists, maps, structs, optionals, a functional setting; refused writes; several at once; resets) records each file the server writes, and the C++ server's equal teta-wot's (`conformance.http`). The plan's `fixtures/settings/settings.json` is `fixtures/http/settings.json`.
- **Settings files are interchangeable both ways**: `settings-load` starts the server on a file written as teta-wot writes it, which loads, isn't rewritten by loading, and is rewritten identically by the next save; since the C++ server writes teta-wot's bytes, teta-wot loads its files. Restarts in `settings` load the server's own files, a hand-edited one, a list and broken JSON, with teta-wot's warnings, word for word.
- **The command line equals the reference's**: 21 scenarios (`conformance.cli`) compare exit codes and output (clap's help and errors byte for byte; configuration errors in pydantic's words; a missing file, a port in use, a Thing that can't be built or can't start) and the fallback page and its answers (redirects, CORS, a preflight). Two differences are masked and recorded as deltas (below).
- **The examples**: `thing-slots`, `dependency-injection`, `settings`, `config-and-cli` and `graceful-shutdown` print what their Rust originals print (but for the service's name, timings, paths and the interleaving of log lines); CTest runs them all.
- teta-wot has no tests of its own for settings, slots or the server's configuration (they live in its examples and the `wot` repository's fixtures), so the unit tests are new: settings (10), slots (10), the configuration model, registry and Things from configuration (12), the command line in-process (6), the fallback page (4), `Harness` (2), and a compile-fail case for a slot of an incomplete type.
- 313 tests pass with `windows-msvc`. Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now.

## Findings

- **Drogon turns a request for `/` into one for `/index.html`** (its home page) before the default handler sees it, and `getOriginalPath()` returns the rewritten path. It went unnoticed in Phase 4 because the binding answers both with the same 404; the fallback server answers `/` itself. The adapter now turns the home page off.
- **clap's messages vary in small ways**: a missing value has no usage line, an unexpected value for a flag shows a usage of that flag alone (`Usage: prog --fallback`), and the program's name is `argv[0]`'s file name. 30 argument cases were compared byte for byte before the scenarios were written.
- **teta-wot prints `listening on â€¦` once bound, before the Things start**, so a Thing that can't start still prints it. The C++ command line prints it then too when the port is given, and once Drogon listens for port 0.
- **A port in use**: trantor ends the process when it can't bind, so the server checks the address first (resolving a host name as tokio does, `SO_REUSEADDR` on POSIX as tokio and trantor set it) and fails with teta-wot's `can't listen on â€¦` before any Thing starts.
- **The C runtime writes `\r\n` on Windows** for each `\n` on a text-mode stream; teta-wot writes `\n`. `serve_from_cli`, which owns the process, puts standard output and error in binary mode.
- **MSVC's `path::string()` and `path(std::string)` use the ANSI code page**: paths from configuration files and messages go through UTF-8 (`detail::path_from_text`, `detail::path_text`).
- **A loaded file isn't rewritten until a setting changes**, so a hand-edited file keeps its unknown keys, CRLF or broken JSON until then; a functional setting saves its getter's value as last read through the property, so a change made directly in C++ is saved only by the property's next write (as in teta-wot).

## Deviations from the plan

- **Slots' targets must be complete types where the slot is declared.** Whether a slot holds a `ThingRef<T>` (a Thing type) or a `std::shared_ptr<T>` (an interface) is decided from the type; Things that use each other connect one way through an interface. A clear `static_assert` says so, and the compile-fail suite checks it.
- **Interfaces are abstract classes** with `t.provides<I>()`, where teta-wot has `#[wot::interface]` traits implemented for `ThingRef<T>`; `t.provides<I>(make)` is the equivalent of the latter.
- **The runtime empties slots when it is destroyed**, so Things that refer to each other don't keep each other alive (teta-wot's `Arc`s leak in that case).
- **A Thing's configuration is a `Config` type and a constructor** (teta-wot's `#[thing(config = â€¦)]` and `FromConfig`); `args` fill its fields in order, as serde fills a struct from a sequence.
- **`security` in a configuration is refused** (exit code 3) until the binding supports security in Phase 9, rather than served unprotected; the secret's environment variable is still checked first, as teta-wot does. **`mdns`** logs a warning until Phase 9.
- **Messages**: the `args`-and-`kwargs` error says *a C++ Thing's configuration*; a missing service is named by its short C++ name. Log lines keep teta-wot-cpp's format, on standard error. These are recorded in the [wire contract](../wire-contract.md#3-deltas).
- **The demonstration of `config-and-cli` serves once**: Drogon's application runs once per process, so it shows the fallback page's answers in-process (`fallback_answer`), where the Rust example serves it; `-c failing.json --fallback` serves it for real, and the `cli` scenarios compare it with teta-wot's.
- **`graceful-shutdown`** invokes the long action once the server listens, not after a fixed 100 ms.
