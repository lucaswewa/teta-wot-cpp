# Scripts

PowerShell scripts for building and for testing by hand on Windows (written for Windows PowerShell 5.1, and tested with it; PowerShell 7 should work too). Run them from any folder; they find the repository themselves. Every script has help: `Get-Help scripts\serve.ps1 -Detailed`.

They set up the build environment themselves, so a plain PowerShell window works (no developer prompt needed): they find Visual Studio's C++ tools with `vswhere` and load `vcvars64.bat`, and keep your `VCPKG_ROOT`. A developer prompt points `VCPKG_ROOT` at Visual Studio's own copy of vcpkg, which would rebuild every package; set `TETA_WOT_VCPKG_ROOT` to choose another vcpkg explicitly.

| Script | What it does |
|---|---|
| `build.ps1` | Configures a preset when it needs to, and builds it (or `-Target`s); `-Test` runs the tests after |
| `test.ps1` | Runs the tests: all, a `-Suite` (`unit`, `examples`, `conformance`, `compile-fail`), a CTest `-Filter`, or a GoogleTest filter (`-GTest`) |
| `serve.ps1` | Serves Things until Ctrl-C: an example, the C++ twin server (chosen twins, a scenario's configuration, or a configuration file), or the Rust reference with the same configuration |
| `probe.ps1` | Explores a running server (its Things, every property's value, the invocations), invokes an action and waits for it, or watches server-sent events |
| `conformance.ps1` | Compares with the Rust reference (`conformance/run.py`): the fixtures, live (`-Mode diff`), or recaptures them; `-Examples` for the examples |
| `common.ps1` | What the others share (dot-sourced) |

## Building and testing

```powershell
scripts\build.ps1                          # configure if needed, build everything
scripts\build.ps1 -Target twin-server      # one target
scripts\build.ps1 -Configure               # after changing vcpkg.json or a preset
scripts\test.ps1                           # every test
scripts\test.ps1 -Suite conformance        # the reference's fixtures over sockets
scripts\test.ps1 -GTest 'Http.*:Sse.*'     # GoogleTest directly, with its output
```

The first configure installs the dependencies with vcpkg, which takes a while (Drogon alone, about ten minutes).

## Testing by hand

Serve something in one window:

```powershell
scripts\serve.ps1                                      # every C++ twin on http://127.0.0.1:5000
scripts\serve.ps1 -Things counter,http_probe -GlobalLock
scripts\serve.ps1 -Scenario default                    # a scenario's configuration
scripts\serve.ps1 -Things counter -Prefix /api/v1 -WireProfile wot
scripts\serve.ps1 -Example light                       # an example, with --serve
```

and poke it from another:

```powershell
scripts\probe.ps1                                         # what is served, with every value
scripts\probe.ps1 -Invoke http_probe/slow -Body '{"seconds": 2}'
scripts\probe.ps1 -Watch http_probe/value                 # server-sent events, until Ctrl-C
curl.exe -i -X PUT -d 5 http://127.0.0.1:5000/http_probe/value
```

To compare with teta-wot by hand, serve the same configuration from the Rust reference on another port, and send both the same requests:

```powershell
scripts\serve.ps1 -Scenario default                       # C++, port 5000
scripts\serve.ps1 -Scenario default -Reference -Port 5001 # Rust, port 5001 (built with cargo if needed)
```

`scripts\conformance.ps1 -Mode diff` does the same comparison automatically, for every scripted request.

The twins are `mything`, `broken`, `counter`, `types`, `http_probe`, `settings_probe` and `ws_probe` ([conformance/README.md](../conformance/README.md#the-twins) says what each exercises).

## Comparing with the reference

```powershell
scripts\conformance.ps1                                   # the C++ twin server against the fixtures
scripts\conformance.ps1 -Mode diff -Scenario default      # live, against the Rust reference
scripts\conformance.ps1 -Examples                         # the C++ examples against the fixtures
scripts\conformance.ps1 -Examples -Mode diff              # against teta-wot's own examples
scripts\conformance.ps1 -Mode capture                     # rewrite the fixtures from the reference
```

`diff` and `capture` need Rust (`cargo`); the reference server is built from `conformance/reference-rs`, and teta-wot's examples from a teta-wot v0.1.0 checkout next to this repository (`..\teta-wot`, or `-TetaWot`).

If PowerShell refuses to run the scripts, allow local scripts once with `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`, or run one with `powershell -ExecutionPolicy Bypass -File scripts\build.ps1`.
