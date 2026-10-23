# discovery

**Concept:** being found (W3C WoT Discovery).

- The server advertises itself on the local network with DNS-SD over mDNS, as a Thing Description Directory (`_directory._sub._wot._tcp`).
- It serves the directory's TD at `/.well-known/wot`, and its Things' TDs at `/directory/things`.
- A client that knows nothing about it browses for directories, reads the well-known URL, and follows the directory's `things` form to every TD.

**Technologies:** the CMake option `TETA_WOT_MDNS` (on in the repository's presets) and `ThingServerBuilder::mdns(true)` (or `"mdns": true` in a configuration file); mjansson/mdns for browsing, the library the server advertises with; Python's `zeroconf` (`conformance/browse_mdns.py`). The C++ port of teta-wot's `discovery`.

## Run it

```
cmake --build --preset linux-gcc --target example-discovery
./build/linux-gcc/examples/discovery/example-discovery
```

This does both in one process: it serves two Things with mDNS advertising, browses the network, finds itself, and lists the TDs. It exits with a non-zero status if the advertisement isn't found within 20 seconds, which needs multicast on the network; built without `TETA_WOT_MDNS`, it says so and exits 77.

To be found by something else, serve until Ctrl-C (on every interface, port 8000 unless `--port` says otherwise; Windows may ask whether to allow this):

```
./build/linux-gcc/examples/discovery/example-discovery --serve
```
