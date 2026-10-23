# bearer-security

**Concept:** a Thing that requires credentials, here a bearer token, with the requirement described where clients look for it.

**Technologies:**

- `ThingServerBuilder::security(tw::http::Security::bearer(token))`, or `Security::basic(user, password)`;
- in a configuration file, `"security": {"scheme": "bearer", "token_env": "WOT_TOKEN"}`: the variable that holds the token, never the token;
- TD `securityDefinitions`, and OpenAPI `securitySchemes`.

The C++ port of teta-wot's `bearer-security`.

## Run it

```
cmake --build --preset linux-gcc --target example-bearer-security
./build/linux-gcc/examples/bearer-security/example-bearer-security            # the answers, in-process
./build/linux-gcc/examples/bearer-security/example-bearer-security --serve    # serve on http://127.0.0.1:8000
```

It shows the answers in-process, and exits with a non-zero status if one is wrong. The token is `WOT_TOKEN` from the environment, or made up. Served, it prints the `curl` command to use, token included; in Swagger UI (`http://127.0.0.1:8000/docs`), "Authorize" takes the token.

## What to look for

- Every interaction (reads, writes, actions, invocations, observation, the WebSocket) answers 401 without the token, with `WWW-Authenticate: Bearer realm="teta-wot"`.
- The descriptions stay public: TDs, `/things/`, the directory, `/openapi.json` and the docs pages, so a client can find out what to send.
