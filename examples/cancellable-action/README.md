# cancellable-action

**Concept:** Cooperative cancellation: actions notice it, may handle it, and pass it on to child invocations.

**Technologies:** `ActionCtx::sleep` (throws `Cancelled`), handling `Cancelled` to finish normally, `ActionCtx::spawn_child` and `ChildInvocation::join`, `InvocationManager::cancel` (what `DELETE /action_invocations/{id}` does) and its refusals. The C++ port of teta-wot's `cancellable-action`.

## Run it

```
cmake --build --preset linux-gcc --target example-cancellable-action
./build/linux-gcc/examples/cancellable-action/example-cancellable-action
```

It prints how each of three invocations ends, the same lines as the Rust example (the number of lines saved depends on timing).

## What to look for

- `scan` ends `cancelled` with an `InvocationCancelledError`; `scan_or_save` catches the cancellation and ends `completed`.
- Cancelling `batch` cancels its child; the parent ends `cancelled` once the child has stopped.
- Cancelling a finished invocation is refused with teta-wot's message.
