# Wire profiles

A server chooses how it answers with its *wire profile*:

- **`tetathing`** (the default): teta-wot's answers, which are LabThings-FastAPI's;
- **`wot`** follows the W3C WoT HTTP Basic and SSE Profiles (Working Draft, November 2025).

Choose it in the configuration file (`"wire_profile": "wot"`) or the builder (`.wire_profile(tw::http::WireProfile::wot)`). It applies to the whole server.

## What differs

| | `tetathing` | `wot` |
|---|---|---|
| Writing a property, resetting | `201` (or `200`) and `null` | `204`, no body |
| Invalid input | `422`, FastAPI's list of pydantic errors | `400` problem, with `invalid-params` |
| Other errors | `{"detail": …}`, as `application/json` | RFC 9457 problems, as `application/problem+json` |
| An invocation | JSON: `error`, `cancelled`, `timeCompleted` in local time | An `ActionStatus`: `pending`, `running`, `completed` or `failed`; `timeEnded` in RFC 3339 UTC |
| Cancelling | `200`, `null` | `204` |
| A `.synchronous()` action | answered like any action (`201`) | `200` with the output (or `204`), once it has finished |
| Server-sent events on an affordance | `data:` only | named after the affordance, with an `id` |
| The TD | no `profile` | `profile` names the HTTP Basic and SSE Profiles; `synchronous` says which actions wait |

The problem types of the `wot` profile are teta-wot's, under its `docs/problems.md`.

## Which to choose

- **Clients written for LabThings or teta-wot,** including LabThings' Python client: `tetathing`.
- **Consumers written from the W3C specifications,** and new code that wants standard answers: `wot`.
- **Both at once:** two servers, on two ports, as the [`node-wot-consumer`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/node-wot-consumer) example does.

The [`strict-profile`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/strict-profile) example sends the same requests to both, and prints the answers side by side.
