# api-prefix

**Concept:** serving under a path prefix (`api_prefix`), for example behind a reverse proxy that routes `/api/v1` to the server. The example also shows how absolute URLs are built from each request.

**Technologies:** `ThingServerBuilder::api_prefix`, and `tw::testing::TestClient` with another `Host` (`with_host`). The C++ port of teta-wot's `api-prefix`.

## Run it

```
cmake --build --preset linux-gcc --target example-api-prefix
./build/linux-gcc/examples/api-prefix/example-api-prefix
```

It prints what the server writes under the prefix, the same lines as the Rust example, and exits with a non-zero status if a URL is wrong.

## What to look for

- Form `href`s are relative and carry the prefix; the TD's `base` is the server's root, built from the request's `Host`.
- The TD `id` doesn't depend on the prefix or the host: it is derived from the server ID and the Thing's name.
- Without the prefix, the routes don't exist (`404`).
