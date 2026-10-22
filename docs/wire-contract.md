# Wire contract

What a client sees of a server, which teta-wot-cpp must reproduce exactly as [teta-wot v0.1.0](https://github.com/lucaswewa/teta-wot/tree/v0.1.0) does ([ADR-0001](adr/0001-parity-reference.md)). This document lists it by area, with where teta-wot implements it, and records each confirmed fact's fixture as phases capture them. Where this document and the reference disagree, the reference wins and this document is corrected.

**Status column:** *fixture* means a scenario in `conformance/scenarios/` captures it and CI checks the fixture against the reference; *source* means it is taken from teta-wot's code and documentation, and gets a fixture in the phase shown.

## 1. What is compared, and what isn't

Compared, after normalisation ([ADR-0011](adr/0011-verification.md)):

- the status code;
- the headers clients rely on: `content-type`, `location`, `allow` (methods sorted), `access-control-*`, `vary`, `cache-control`, `www-authenticate`, `link`, `etag`;
- the body, both parsed and as exact text (number formatting, key order, escaping), with UUIDs numbered by first appearance, timestamps masked, a fresh invocation's run state masked and log record locations masked;
- settings files byte for byte, and the settings warnings' text (Phase 5);
- the command line's exit codes and output (log lines aside), and the fallback page (Phase 5);
- WebSocket messages, how the server closes a socket, and server-sent event fields (Phase 6);
- Blob and MJPEG bytes (Phase 7).

Not compared, because hyper and Drogon can't be made to agree and clients don't depend on them (Phase 0 spike, [phase-00 note](notes/phase-00-baseline.md)):

- header order and the case of header names;
- the transport headers `date`, `content-length`, `connection`, `transfer-encoding`, and `connection: close` on HTTP/1.0 answers;
- the headers of a `100 Continue` interim answer;
- the headers of a 400 for a request the HTTP parser rejects (malformed request line or headers); the status is the same;
- how a connection opened during shutdown fails (teta-wot refuses it; Drogon accepts and closes it);
- the headers of a WebSocket handshake's 101 (Drogon adds `content-type` and no `vary`), WebSocket pings (Drogon pings every 30 s; teta-wot doesn't), and what happens after a client's Close frame (neither server answers it with one).

Every request in the scenarios carries `Host: testserver`, so absolute URLs are comparable.

## 2. Contract by area

| Area | The contract | teta-wot source | Status |
|---|---|---|---|
| DataSchemas | What `DataSchema::for_type` makes of each type: schemars' schema converted (refs inlined, `anyOf` → `oneOf`, type arrays split, numeric formats dropped, keys sorted) | `teta-wot-td/src/convert.rs`, schemars 1.2.2 | corpus, `tests/td` (Phase 1): 54 types |
| JSON text | serde_json 1.0.151 with `preserve_order`: key order kept; doubles shortest round-trip, decimal for decimal exponents −5…15 (`1.0`, `0.00001`), scientific otherwise with a signed exponent (`1e+16`, `1e-7`); only `"`, `\`, control characters escaped | serde_json | corpus, `tests/json` (Phase 0) |
| Routes | The route table: TD at `{prefix}/{thing}/`; properties, actions, events, `properties`, `actions`, `events`, `ws`, `action_invocations[/{id}[/output]]`, `things/`, `thing_descriptions/`, `blob/{id}`, streams and `…/viewer`, custom endpoints, `/.well-known/wot`, `{prefix}/directory/things[/{id}]`, `/openapi.json`, `/docs`, `/docs/oauth2-redirect`, `/redoc`; the first route matching path and method answers; paths are compared as sent (not percent-decoded) | `teta-wot-http/src/lib.rs`, `routes.rs` | fixtures `counter`, `default`, `prefixed` (Phase 4: everything but Blobs, streams, discovery, OpenAPI and docs, which come in Phases 7 to 9) |
| Reserved Thing names | `things`, `thing_descriptions`, `action_invocations`, `blob`, `docs`, `redoc`, `directory` | `routes.rs` | `tests/http` (Phase 4) |
| Redirects | Starlette's trailing-slash redirects: a path that isn't a route, but is one with a trailing slash added (or every trailing slash removed), answers 307 with the absolute URL in `location`, the query kept | `routes.rs`, `handlers.rs` | fixtures `counter`, `default`, `prefixed` (Phase 4) |
| 404, 405 | `{"detail": "Not Found"}` as `application/json`; 405 `{"detail": "Method Not Allowed"}` with `allow` naming the method of the first route with that path (`GET, HEAD` for the docs and discovery routes); `HEAD` is a method like the others (a 405 on a `GET` route), answered only on the docs and discovery routes | `handlers.rs`, `routes.rs` | fixtures `counter`, `default` (Phase 4) |
| CORS | Every answer `vary: Origin`; a request's `Origin` mirrored with credentials allowed; a preflight (`OPTIONS` with `Origin` and `Access-Control-Request-Method`) on any path answered 200 `OK` (`text/plain; charset=utf-8`) with every method, the requested headers, max age 600 and Starlette's `vary` list; an `OPTIONS` without both is routed | `cors.rs` | fixtures `counter`, `default`, `prefixed` (Phase 4) |
| Request bodies | Up to 64 MiB (more is 413 `{"detail": "length limit exceeded"}`); parsed as serde_json 1.0.151 does, without `float_roundtrip` (`-0` a float, integers beyond 64 bits floats, its own decimal arithmetic, `1e400` an error, a repeated key keeps its place and takes the last value, 128 levels of nesting); a `null` or empty body is `missing`; malformed JSON is `json_invalid` with serde_json's message and `loc` `["body", <offset>]` (the offset counts characters on earlier lines and bytes on the last) | axum, `handlers.rs` | corpus `tests/http/wire_corpus.jsonl` (8,559 bodies); fixtures `validation`, `default`, `wot` (Phase 4) |
| Property read/write | `GET` the value; `PUT` writes: `tetathing` 201 and `null`, `wot` 204; read-only `PUT` 405; reset `POST …/reset` (200 and `null`, `wot` 204) on resettable writable properties; `GET`/`PUT {prefix}/{thing}/properties` reads all (sorted) or writes several, every value checked first | `handlers.rs` | fixtures `counter`, `default`, `locked`, `wot` (Phase 4) |
| Validation errors | `tetathing`: 422 with FastAPI's list of pydantic errors (`type`, `loc`, `msg`, `input`, `ctx`); malformed IDs in paths 422 `uuid_parsing` with the `uuid` crate's message; `wot`: 400 problem with `invalid-params` | `teta-wot-core/src/validate.rs`, `problem.rs` | corpus, `tests/validate` (Phase 2): 8,413 values; corpus `tests/http/wire_corpus.jsonl` (38 IDs); fixtures `validation`, `default`, `wot` over HTTP (Phase 4) |
| Other errors | `tetathing`: `{"detail": …}` (and the framework's problem details) as `application/json`; `wot` and discovery: RFC 9457 problems as `application/problem+json`, types under `docs/problems.md` | `problem.rs` | fixtures `default`, `locked`, `wot` (Phase 4); discovery Phase 9 |
| Invocations | `POST` answers 201 with `location` and the invocation; `status` `pending` → `running` → `completed`/`error`/`cancelled`; `timeRequested`, `timeStarted`, `timeCompleted` as local naive times with microseconds; `links` `self` and `output`; `input`, `output`, `log`, `error`; lists without the last four; `{thing}/actions` newest first; `/output` 503 (`wot` 404) without one; `DELETE` cancels (`tetathing` 200 `null`, `wot` 204), then 503 (`wot` 409); retention 5 minutes; in `wot`, `ActionStatus` objects with `failed` and `timeEnded` in RFC 3339 UTC | `render.rs`, `handlers.rs`, `teta-wot-core/src/invocation.rs` | fixtures `counter`, `default`, `locked`, `prefixed`, `wot` (Phase 4); the runtime's life cycle, retention and problems: `tests/core` (Phase 3) |
| Invocation logs | Records `{message, levelname, levelno, lineno, filename, created, exception_type, traceback}`, Python's level names and numbers (`TRACE` 5), `created` UTC with microseconds and `Z`; the last 1000 kept | `teta-wot-core/src/logs.rs` | `tests/core` (Phase 3); fixtures `default`, `locked` (Phase 4) |
| Global lock | Busy: wait 50 ms, then `GlobalLockBusyError` (409 for writes and resets; an invocation ends in `error` with a "Global lock was busy" line in its log); reads and opted-out properties don't wait; reentrant per invocation, through in-process calls and child invocations | `teta-wot-core` | `tests/core` (Phase 3); fixture `locked` (Phase 4) |
| Thing Description | TD 1.1, valid against the W3C schema; `id` `urn:uuid:` v5 of `{server_id}/{thing}` in the namespace v5(URL, `https://github.com/lucaswewa/wot#td-id`) (`conformance/counter` → `urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd`); title, description from doc comments; forms for observation (SSE and WebSocket), events, top-level forms, invocation query and cancel forms, stream links; `profile` and `synchronous` in `wot` | `teta-wot-td`, `handlers.rs` (`served_td`) | fixtures `tds`, `tds-wot`: seven twins' TDs reproduced byte for byte in both profiles, from their C++ definitions (Phase 1, Phase 3); `blob_probe` Phase 7, `ndarray_probe` Phase 10 |
| Observation | WebSocket at `{prefix}/{thing}/ws` (one broker subscription per connection): `addPropertyObservation`, `addActionObservation`, `addEventSubscription`; `propertyStatus`, `actionStatus` (`"action name"`), `event` (`data` and a UTC `timestamp` in microseconds); 404/403 error responses, after which the rest of the message is skipped; other messages ignored, bad ones logged; closed 1001 `the server is shutting down` at shutdown, after what was buffered; plain `GET` 404, as is an upgrade for another path; a slow client misses what doesn't fit its buffer of 5. SSE (`text/event-stream`, `cache-control: no-cache`): `data:` lines (`tetathing`), named with an `id` (`wot`, and the "all" streams), `:` after 15 s of quiet | `observe.rs` | fixtures `ws`, `sse`, `sse-wot`, `events`, and the examples `observe-websocket`, `events`, `sse-observe` (Phase 6) |
| Blobs, MJPEG | `GET {prefix}/blob/{id}` with the media type (`charset=utf-8` added to `text/*`); `/output` serves or redirects; hrefs made absolute; MJPEG `multipart/x-mixed-replace` and the viewer page | `output.rs` | Phase 7 |
| Settings files | `{folder}/{thing}/Settings-{Class}.json` (the class name is the Thing's title unless set); every setting, keys sorted, nested values in their own order; indented four spaces (serde_json `PrettyFormatter`), no trailing newline, CRLF on Windows, LF elsewhere; written atomically (`.tmp`, then a rename) after every change once loaded, never before; a folder per Thing; loaded before any Thing starts, each value as a client's write; a missing file leaves the defaults; an invalid value, an unknown key, a file that isn't an object or isn't JSON only warns (teta-wot's messages), and an unknown key goes at the next save | `teta-wot-core/src/settings.rs` | fixtures `settings`, `settings-load` (Phase 5): files, loading, warnings, restarts |
| Configuration file | LabThings' `ThingServerConfig`: `things` (a class name, or `cls`/`class`, `args`, `kwargs`, `thing_slots`), `settings_folder` (`./settings`), `api_prefix`, `enable_global_lock` (pydantic's lax booleans), `global_lock_log_level`, `application_config`; teta-wot's `server_id`, `wire_profile`, `security`, `mdns`; unknown keys warn; every problem reported at once in pydantic's words and locations; class names compared with `:` and `::` as `.` | `teta-wot-server/src/config.rs`, `registry.rs` | fixtures `cli` (Phase 5) |
| OpenAPI, docs | OpenAPI 3.1 at `/openapi.json` with teta-wot's `operationId`s; Swagger UI 5.33.0 and ReDoc 2.5.4 pages | `openapi.rs`, `docs.rs` | Phase 8 |
| Discovery | `/.well-known/wot` (`application/td+json`) at the root; `{prefix}/directory/things` sorted by `id` (`application/ld+json`), `offset`, `limit`, `format=collection`, `next` and `canonical` links, `etag`; `{prefix}/directory/things/{id}`; `HEAD`; 405 for writes; errors always problems | `discovery.rs` | Phase 9 |
| Security | Basic or bearer on every interaction, `401` with `WWW-Authenticate`; TDs, directory, OpenAPI and docs public; `securityDefinitions` in TDs | `security.rs` | Phase 9 |
| Command line | clap's: `-c`, `-j`, `--host`, `--port` (127.0.0.1:5000), `--fallback`, `--debug`, `-h`, its help and error messages; exit codes 0 (stopped), 2 (command line), 3 (configuration, a Thing that can't start, the fallback served), 1 (anything else); `listening on http://…` once bound, before the Things start; the fallback page at `/` (500), `true` at `/fallback`, a 307 to `/` elsewhere, with the binding's CORS | `teta-wot-server/src/cli.rs`, `teta-wot-http/src/fallback.rs` | fixtures `cli` (Phase 5): 21 scenarios |

## 3. Deltas

Differences from teta-wot v0.1.0 that are accepted, each with its exact expected answer. On the wire, they are Drogon's, answered before the binding sees the request (Phase 4 note).

| Request | teta-wot | teta-wot-cpp |
|---|---|---|
| A method Drogon's parser doesn't know: anything but `GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS`, `PATCH`, `PROPFIND`, `MKCOL`, `COPY`, `MOVE` (such as `QUERY` or `TRACE`) | routed: 405 with `allow`, or 404 | 405, no body, no headers but the transport's |
| A target whose query is empty (`GET /counter?`) | 307 to `…/counter/?` | 307 to `…/counter/` (the `?` is lost) |
| A body over 128 MiB | 413 `{"detail": "length limit exceeded"}` | 413, no body, `connection: close` |

On the command line (Phase 5 note), where `run.py --cli` masks the first two:

| What | teta-wot | teta-wot-cpp |
|---|---|---|
| A Thing given both `args` and `kwargs` | `use either args or kwargs, not both: a Rust Thing's configuration is one typed value` | `… a C++ Thing's configuration is one typed value` |
| The lists of registered names in configuration errors | the twin registry's, with `blob_probe` and `ndarray_probe` | without them until Phases 7 and 10 |
| A missing service | `` Thing `camera` needs the service `dependency_injection::DataStore` … `` (the module path) | `` … the service `DataStore` … `` (the short name) |
| A configuration with `security` | served with Basic or bearer authentication | refused (exit code 3) until the binding supports security (Phase 9) |
| `"mdns": true` | advertised (with the `mdns` feature) | a warning: teta-wot-cpp doesn't advertise until Phase 9 |
| Log lines | `tracing`'s format, on standard output | teta-wot-cpp's format, on standard error (log lines aren't contract) |
