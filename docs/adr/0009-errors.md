# ADR-0009: Errors: exceptions or Expected

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D9

## Context

Rust actions return `Result`; teta-wot catches panics and the server carries on.

## Decision

- Thing code reports failures by **throwing** (`HandledError` for anticipated ones, logged without detail) or by returning **`Expected<T, ActionError>`**.
- Any exception escaping an action ends its invocation as `error`, and the server carries on.

## Consequences

- A crash (a segmentation fault in a driver) can't be caught in C++, unlike a Rust panic. This is a behavioural difference, not a wire one, and is documented; device actors keep driver code on known threads.

## Implementation (Phase 3)

- `ActionError::from(std::exception_ptr)` decides what an exception means for an invocation: `Cancelled` ends it `cancelled`; `HandledError` (or `ActionError::handled`) is an anticipated `InvocationError`; `GlobalLockBusy` is the 409 `GlobalLockBusyError`; any other `std::exception` is an error logged with its type and cause chain; anything else is a panic (`throw "message"` is Rust's `panic!("message")`).
- **The problem's title is the exception's type's short name**, as teta-wot's is the Rust error type's (`DeviceError`, `ValidationError`, a Thing's `MotorJammed`); the standard library's exceptions are `Error`, as `anyhow::Error` is. `std::throw_with_nested` builds the cause chain: the message is `a: b` (anyhow's `{:#}`), the traceback anyhow's `{:?}`.
- **`Expected<T>`** is `tl::expected<T, ActionError>` (vcpkg `tl-expected`, ADR-0002): an action may return one instead of throwing.
- Properties report failures as `PropertyError`, whose kinds and problems are teta-wot's; device calls as `DeviceError`. In a device call, a `std::exception` is a driver error and `DeviceFault` a panic that faults the device.
