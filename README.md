[![CI](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml)

# teta-wot-cpp

A C++ library for serving W3C Web of Things *Things* over HTTP, with functional and wire-level parity with [teta-wot](https://github.com/lucaswewa/teta-wot), built on [Drogon](https://github.com/drogonframework/drogon), for Windows and Linux.

- [Goals](docs/goals.md)
- [Architecture and implementation plan](docs/PLAN.md) (approved)
- [Documentation index](docs/README.md): decision records, phase notes, the wire contract, the guide

**Status:** Phases 0 to 3 are done: the build skeleton, the parity harness against the Rust reference, `teta::wot-json`, `teta::wot-td` (the TD model, DataSchemas from C++ types, W3C validation), and `teta::wot-core`: the validator (pydantic's lax coercions and errors) and the runtime, on threads: Things defined with `ThingSpec`, properties, actions and invocations, cancellation, the global lock, invocation logs, events and device actors. Things run in-process; the HTTP binding is Phase 4. See the [phase notes](docs/notes/README.md).

## Building

```text
cmake --preset linux-gcc        # or windows-msvc, from a developer prompt; needs VCPKG_ROOT
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

See [Building](docs/guide/src/building.md) for the other presets and for building without vcpkg.
