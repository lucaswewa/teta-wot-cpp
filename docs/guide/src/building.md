# Building teta-wot-cpp

## With vcpkg (the supported way)

Install [vcpkg](https://vcpkg.io), set `VCPKG_ROOT`, and use a preset:

```text
cmake --preset linux-gcc            # or linux-clang, windows-msvc, windows-clang-cl
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

On Windows, run these from a Visual Studio developer command prompt, or use the [PowerShell scripts](https://github.com/lucaswewa/teta-wot-cpp/tree/main/scripts), which set up the environment themselves (`scripts\build.ps1`, `scripts\test.ps1`) and serve and probe Things for testing by hand. The Windows presets use the `x64-windows-static` triplet and the static C runtime, so programs are single executables.

The dependencies and their versions come from `vcpkg.json` and the baseline in `vcpkg-configuration.json`.

## Without vcpkg

The `system` preset finds the dependencies through `CMAKE_PREFIX_PATH` and the system (Drogon, nlohmann-json, Boost.PFR, RE2 and tl-expected; GoogleTest and json-schema-validator for the tests; spdlog for one example):

```text
CMAKE_PREFIX_PATH=/opt/deps cmake --preset system
cmake --build --preset system
ctest --preset system
```

The `conformance.http` test replays the reference's HTTP fixtures against the C++ twin server over sockets; it needs Python 3, and is skipped without it.

## Using it from your project

```cmake
find_package(teta-wot CONFIG REQUIRED)
target_link_libraries(my-server PRIVATE teta::wot)
```

The [`cmake-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cmake-consumer) example is the smallest such project.
