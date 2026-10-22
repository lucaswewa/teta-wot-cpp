# graceful-shutdown

**Concept:** stopping a server without leaving hardware in a bad state. A stop request cancels running actions cooperatively, gives them time to finish, then stops the Things and closes their devices, in reverse order of starting.

**Technologies:** `ThingServer::serve` with a stop token, `tw::shutdown_signal()` (Ctrl-C, Ctrl-Break, closing the console window, logging off and shutting down on Windows; SIGINT and SIGTERM elsewhere), `ThingServerBuilder::shutdown_grace`, `tw::Device`, and `on_start`/`on_stop` hooks. The C++ port of teta-wot's `graceful-shutdown`.

## Run it

```
cmake --build --preset linux-gcc --target example-graceful-shutdown
./build/linux-gcc/examples/graceful-shutdown/example-graceful-shutdown          # stops itself after about 500 ms
./build/linux-gcc/examples/graceful-shutdown/example-graceful-shutdown --wait   # waits for you to press Ctrl-C
```

It prints a timestamped line for each lifecycle event, and exits with a non-zero status if the running action wasn't cancelled. A typical run:

```
[    5 ms] building the server
[    8 ms] left: shutter opened (on "device:left:shutter")
[    8 ms] left: started
[    8 ms] right: shutter opened (on "device:right:shutter")
[    8 ms] right: started
[   80 ms] serving on http://127.0.0.1:64614
[   80 ms] left: time lapse started
[  581 ms] shutdown requested
[  581 ms] left: time lapse cancelled after 4 frames; finishing the file
[  590 ms] right: stopping
[  590 ms] right: shutter closed
[  590 ms] left: stopping
[  590 ms] left: shutter closed
[  590 ms] left's time lapse ended as `cancelled`
```

## What to look for

- The action sees the cancellation at its next `ctx.sleep` and can finish its work before ending.
- `right` started last, so it stops first; each Thing's `on_stop` runs before its devices close.
- With `--wait`, closing the console window also stops the server cleanly: Windows waits for the handler while the shutdown runs.
