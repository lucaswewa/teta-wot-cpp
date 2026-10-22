# Wire contract

What a client sees of a server, which teta-wot-cpp must reproduce exactly as [teta-wot v0.1.0](https://github.com/lucaswewa/teta-wot/tree/v0.1.0) does ([ADR-0001](adr/0001-parity-reference.md)). This document lists it by area, with where teta-wot implements it, and records each confirmed fact's fixture as phases capture them. Where this document and the reference disagree, the reference wins and this document is corrected.

**Status column:** *fixture* means a scenario in `conformance/scenarios/` captures it and CI checks the fixture against the reference; *source* means it is taken from teta-wot's code and documentation, and gets a fixture in the phase shown.

## 1. What is compared, and what isn't

Compared, after normalisation ([ADR-0011](adr/0011-verification.md)):

- the status code;
- the headers clients rely on: `content-type`, `location`, `allow` (methods sorted), `access-control-*`, `vary`, `cache-control`, `www-authenticate`, `link`, `etag`;
- the body, both parsed and as exact text (number formatting, key order, escaping), with UUIDs numbered by first appearance, timestamps masked, a fresh invocation's run state masked and log record locations masked;
- WebSocket messages and server-sent event fields (Phase 6), Blob and MJPEG bytes (Phase 7), settings files byte for byte (Phase 5).

Not compared, because hyper and Drogon can't be made to agree and clients don't depend on them (Phase 0 spike, [phase-00 note](notes/phase-00-baseline.md)):

- header order and the case of header names;
- the transport headers `date`, `content-length`, `connection`, `transfer-encoding`, and `connection: close` on HTTP/1.0 answers;
- the headers of a `100 Continue` interim answer;
- the headers of a 400 for a request the HTTP parser rejects (malformed request line or headers); the status is the same;
- how a connection opened during shutdown fails (teta-wot refuses it; Drogon accepts and closes it).

Every request in the scenarios carries `Host: testserver`, so absolute URLs are comparable.

## 2. Contract by area

| Area | The contract | teta-wot source | Status |
|---|---|---|---|
| DataSchemas | What `DataSchema::for_type` makes of each type: schemars' schema converted (refs inlined, `anyOf` → `oneOf`, type arrays split, numeric formats dropped, keys sorted) | `teta-wot-td/src/convert.rs`, schemars 1.2.2 | corpus, `tests/td` (Phase 1): 54 types |
| JSON text | serde_json 1.0.151 with `preserve_order`: key order kept; doubles shortest round-trip, decimal for decimal exponents −5…15 (`1.0`, `0.00001`), scientific otherwise with a signed exponent (`1e+16`, `1e-7`); only `"`, `\`, control characters escaped | serde_json | corpus, `tests/json` (Phase 0) |
| Routes | The route table: TD at `{prefix}/{thing}/`; properties, actions, events, `properties`, `actions`, `events`, `ws`, `action_invocations[/{id}[/output]]`, `things/`, `thing_descriptions/`, `blob/{id}`, streams and `…/viewer`, custom endpoints, `/.well-known/wot`, `{prefix}/directory/things[/{id}]`, `/openapi.json`, `/docs`, `/docs/oauth2-redirect`, `/redoc` | `teta-wot-http/src/lib.rs`, `routes.rs` | fixture `counter` (TD, property, action, invocations, `things/`); the rest Phase 4 |
| Reserved Thing names | `things`, `thing_descriptions`, `action_invocations`, `blob`, `docs`, `redoc`, `directory` | `routes.rs` | Phase 4 |
| Redirects | Starlette's trailing-slash redirects (`/counter` → `/counter/`) | `routes.rs`, `handlers.rs` | fixture `counter` (`td_without_slash`) |
| 404, 405 | `{"detail": "Not Found"}` as `application/json`; 405 with `allow` (routes answering several methods list them); `HEAD` answered on `GET` routes | `handlers.rs`, `routes.rs` | fixture `counter` (`not_found`, `write_readonly`); `allow` lists Phase 4 |
| CORS | Every answer `vary: Origin`; a request's `Origin` mirrored with credentials allowed; a preflight (`OPTIONS` with `Origin` and `Access-Control-Request-Method`) on any path answered 200 `OK` (`text/plain; charset=utf-8`) with every method, the requested headers, max age 600 and Starlette's `vary` list | `cors.rs` | fixture `counter` (`cors_preflight`) |
| Request bodies | No size limit; parsed as serde_json does (`-0` a float, `1e400` an error, last duplicate key wins); a `null` or empty body is `missing`; malformed JSON is `json_invalid` with serde_json's message and `loc` `["body", <offset>]` | axum, `handlers.rs` | Phase 0 probe (64 MiB accepted); fixture `validation` (`body_*`, `invoke_not_json`); replay Phase 4 |
| Property read/write | `GET` the value; `PUT` writes: `tetathing` 201 (or 200) and `null`, `wot` 204; read-only `PUT` 405; reset `POST …/reset` | `handlers.rs` | fixture `counter` (read, read-only write); the rest Phase 4 |
| Validation errors | `tetathing`: 422 with FastAPI's list of pydantic errors (`type`, `loc`, `msg`, `input`, `ctx`); `wot`: 400 problem with `invalid-params` | `teta-wot-core/src/validate.rs`, `problem.rs` | corpus, `tests/validate` (Phase 2): 8,413 values; fixture `validation` (the reference's 422s, reproduced by the validator); HTTP replay Phase 4, `wot` Phase 9 |
| Other errors | `tetathing`: `{"detail": …}` (and the framework's problem details) as `application/json`; `wot` and discovery: RFC 9457 problems as `application/problem+json`, types under `docs/problems.md` | `problem.rs` | Phase 4, Phase 9 |
| Invocations | `POST` answers 201 with `location` and the invocation; `status` `pending` → `running` → `completed`/`error`/`cancelled`; `timeRequested`, `timeStarted`, `timeCompleted` as local naive times with microseconds; `links` `self` and `output`; `input`, `output`, `log`, `error`; `DELETE` cancels (`tetathing` 200 `null`, `wot` 204); retention 5 minutes; in `wot`, `ActionStatus` objects with `failed` and `timeEnded` in RFC 3339 UTC | `render.rs`, `handlers.rs`, `teta-wot-core/src/invocation.rs` | fixture `counter` (`invoke`, `get_invocation`, `list_action_invocations`); the rest Phase 3–4, Phase 9 |
| Invocation logs | Records `{message, levelname, levelno, lineno, filename, created, exception_type, traceback}`, Python's level names and numbers (`TRACE` 5), `created` UTC with microseconds and `Z`; the last 1000 kept | `teta-wot-core/src/logs.rs` | Phase 3 |
| Global lock | Busy: wait 50 ms, then `GlobalLockBusyError`; reentrant through in-process calls | `teta-wot-core` | Phase 3–4 |
| Thing Description | TD 1.1, valid against the W3C schema; `id` `urn:uuid:` v5 of `{server_id}/{thing}` in the namespace v5(URL, `https://github.com/lucaswewa/wot#td-id`) (`conformance/counter` → `urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd`); title, description from doc comments; forms for observation (SSE and WebSocket), events, top-level forms, invocation query and cancel forms, stream links; `profile` and `synchronous` in `wot` | `teta-wot-td`, `handlers.rs` (`served_td`) | fixtures `tds`, `tds-wot`: seven twins' TDs reproduced byte for byte in both profiles (Phase 1); `blob_probe` Phase 7, `ndarray_probe` Phase 10 |
| Observation | WebSocket at `{prefix}/{thing}/ws`: `addPropertyObservation`, `addActionObservation`, `addEventSubscription`; `propertyStatus`, `actionStatus`, `event`; 404/403 error messages; others ignored; plain `GET` 404. SSE: `data:` lines (`tetathing`), named with an `id` (`wot`) | `observe.rs` | Phase 6 |
| Blobs, MJPEG | `GET {prefix}/blob/{id}` with the media type (`charset=utf-8` added to `text/*`); `/output` serves or redirects; hrefs made absolute; MJPEG `multipart/x-mixed-replace` and the viewer page | `output.rs` | Phase 7 |
| Settings files | `{folder}/{thing}/Settings-{Type}.json`, indented four spaces (serde_json `PrettyFormatter`), CRLF on Windows, LF elsewhere, written atomically | `teta-wot-core/src/settings.rs` | corpus (indentation, Phase 0); files Phase 5 |
| OpenAPI, docs | OpenAPI 3.1 at `/openapi.json` with teta-wot's `operationId`s; Swagger UI 5.33.0 and ReDoc 2.5.4 pages | `openapi.rs`, `docs.rs` | Phase 8 |
| Discovery | `/.well-known/wot` (`application/td+json`) at the root; `{prefix}/directory/things` sorted by `id` (`application/ld+json`), `offset`, `limit`, `format=collection`, `next` and `canonical` links, `etag`; `{prefix}/directory/things/{id}`; `HEAD`; 405 for writes; errors always problems | `discovery.rs` | Phase 9 |
| Security | Basic or bearer on every interaction, `401` with `WWW-Authenticate`; TDs, directory, OpenAPI and docs public; `securityDefinitions` in TDs | `security.rs` | Phase 9 |
| Command line | `-c`, `-j`, `--host`, `--port` (127.0.0.1:5000), `--fallback`, `--debug`; exit codes 0, 1, 2, 3; the fallback page | `teta-wot-server/src/cli.rs`, `fallback.rs` | Phase 5 |

## 3. Deltas

Differences from teta-wot v0.1.0 that are accepted, each with its exact expected answer. None yet: the transport-level differences in §1 are outside the contract rather than deltas.
