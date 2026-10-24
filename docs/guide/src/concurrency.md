# Concurrency and hardware

teta-wot-cpp runs Thing code on threads: every request and every invocation runs on a worker thread of the runtime's pool, and many run at once (ADR-0005). Where teta-wot needs `async` and `blocking`, C++ code just blocks.

## What runs at once

- **Requests** are served concurrently: many clients read properties while actions run.
- **Actions** run on worker threads. Several invocations, of the same action or different ones, can run at the same time, unless the global lock stops them.
- **Thing code must therefore be safe to call concurrently.** `Prop<T>`, `Event<T>` and `MjpegStream` are; other state goes behind a `std::mutex` or in atomics.

## Blocking code

A driver call, a `std::this_thread::sleep_for` or a long computation is fine in an action or a property's getter: it holds one worker thread. A long wait should still be cancellable, so prefer `ctx.sleep`, or register a cancellation callback that interrupts the driver ([Actions](actions.md#cancellation)).

## Devices

A driver that must stay on one thread (many vendor SDKs, COM objects, anything not thread-safe) goes behind a device:

```cpp
class Laser {
  public:
    tw::Device<SerialInstrument> port{[] { return SerialInstrument("COM3"); }};

    std::string identify() {
        return port.call([](SerialInstrument& d) { return d.query("*IDN?"); });
    }

    static void define(tw::ThingSpec<Laser>& t) {
        t.device("port", &Laser::port);
        t.action("identify", &Laser::identify);
    }
};
```

The driver is created, used and destroyed on a thread of its own (`device:<thing>:<name>`). Callers queue calls to it:

- `call(f)` waits for the answer, from any thread;
- `post(f)` returns a `std::future`.

Other features:

- a call can time out (`DeviceOptions::call_timeout`), and the mailbox is bounded;
- an exception thrown as `tw::DeviceFault` (a panic, in teta-wot's terms) faults the device, and `reset()` recovers it;
- `abort()` interrupts a long call out of band, if the driver has an abort handle;
- the device opens before the Thing starts and closes after it stops.

See the [`device-actor`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/device-actor) example.

## The global lock

LabThings can serialise hardware access with a global lock (`enable_global_lock`):

- while an action runs, other actions and property writes wait briefly (50 ms) for the lock, then are refused with `GlobalLockBusyError`;
- the lock is reentrant, so an action's in-process calls to other Things hold it too.

teta-wot-cpp has the same lock, off by default as in LabThings. Actions and properties that don't touch hardware opt out with `.global_lock(false)`; an action that opted out can still hold it for a critical section with `ctx.hold_global_lock()`. See the [`global-lock`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/global-lock) example.

## Cancellation and shutdown

Long actions check for cancellation ([Actions](actions.md#cancellation)), so that a client's `DELETE` and the server's shutdown can stop them. On shutdown:

1. every invocation is cancelled;
2. the server waits up to its grace period for them, and for requests in progress;
3. it stops the Things in reverse order.

An action that ignores cancellation holds the shutdown for the whole grace period. The [`graceful-shutdown`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/graceful-shutdown) example, and the soak tool's shutdown-under-load test, show a server stopping cleanly while clients are at work.
