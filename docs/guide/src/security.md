# Security

By default a server requires no credentials: anyone who can reach it can use it. A server can require HTTP Basic credentials or a bearer token for every interaction.

```cpp
auto server = tw::ThingServer::builder()
                  .security(tw::http::Security::bearer(token))  // or tw::http::Security::basic("lab", password)
                  .thing<Stage>("stage")
                  .build();
```

In a configuration file, the secret comes from an environment variable, never from the file itself:

```json
{"security": {"scheme": "bearer", "token_env": "WOT_TOKEN"}}
{"security": {"scheme": "basic", "username": "lab", "password_env": "WOT_PASSWORD"}}
```

## What is protected

- **Protected:** reading and writing properties, invoking and following actions, observing (server-sent events and the WebSocket's upgrade), streams, Blob downloads, and custom endpoints. Without the credentials, they answer `401` with a `WWW-Authenticate` challenge.
- **Public:** what describes the server, so that clients can find out what to send:
  - Thing Descriptions;
  - `/things/` and `/thing_descriptions/`;
  - discovery;
  - the OpenAPI document and the docs pages.

The TDs say what is required: `securityDefinitions` has `basic_sc` or `bearer_sc`, in the `Authorization` header, and `security` names it. The OpenAPI document has the matching security scheme, so Swagger UI shows an "Authorize" button. In tests, `TestClient::with_header("authorization", "Bearer " + token)` sends the credentials.

## Limits

- **Credentials travel in clear over HTTP.** On a network you don't trust, put the server behind a proxy that provides TLS.
- **Browsers can't add headers** to `EventSource`, `<img>` (MJPEG) or WebSockets. With Basic, they ask the user and then send the credentials themselves. With a bearer token, those pages need a script or a proxy.

The [`bearer-security`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/bearer-security) example shows the answers with and without a token.
