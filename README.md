[![CI](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/lucaswewa/teta-wot-cpp/actions/workflows/ci.yml)

# teta-wot-cpp

A C++ library for serving W3C Web of Things *Things* over HTTP, with functional and wire-level parity with [teta-wot](https://github.com/lucaswewa/teta-wot), built on [Drogon](https://github.com/drogonframework/drogon), for Windows and Linux.

```cpp
#include <teta/wot.hpp>
namespace tw = teta::wot;

class Light {
  public:
    tw::Prop<std::int64_t> brightness{100};
    tw::Prop<bool> is_on{false};
    void toggle() { is_on.update([](bool& on) { on = !on; }); }

    static void define(tw::ThingSpec<Light>& t) {
        t.property("brightness", &Light::brightness).ge(0).le(100);
        t.property("is_on", &Light::is_on).read_only();
        t.action("toggle", &Light::toggle);
    }
};

int main() { tw::ThingServer::builder().thing<Light>("light").build().serve("127.0.0.1", 5000); }
```

- [The guide](docs/guide/src/SUMMARY.md): the quickstart, a tutorial, and every feature
- [Examples](examples/README.md): 38 of them, each run by CI and compared with its Rust original
- [Changelog](CHANGELOG.md) and [releasing](docs/releasing.md)
- [Goals](docs/goals.md), the [plan](docs/PLAN.md), and the [documentation index](docs/README.md): decision records, phase notes, the wire contract

**Status:** all ten phases of the [plan](docs/PLAN.md) are done, towards v0.1.0: everything teta-wot v0.1.0 has (Things defined with `ThingSpec`, properties, actions and invocations, events, slots, settings, configuration files and teta-wot's command line, WebSockets and server-sent events, Blobs and MJPEG streams, arrays, the OpenAPI document and its docs pages, the `wot` wire profile, discovery and DNS-SD, security, the Windows service), answering as teta-wot does byte for byte; a systemd unit; a vcpkg overlay port; fuzz targets, a shutdown-under-load test and a soak tool. CI builds and tests on Windows (MSVC); the Linux builds aren't run in CI for now. See the [phase notes](docs/notes/README.md).

## Building

```text
cmake --preset windows-msvc     # from a developer prompt, or linux-gcc; needs VCPKG_ROOT
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

On Windows, `scripts\build.ps1` and `scripts\test.ps1` do the same from any PowerShell window; [scripts/](scripts/README.md) also serves and probes Things for testing by hand. See [Installing](docs/guide/src/tutorial/installing.md) for the other presets, the vcpkg overlay port, and using the library from your own project.

## Licence

MIT ([`LICENSE`](LICENSE)); the repository also holds work by others under their own licences ([`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)).
