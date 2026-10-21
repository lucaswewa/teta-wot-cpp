# Architecture decision records

Each records one decision: its context, what was decided, and what follows. Decisions D1–D13 of the [plan](../PLAN.md#0-decisions) are ADR-0001 to ADR-0013. New ADRs copy [`template.md`](template.md) and take the next number; a decision that changes is superseded by a new ADR, not edited away.

| ADR | Decision | Status |
|---|---|---|
| [ADR-0001](0001-parity-reference.md) | Parity reference: teta-wot v0.1.0 | Accepted |
| [ADR-0002](0002-language-level.md) | Language level: C++20 | Accepted |
| [ADR-0003](0003-build-and-packages.md) | Build and packages: CMake and vcpkg | Accepted |
| [ADR-0004](0004-drogon-is-a-transport-only.md) | Drogon is a transport only | Accepted |
| [ADR-0005](0005-concurrency.md) | Concurrency: actions on threads only | Accepted |
| [ADR-0006](0006-json.md) | JSON: ordered values, serde_json-exact text | Accepted |
| [ADR-0007](0007-types-to-schemas.md) | Types to schemas: Codec<T> | Accepted |
| [ADR-0008](0008-validation.md) | Validation: port teta-wot's SchemaValidator | Accepted |
| [ADR-0009](0009-errors.md) | Errors: exceptions or Expected | Accepted |
| [ADR-0010](0010-logging.md) | Logging: a facade with per-invocation capture | Accepted |
| [ADR-0011](0011-verification.md) | Verification: parity with teta-wot is the gate | Accepted |
| [ADR-0012](0012-naming.md) | Naming | Accepted |
| [ADR-0013](0013-licence.md) | Licence: MIT | Accepted |
