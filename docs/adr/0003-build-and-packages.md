# ADR-0003: Build and packages: CMake and vcpkg

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D3

## Context

Windows and Linux builds need the same dependencies (Drogon and its own, nlohmann-json, GoogleTest, later RE2, CLI11, Boost.PFR) at the same versions. teta-wot ships a server as one Windows executable.

## Decision

- **CMake ≥ 3.28, with `CMakePresets.json`**: `windows-msvc`, `windows-clang-cl`, `linux-gcc`, `linux-clang`, `linux-clang-asan`, `linux-clang-tsan`, all Debug with warnings as errors.
- **vcpkg in manifest mode**, pinned by `vcpkg-configuration.json`'s baseline. Optional parts are manifest features (`tests`, `drogon`); dependencies are added to `vcpkg.json` by the phase that first uses them, not ahead of time.
- Windows uses the `x64-windows-static` triplet with the static CRT, so a server is one `.exe`.
- A `system` preset builds without vcpkg, from `CMAKE_PREFIX_PATH` or the system, for environments where vcpkg can't download.
- The library installs a CMake package, `teta-wot` (`find_package(teta-wot CONFIG)`, targets `teta::wot`, `teta::wot-json`, …).

## Amendments

- **Phase 4 (2026-10-22):** Drogon is a dependency of the library, no longer a manifest feature, since the server is built on it (ADR-0004). The features are `tests` and `spdlog`.

## Consequences

- CI clones vcpkg at the baseline and caches its binary archives per preset.
- No other package manager is supported.
