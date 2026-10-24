# Discovery

A client that knows nothing about a server can find it, and its Things, as W3C WoT Discovery describes.

## The well-known URL and the directory

Every server is a read-only Thing Description Directory of its own Things:

| URL | What |
|---|---|
| `/.well-known/wot` | The directory's TD (`@type` `ThingDirectory`), as `application/td+json`, at the root whatever the API prefix |
| `{prefix}/directory/things` | Every served TD, sorted by `id`, as `application/ld+json`; `?offset=` and `?limit=` page it (with `next` and `canonical` links), and `?format=collection` makes it a `ThingCollection` |
| `{prefix}/directory/things/{id}` | One TD, by its `id` (percent-encoded in the path) |

The directory's TD describes the other two. A generic consumer:

1. reads `/.well-known/wot`;
2. follows its `things` property's form;
3. reads the TDs it lists.

Its errors are always problem details, whatever the wire profile.

## DNS-SD

With `"mdns": true` in the configuration (or `.mdns(true)` on the builder), the server advertises itself on the local network:

- as a `_wot._tcp` service with the `_directory` subtype, named after its server ID;
- with a TXT record saying where its TD is (`td=/.well-known/wot`), its type (`type=Directory`), and its scheme.

```cpp
auto server = tw::ThingServer::builder().server_id("lab-microscope").mdns(true).thing<Stage>("stage").build();
server.serve("0.0.0.0", 8000);  // every interface: the advertisement names the computer's addresses
```

A browser for `_directory._sub._wot._tcp.local.` finds it: `dns-sd -B _wot._tcp` on Windows, `avahi-browse -r _wot._tcp` on Linux, or Python's `zeroconf`. The advertisement is withdrawn when the server stops. Multicast must be allowed on the network, and Windows may ask whether to let the server listen.

Advertising needs teta-wot-cpp built with `TETA_WOT_MDNS` (the repository's presets and the port's `mdns` feature have it; `tw::mdns_supported()` says whether it is). Without it, the server logs a warning and serves anyway.

The [`discovery`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/discovery) example advertises two Things, finds them from C++ and from Python, and reads the directory.
