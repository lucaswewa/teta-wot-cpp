# cmake-consumer

**Concept:** using teta-wot-cpp from another CMake project: `find_package(teta-wot CONFIG REQUIRED)` and `target_link_libraries(... teta::wot)`.

**Technologies:** the installed CMake package (`teta-wot-config.cmake`, targets `teta::wot`, `teta::wot-json`), and `<teta/wot.hpp>`.

This example has no counterpart in teta-wot: it is the C++ project's own (plan §6.3).

## Running it

In the tree, it is built and run with the other examples:

```text
cmake --preset linux-gcc && cmake --build --preset linux-gcc && ctest --preset linux-gcc -L example
```

Against an installed library, as an application would:

```text
cmake --install build/linux-gcc --prefix /tmp/teta-wot
cmake -S examples/cmake-consumer -B build/consumer -DCMAKE_PREFIX_PATH=/tmp/teta-wot
cmake --build build/consumer && ./build/consumer/example-cmake-consumer
```

## What to look for

It prints a JSON object written as teta-wot writes JSON, `1e16` included (`1e+16`), and exits 0. CI builds it both ways on Windows and Linux.
