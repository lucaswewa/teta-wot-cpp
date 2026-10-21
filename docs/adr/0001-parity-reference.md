# ADR-0001: Parity reference: teta-wot v0.1.0

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D1

## Context

teta-wot-cpp exists to be a C++ implementation of [teta-wot](https://github.com/lucaswewa/teta-wot) that clients can't tell apart from it ([goals](../goals.md): functionality parity, wire-level parity). Parity needs one fixed thing to be at parity with. teta-wot's `tetathing` profile descends from LabThings-FastAPI v0.3.0, and the `wot` repository holds fixtures captured from LabThings, so LabThings could also have been a reference.

## Decision

- **teta-wot at the `v0.1.0` tag (commit `135f9ff`) is the reference, and the only one.** What it does on the wire is the contract (§3 of the plan, `docs/wire-contract.md`), whatever LabThings does.
- The reference is built from source in `conformance/reference-rs/`, **with teta-wot v0.1.0's own `Cargo.lock`**, so every dependency that shapes the wire (serde_json, hyper, axum, schemars) is the version teta-wot released with.
- A difference found by a test is a C++ bug, unless it is a bug in teta-wot v0.1.0. That is reported upstream and recorded in `docs/wire-contract.md` as a delta with its exact expected answer, until a later teta-wot release that fixes it is adopted.
- Moving to a later teta-wot release is a deliberate change: the tag, the lock file and every fixture move together.

## Consequences

- LabThings' Python `ThingClient` and the `wot` repository's LabThings fixtures aren't gates (ADR-0011).
- The lock file matters: Phase 0 found that serde_json 1.0.151 writes `1e16` as `1e+16`; a reference built with a newer lock could have changed such details silently.
