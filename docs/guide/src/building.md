# Building teta-wot-cpp

## With vcpkg (the supported way)

Install [vcpkg](https://vcpkg.io), set `VCPKG_ROOT`, and use a preset:

```text
cmake --preset linux-gcc            # or linux-clang, windows-msvc, windows-clang-cl
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

On Windows, run these from a Visual Studio developer command prompt. The Windows presets use the `x64-windows-static` triplet and the static C runtime, so programs are single executables.

The dependencies and their versions come from `vcpkg.json` and the baseline in `vcpkg-configuration.json`.

## Without vcpkg

The `system` preset finds the dependencies through `CMAKE_PREFIX_PATH` and the system (nlohmann-json; GoogleTest for the tests; Drogon for the spike):

```text
CMAKE_PREFIX_PATH=/opt/deps cmake --preset system
cmake --build --preset system
ctest --preset system
```

## Using it from your project

```cmake
find_package(teta-wot CONFIG REQUIRED)
target_link_libraries(my-server PRIVATE teta::wot)
```

The [`cmake-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cmake-consumer) example is the smallest such project.
