# ADR-0013: Licence: MIT

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D13

## Context

The repository is MIT licensed (unlike teta-wot, which has none).

## Decision

- teta-wot-cpp is **MIT** licensed, and can be packaged: a vcpkg overlay port is planned for Phase 10.
- Vendored third-party files keep their licences, listed in a third-party notices file from the phase that vendors the first one.

## Consequences

- Code ported from teta-wot is ported by its author's project; nothing is copied from LabThings beyond the twin Things' docstrings, which the `wot` repository already records as MIT.
