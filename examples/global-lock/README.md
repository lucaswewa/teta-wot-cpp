# global-lock

**Concept:** The optional global lock: actions and client writes one at a time.

**Technologies:** `RuntimeBuilder::global_lock`, `GlobalLockBusy` (HTTP 409), per-invocation reentrancy (`ActionCtx::hold_global_lock` inside an action that already holds it), `.global_lock(false)`. The C++ port of teta-wot's `global-lock`.

## Run it

```
cmake --build --preset linux-gcc --target example-global-lock
./build/linux-gcc/examples/global-lock/example-global-lock
```

It prints the same lines as the Rust example.

## What to look for

- While `acquire` runs, `autofocus` ends `error` with `GlobalLockBusyError` (409) and the busy line in its log, and a property write is refused.
- `status` opted out of the lock and runs; `acquire` takes the lock a second time without blocking itself.
