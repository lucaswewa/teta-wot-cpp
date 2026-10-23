# microscope-service

**Concept:** a Thing server as a service: the [simulated microscope](../simulated-microscope/README.md) installed, started and stopped by the Windows Service Control Manager, or on Linux by systemd, stopping gracefully when the service stops.

**Technologies:**

- `tw::service` (`run_as_service`, `install`, `uninstall`) on Windows;
- `tw::systemd` (`unit_file`, and the `READY=1` and `STOPPING=1` notifications the server sends itself) on Linux;
- `tw::cli::serve_until` with the service's stop signal, and `tw::cli::arguments`;
- the log written to a file (Windows) or to the journal (Linux);
- PowerShell's `Start-Service` and `Stop-Service`; `systemctl` and `journalctl`.

The C++ port of teta-wot's `microscope-service`, which is Windows-only; the systemd unit is this project's own (plan §7).

## Run it

```
cmake --build --preset windows-msvc --target example-microscope-service
build\windows-msvc\examples\microscope-service\example-microscope-service.exe
```

Without arguments, it demonstrates the stop path without the Service Control Manager or systemd (and without an administrator):

1. it serves the microscope as the service would;
2. it starts a long move;
3. it raises the same stop signal that the service's *stop* request raises;
4. it checks that the server stopped cleanly, cancelling the move rather than waiting for it.

As a Windows service, from an administrator console:

```
build\windows-msvc\examples\microscope-service\example-microscope-service.exe install --port 5000
Start-Service teta-wot-microscope
# … use http://127.0.0.1:5000/ …
Stop-Service teta-wot-microscope
build\windows-msvc\examples\microscope-service\example-microscope-service.exe uninstall
```

`install` writes the service's configuration (`microscope-service.json`, with an absolute settings folder) next to the executable, and the service logs to `microscope-service.log` there. `console -c FILE` runs the same server in a console.

As a systemd service:

```
sudo build/linux-gcc/examples/microscope-service/example-microscope-service install --port 5000 --user "$USER"
sudo systemctl start teta-wot-microscope      # returns once the server is ready (Type=notify)
journalctl -u teta-wot-microscope
sudo systemctl stop teta-wot-microscope
sudo build/linux-gcc/examples/microscope-service/example-microscope-service uninstall
```

There `install` writes `/etc/systemd/system/teta-wot-microscope.service` (`Type=notify`, `TimeoutStopSec=20`) and reloads systemd.

`service_test.ps1` and `service_test.sh` do the whole cycle and check it (CI runs them, since GitHub's runners are administrators and the Linux ones run systemd):

```
powershell -ExecutionPolicy Bypass -File examples\microscope-service\service_test.ps1
examples/microscope-service/service_test.sh
```

## What to look for

- The service stops as a console server stops on Ctrl-C: invocations are cancelled, requests finish within the grace period (5 s here), and the Things stop and save their settings. The long move is cancelled, not waited for.
- On Windows the service reports *stop pending* with a wait hint (20 s) while it stops, and *stopped* with the server's exit code. On Linux, systemd's *stop* is SIGTERM, and the server sends `STOPPING=1`.
- A Windows service starts in `C:\Windows\System32` with no console: every path in its configuration is absolute, and it logs to a file.
