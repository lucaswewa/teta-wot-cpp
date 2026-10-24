# Installing teta-wot-cpp

## The toolchain

- **A C++20 compiler:** MSVC from Visual Studio 2022 17.10 or later on Windows; GCC 13 or Clang 17 on Linux.
- **CMake 3.28** or later, and Ninja.
- **[vcpkg](https://vcpkg.io)**, with `VCPKG_ROOT` set: the dependencies (Drogon, nlohmann/json, Boost.PFR, RE2, tl-expected; GoogleTest and json-schema-validator for the tests) come from it, at the baseline pinned in `vcpkg-configuration.json`.

## Building the repository

Use a preset:

```text
cmake --preset windows-msvc            # or linux-gcc, linux-clang, windows-clang-cl
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

On Windows, run these from a Visual Studio developer command prompt, or use the [PowerShell scripts](https://github.com/lucaswewa/teta-wot-cpp/tree/main/scripts) (`scripts\build.ps1`, `scripts\test.ps1`), which set up the environment themselves, and serve and probe Things for testing by hand. The Windows presets use the `x64-windows-static` triplet and the static C runtime, so programs are single executables.

| Preset | What |
|---|---|
| `windows-msvc`, `linux-gcc`, `linux-clang`, `windows-clang-cl` | Debug builds with the tests and examples |
| `windows-msvc-release` | A release build, for packages and the soak test |
| `windows-msvc-docs-offline` | With Swagger UI and ReDoc compiled in |
| `linux-clang-asan`, `linux-clang-tsan` | With AddressSanitizer or ThreadSanitizer |
| `system` | Without vcpkg: dependencies from `CMAKE_PREFIX_PATH` and the system |

Some tests need Python 3 (the conformance harness), and are skipped without it; a few need Python packages (`openapi-spec-validator`, `zeroconf`, `labthings-fastapi`), and are skipped without them.

## Options

| CMake option | What it adds |
|---|---|
| `TETA_WOT_MDNS` | Advertising the server with DNS-SD ([Discovery](../discovery.md)); on in the presets |
| `TETA_WOT_DOCS_OFFLINE` | Swagger UI and ReDoc compiled in, for networks without internet access ([Documentation](../documentation.md)) |
| `TETA_WOT_TD_VALIDATION` | `teta::wot-td-validation`: validating Thing Descriptions against the W3C JSON Schema |
| `TETA_WOT_BUILD_TESTS`, `TETA_WOT_BUILD_EXAMPLES`, `TETA_WOT_BUILD_FUZZ`, `TETA_WOT_BUILD_TOOLS` | The tests, the examples, the fuzz targets, the soak tool (on in a top-level build) |

Arrays, Windows services and the testing helpers need no option: they depend on nothing more.

## Using it from your project

### With the vcpkg overlay port

The repository has a vcpkg port, [`ports/teta-wot`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/ports/teta-wot), which builds the checkout it lives in. In a project's `vcpkg.json`, depend on it:

```json
{
  "dependencies": ["teta-wot"]
}
```

and point `vcpkg-configuration.json` at the overlay (check out a release tag, such as `v0.1.0`):

```json
{
  "overlay-ports": ["../teta-wot-cpp/ports"]
}
```

Its features are `mdns` and `docs-offline`. Or, in classic mode: `vcpkg install teta-wot --overlay-ports=<checkout>/ports`.

### As an installed CMake package

`cmake --install build/<preset> --prefix <dir>` installs the libraries and headers with a CMake package. Either way, a project then finds it:

```cmake
find_package(teta-wot CONFIG REQUIRED)
target_link_libraries(my-server PRIVATE teta::wot)
```

`teta::wot` is everything; the parts are `teta::wot-json`, `teta::wot-td`, `teta::wot-core`, `teta::wot-http`, `teta::wot-drogon` and `teta::wot-server`. `#include <teta/wot.hpp>` brings in what Thing code usually needs. The [`cmake-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cmake-consumer) example is the smallest such project, and CI builds it against an installed tree.

## Without a compiler

Each [release](https://github.com/lucaswewa/teta-wot-cpp/releases) has a Windows package with the simulated microscope's server, ready to run, for trying teta-wot-cpp out.

## The Python side

LabThings' Python client, `labthings_fastapi.ThingClient`, works against teta-wot-cpp servers:

```text
pip install labthings-fastapi==0.3.0
python examples/simulated-microscope/demo.py --spawn build/windows-msvc/examples/simulated-microscope/example-simulated-microscope.exe
```
