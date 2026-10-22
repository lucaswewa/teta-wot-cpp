[![CI](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml)

# teta-wot-cpp

A C++ library for serving W3C Web of Things *Things* over HTTP, with functional and wire-level parity with [teta-wot](https://github.com/lucaswewa/teta-wot), built on [Drogon](https://github.com/drogonframework/drogon), for Windows and Linux.

- [Goals](docs/goals.md)
- [Architecture and implementation plan](docs/PLAN.md) (approved)
- [Documentation index](docs/README.md): decision records, phase notes, the wire contract, the guide

**Status:** Phases 0 to 6 are done: the build skeleton, the parity harness against the Rust reference, `teta::wot-json`, `teta::wot-td` (the TD model, DataSchemas from C++ types, W3C validation), `teta::wot-core` (the validator, and the runtime on threads: Things defined with `ThingSpec`, properties, actions and invocations, cancellation, the global lock, invocation logs, events and device actors), the first end-to-end milestone: `teta::wot-http` (the HTTP binding, answering as teta-wot does byte for byte), its Drogon transport, `ThingServer` and the in-process `TestClient`; and composition: settings files interchangeable with teta-wot's, slots and interfaces, services, configuration files, the registry, teta-wot's command line and its fallback server; and observation: WebSockets and server-sent events, with backpressure. See the [phase notes](docs/notes/README.md).

## Building

```text
cmake --preset linux-gcc        # or windows-msvc, from a developer prompt; needs VCPKG_ROOT
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

On Windows, `scripts\build.ps1` and `scripts\test.ps1` do the same from any PowerShell window; [scripts/](scripts/README.md) also serves and probes Things for testing by hand. See [Building](docs/guide/src/building.md) for the other presets and for building without vcpkg.
