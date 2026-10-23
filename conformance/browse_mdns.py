"""Finds a server's DNS-SD advertisement with Python's zeroconf (W3C WoT Discovery).

    python3 conformance/browse_mdns.py --server PATH

Starts the server with `"mdns": true`, browses for TD Directories
(`_directory._sub._wot._tcp.local.`) until it finds the server's port, and
checks the TXT record (`td`, `type`, `scheme`) and that `td` answers the
directory's TD. Exits 77 when zeroconf isn't installed (`pip install
zeroconf`), which CTest reports as skipped.
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run  # noqa: E402

try:
    from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
except ImportError:
    print("zeroconf isn't installed: skipped")
    sys.exit(77)

DIRECTORY = "_directory._sub._wot._tcp.local."


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--server", required=True, help="a server executable with teta-wot's command line")
    args = parser.parse_args()
    instance = f"teta-wot mdns check {os.getpid()}"
    server = run.Server(args.server, {"config": {"things": {"counter": "twins.counter:TestThing"},
                                                 "server_id": instance, "mdns": True}})
    found = {}

    class Listener(ServiceListener):
        def add_service(self, zc, type_, name):
            found[name] = zc.get_service_info(type_, name, timeout=3000)

        def update_service(self, zc, type_, name):
            pass

        def remove_service(self, zc, type_, name):
            pass

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, DIRECTORY, Listener())
        deadline = time.monotonic() + 20
        ours = None
        while ours is None and time.monotonic() < deadline:
            time.sleep(0.2)
            ours = next((info for info in list(found.values()) if info and info.port == server.port), None)
        if ours is None:
            print(f"not found within 20 s (is multicast allowed?); found: {sorted(found)}")
            sys.exit(1)
        txt = {key.decode(): (value or b"").decode() for key, value in ours.properties.items()}
        print(f"found {ours.name} on {ours.server} port {ours.port}, TXT {txt}, addresses {ours.parsed_addresses()}")
        assert ours.name == f"{instance}._wot._tcp.local.", ours.name
        assert txt == {"td": "/.well-known/wot", "type": "Directory", "scheme": "http"}, txt
        # The server listens on the loopback interface here: `td` is read there.
        _, _, raw = server.request("GET", txt["td"])
        td = json.loads(raw)
        assert td["@type"] == "ThingDirectory", td
        print(f"GET {txt['td']}: {td['title']}")
    finally:
        zc.close()
        server.stop()


if __name__ == "__main__":
    main()
