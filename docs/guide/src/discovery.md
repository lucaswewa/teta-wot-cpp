# Discovery and security

A client on the lab network can find a server without knowing its address, and a server can require credentials for everything but its descriptions.

## The directory

Every server is a read-only Thing Description Directory, as W3C WoT Discovery describes:

- `GET /.well-known/wot` is the directory's own TD, at the root whatever the API prefix;
- `GET {prefix}/directory/things` lists every Thing's TD, sorted by `id`: all of them, or a page with `?offset=…&limit=…` (with `next` and `canonical` links), and with `?format=collection` as a `ThingCollection`;
- `GET {prefix}/directory/things/{id}` is one TD, by its `id` (percent-encoded in the path).

Its errors are always problem details, whatever the wire profile.

## Being found

With DNS-SD over mDNS, the server advertises itself as a directory (`_directory._sub._wot._tcp.local.`), under its server ID, with the path of its TD in the TXT record (`td=/.well-known/wot`):

```cpp
auto server = tw::ThingServer::builder().server_id("lab-microscope").mdns(true).thing<Stage>("stage").build();
server.serve("0.0.0.0", 8000);  // every interface: the advertisement names the computer's addresses
```

In a configuration file, it is `"mdns": true`. Advertising needs teta-wot-cpp built with `-DTETA_WOT_MDNS=ON` (the repository's presets have it on; `tw::mdns_supported()` says whether it is); without it, the server logs a warning and serves anyway. Any DNS-SD browser finds the server: Python's `zeroconf`, Avahi's `avahi-browse -r _wot._tcp`, or the `discovery` example's own client.

## Security

One of two schemes covers every interaction: reading and writing properties, invoking and following actions, events, streams, Blobs and the WebSocket.

```cpp
builder.security(tw::http::Security::bearer(token));            // Authorization: Bearer <token>
builder.security(tw::http::Security::basic("lab", password));   // HTTP Basic
```

Without the credentials, the answer is 401 with a `WWW-Authenticate` challenge. The descriptions stay public, so that a client can find out what to send: the TDs say it in `securityDefinitions` (`bearer_sc` or `basic_sc`, in the `Authorization` header), the OpenAPI document in `securitySchemes`, and `/things/`, the directory and the docs pages answer anyone.

A configuration file names the environment variable that holds the secret, never the secret:

```json
{"security": {"scheme": "bearer", "token_env": "WOT_TOKEN"}}
{"security": {"scheme": "basic", "username": "lab", "password_env": "WOT_PASSWORD"}}
```

The `bearer-security` and `discovery` examples show both.
