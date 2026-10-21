# ADR-0011: Verification: parity with teta-wot is the gate

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D11

## Context

Parity has to be tested, not argued. The reference (ADR-0001) can be run, so its answers can be captured, and compared with the C++ server's.

## Decision

- **Golden fixtures from the reference.** `conformance/run.py capture` runs each scenario in `conformance/scenarios/` against the reference server over real sockets and writes `conformance/fixtures/`. CI re-captures them and fails on any change, so they can't drift from the reference.
- **Replay.** `conformance/run.py check` (and, from Phase 4, GoogleTest suites through `TestClient`) compares a server's answers with the fixtures.
- **Differential testing.** `conformance/run.py diff` runs a scenario against two servers and compares their answers; from Phase 4, the reference and the C++ server, and a randomised mode generates more cases.
- **Normalisation** follows the `wot` repository's fixtures: UUIDs numbered by first appearance, timestamps masked, a fresh invocation's run state masked (`<race>`), log record locations masked, only the headers clients rely on kept, `allow` sorted. Every request carries `Host: testserver`. Bodies are compared as normalised **text** as well as parsed, so number formatting, key order and escaping are part of the comparison.
- LabThings' Python `ThingClient` is not a gate.

## Consequences

- Header order and transport headers aren't compared (ADR-0004).
- The harness is plain Python 3 (standard library only), so it runs anywhere CI runs.
