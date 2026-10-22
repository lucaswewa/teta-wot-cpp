# invocation-logs

**Concept:** Per-invocation logs.

**Technologies:** `tw::log` (`info`, `warn`, ...), capture into the running invocation's log (records in Python's shape), a device call logging into its caller's invocation, an error logged with its cause chain (`std::throw_with_nested`), and the **spdlog sink** (`teta/wot/log_spdlog.hpp`): the pump driver logs with spdlog, as a driver library might, and is captured too. The C++ port of teta-wot's `invocation-logs`.

## Run it

```
cmake --build --preset linux-gcc --target example-invocation-logs
./build/linux-gcc/examples/invocation-logs/example-invocation-logs
```

It prints the invocation's log as JSON, the same records as the Rust example (but for the times and line numbers), then the failing invocation's last record. Without spdlog (the vcpkg feature `spdlog`) the driver logs with `tw::log` instead.

## What to look for

- The DEBUG line isn't captured (capture is INFO and above, DEBUG in debug mode).
- The error's `exception_type` is `Error` (a standard exception, as an `anyhow::Error`) and its traceback lists the cause, as anyhow's does.
