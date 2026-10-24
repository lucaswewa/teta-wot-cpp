# Changelog

All notable changes to teta-wot-cpp are recorded here, in the format of [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-10-24

The first release: a C++20 port of [teta-wot](https://github.com/lucaswewa/teta-wot) v0.1.0 that answers as it does on the wire, byte for byte (the [wire contract](docs/wire-contract.md) lists the few documented differences). Everything below is new.

### Added

- **JSON** (`teta::wot-json`): serde_json's parser and writer, ported, so numbers, strings and key order are written as teta-wot writes them.
- **Thing Descriptions** (`teta::wot-td`): the W3C TD 1.1 model and its builders; DataSchemas from C++ types (structs through Boost.PFR, enums, optionals, containers, `NdArray`), as schemars makes them from Rust types; validation against the W3C JSON Schema (`teta::wot-td-validation`).
- **The runtime** (`teta::wot-core`):
  - Things defined with `ThingSpec`: data and functional properties with pydantic-compatible validation and coercion, actions with invocations, cancellation, logs and the global lock, and events;
  - device actors for synchronous drivers;
  - slots and interfaces, settings files interchangeable with teta-wot's, services and the server handle.
- **The HTTP binding and server** (`teta::wot-http`, `teta::wot-server`, on Drogon):
  - teta-wot's routes, answers and errors, in the `tetathing` and `wot` (W3C WoT HTTP Basic and SSE Profiles) wire profiles;
  - configuration files, teta-wot's command line and its fallback server;
  - graceful shutdown on Ctrl-C, Windows' console events and SIGTERM;
  - `TestClient` and `Harness` for tests, in-process.
- **Observation:** WebSockets with event subscriptions, and server-sent events, both described in every TD.
- **Binary data:** Blobs as action inputs and outputs, and MJPEG streams with a viewer page.
- **OpenAPI 3.1:** the document, Swagger UI and ReDoc, offline with `TETA_WOT_DOCS_OFFLINE`.
- **Discovery and security:** `/.well-known/wot`, a TD Directory, DNS-SD with `TETA_WOT_MDNS`, and HTTP Basic or bearer credentials.
- **Arrays:** `NdArray<T, Rank>`, carried as nested lists as LabThings' `NDArray`.
- **Services:** running a server as a Windows service and installing one (`teta::wot::service`), and, on Linux, a `Type=notify` systemd unit (`teta::wot::systemd`), a preview: untested in this release.
- **Packaging:** a CMake package (`find_package(teta-wot)`), and a vcpkg overlay port (`ports/teta-wot`).
- **Examples:** 38 of them, teta-wot's 35 and three of the C++ project's own, from a hand-built TD to the simulated microscope and its service, each run by CI and compared with its Rust original on the wire.
- **The guide:** an mdBook guide (`docs/guide`).
- **Verification:** the parity harness (golden fixtures from teta-wot v0.1.0, and live differential runs), the corpora, fuzz targets (JSON, validation, the WebSocket dialect, request bodies), a shutdown-under-load test, and the soak tool (`tools/soak`).

### Known limitations

- **Linux isn't tested in this release.** CI builds and tests on Windows with MSVC only; the Linux presets, the systemd unit and the coverage-guided fuzzing (libFuzzer) are untested.

[0.1.0]: https://github.com/lucaswewa/teta-wot-cpp/releases/tag/v0.1.0
