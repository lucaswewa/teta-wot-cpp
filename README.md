[![CI](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml)

# teta-wot-cpp

A C++ library for serving W3C Web of Things *Things* over HTTP, with functional and wire-level parity with [teta-wot](https://github.com/lucaswewa/teta-wot), built on [Drogon](https://github.com/drogonframework/drogon), for Windows and Linux.

- [Goals](docs/goals.md)
- [Architecture and implementation plan](docs/PLAN.md) (approved)
- [Documentation index](docs/README.md): decision records, phase notes, the wire contract, the guide

**Status:** Phase 0 (baseline and design lock) is done: the build skeleton, `teta::wot-json`, the Drogon spike and the parity harness against the Rust reference. See the [Phase 0 note](docs/notes/phase-00-baseline.md).

## Building

```text
cmake --preset linux-gcc        # or windows-msvc, from a developer prompt; needs VCPKG_ROOT
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

See [Building](docs/guide/src/building.md) for the other presets and for building without vcpkg.
