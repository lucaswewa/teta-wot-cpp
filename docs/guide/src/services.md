# Running as a service

An instrument server usually runs unattended: started with the computer, stopped cleanly when it shuts down. On Windows that is a *service*, run by the Service Control Manager (SCM); on Linux, a systemd unit. Either way the server stops as a console server stops on Ctrl-C:

1. invocations are cancelled;
2. requests finish, up to the grace period (`ThingServerBuilder::shutdown_grace`);
3. the Things stop, saving their settings and closing their devices.

The `microscope-service` example is the simulated microscope as both.

## A Windows service

`tw::service::run_as_service` connects the process to the SCM and runs the server, with the service's *stop* request as its stop signal:

```cpp
const auto args = tw::cli::arguments(argc, argv);  // UTF-8, the program's name first
try {
    tw::service::run_as_service("teta-wot-microscope", 20s, [&](auto, std::stop_token stop) {
        return tw::cli::serve_until(args, registry(), customise, stop);
    });
} catch (const tw::service::NotAService&) {
    // Not started by the SCM: run in a console instead.
}
```

- **Starting.** The service reports *running* once `serve` is called.
- **Stopping.** On *stop*, or when Windows shuts down, it reports *stop pending*, with the wait hint given (20 s here; it should cover the grace period and stopping the Things), and raises the stop signal.
- **The exit code.** When `serve` returns, the service reports *stopped* with its exit code.
- **Outside a service.** If the SCM didn't start the process, `run_as_service` throws `NotAService` at once, so one executable can also run in a console. A process can try only once: Windows takes one service dispatcher per process, so a second call throws `AlreadyRunning`.

`tw::service::install` registers an executable with its arguments (`InstallOptions`: name, display name, description, whether it starts with Windows), and `tw::service::uninstall` stops and removes it. Both need an administrator:

```text
example-microscope-service install --port 5000
Start-Service teta-wot-microscope
Stop-Service teta-wot-microscope
example-microscope-service uninstall
```

A service starts in `C:\Windows\System32`, as the LocalSystem account, with no console:

- **every path must be absolute:** configuration, settings folder, log;
- **logging goes to a file:** the example sends standard error, where the log is written, to one.

## A systemd unit

teta-wot has no Linux counterpart; teta-wot-cpp's is a `Type=notify` unit. systemd's *stop* is SIGTERM, which already stops a server (`shutdown_signal`), and the server tells systemd how it is doing with the notification protocol (`tw::systemd::notify`, without libsystemd): `READY=1` once it listens, so `systemctl start` returns when the server answers, and `STOPPING=1` when it starts shutting down. Started any other way, the server sends nothing.

`tw::systemd::unit_file` writes the unit:

```cpp
tw::systemd::UnitOptions unit;
unit.description = "teta-wot simulated microscope";
unit.executable = "/opt/microscope/microscope-service";
unit.arguments = {"run", "-c", "/opt/microscope/microscope.json", "--port", "5000"};
unit.user = "lab";
unit.stop_timeout = 20s;  // TimeoutStopSec: the grace period, and stopping the Things
std::ofstream("/etc/systemd/system/teta-wot-microscope.service") << tw::systemd::unit_file(unit);
```

```ini
[Unit]
Description=teta-wot simulated microscope
Wants=network-online.target
After=network-online.target

[Service]
Type=notify
ExecStart=/opt/microscope/microscope-service run -c /opt/microscope/microscope.json --port 5000
TimeoutStopSec=20
User=lab

[Install]
WantedBy=multi-user.target
```

The server logs to standard error, which systemd keeps in the journal (`journalctl -u teta-wot-microscope`).

## Testing it

The example's `service_test.ps1` and `service_test.sh` check the real thing, as an administrator (or with `sudo`): they install the service, start it and use it, stop it during a long move, check that the stop was graceful (exit code 0, the move cancelled in the log), and remove it. CI runs them.
