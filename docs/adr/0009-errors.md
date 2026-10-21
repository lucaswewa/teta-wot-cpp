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
