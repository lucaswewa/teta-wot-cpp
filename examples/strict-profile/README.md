# strict-profile

**Concept:** the two wire profiles.

- **`tetathing`**, the default behaviour, which teta-wot's and LabThings' clients depend on.
- **`wot`** follows the W3C WoT HTTP Basic and SSE Profiles where the two conflict.

One Thing is served in both, and the same requests go to each.

**Technologies:** `ThingServerBuilder::wire_profile(tw::http::WireProfile::wot)` (or `"wire_profile": "wot"` in a configuration file); `.synchronous()` on an action; `tw::testing::TestClient`, two servers in one process. The C++ port of teta-wot's `strict-profile`.

## Run it

```
cmake --build --preset linux-gcc --target example-strict-profile
./build/linux-gcc/examples/strict-profile/example-strict-profile
```

It prints each request with both answers (status, content type, body), and exits with a non-zero status if they don't differ as described below. No network is used.

## What to look for

- Writes: 201 and `null`, or 204. Invalid values: FastAPI's 422, or a 400 problem with `invalid-params`.
- Invocations: teta-wot's invocation (naive local times), or the Profile's `ActionStatus` (RFC 3339 UTC, `timeEnded`); a cancelled one ends `cancelled`, or `failed` with a problem.
- A synchronous action answers 200 and its output only in `wot`; the `wot` TD names the Profiles and marks the action `synchronous`.
- The top-level resources (`/lamp/properties`) answer alike in both.
