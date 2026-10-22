# device-actor

**Concept:** A synchronous driver on its own thread, behind a mailbox.

**Technologies:** `Device<D>`: `call` from many threads, `post` (a future), a call timeout, `DeviceFault` (a panic) faulting the device, `reset`, an abort handle, `device_thread_name`. The driver isn't thread-safe: it is created, used and destroyed on its thread. The C++ port of teta-wot's `device-actor`.

## Run it

```
cmake --build --preset linux-gcc --target example-device-actor
./build/linux-gcc/examples/device-actor/example-device-actor
```

It prints the same lines as the Rust example (Rust's blocking caller on a plain thread is a queued `post` here: in C++ every call blocks its caller).

## What to look for

- 20 concurrent callers make 20 commands, one at a time, on one thread.
- The stuck call times out after 200 ms; the abort releases the driver, which answers again.
- After the panic, calls fail at once (`faulted ... reset it`) until `reset` recreates the driver.
