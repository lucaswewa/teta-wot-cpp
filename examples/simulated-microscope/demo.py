"""A scripted session with the simulated microscope.

Uses the conformance project's Python environment:

    uv run --locked python examples/simulated-microscope/demo.py
    uv run --locked python examples/simulated-microscope/demo.py \
        --spawn target/debug/simulated-microscope.exe

The first form talks to a server already running on port 5000:

    cargo run -p simulated-microscope -- -c examples/simulated-microscope/microscope.json

``--spawn`` starts the server first, on a free port with a temporary
settings folder, and stops it afterwards (for CI). The captured image is
saved as ``capture.jpg`` in the current folder, or with ``--out``.
"""

import argparse
import json
import os
import socket
import subprocess
import tempfile
import time

import httpx
from labthings_fastapi import ThingClient

HERE = os.path.dirname(os.path.abspath(__file__))

parser = argparse.ArgumentParser()
parser.add_argument("url", nargs="?", default="http://127.0.0.1:5000")
parser.add_argument("--spawn", help="a simulated-microscope executable to start first")
parser.add_argument("--out", default="capture.jpg", help="where to save the captured image")
args = parser.parse_args()

server = None
base = args.url.rstrip("/")
folder = tempfile.TemporaryDirectory()
if args.spawn:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    config = json.load(open(os.path.join(HERE, "microscope.json")))
    config["settings_folder"] = os.path.join(folder.name, "settings")
    config_file = os.path.join(folder.name, "microscope.json")
    json.dump(config, open(config_file, "w"))
    # abspath: CreateProcess doesn't find relative paths written with "/".
    log = open(os.path.join(folder.name, "server.log"), "w")
    server = subprocess.Popen(
        [os.path.abspath(args.spawn), "-c", config_file, "--port", str(port)],
        stdout=log,
        stderr=subprocess.STDOUT,
    )
    base = f"http://127.0.0.1:{port}"
    for _ in range(300):
        try:
            httpx.get(f"{base}/stage/", timeout=1)
            break
        except httpx.HTTPError:
            time.sleep(0.1)

try:
    stage = ThingClient.from_url(f"{base}/stage/")
    camera = ThingClient.from_url(f"{base}/camera/")
    autofocus = ThingClient.from_url(f"{base}/autofocus/")

    print(f"stage.position: {stage.position}")
    print(f"stage.move_to(400, 200, 0) -> {stage.move_to(x=400, y=200, z=0)}")
    blurred = camera.sharpness
    print(f"camera.sharpness out of focus: {blurred:.3f}")

    camera.exposure = 30.0
    print(f"camera.exposure = {camera.exposure} (a setting, saved on the server)")

    result = autofocus.run(range=1000, steps=21)
    print(f"autofocus.run() -> z = {result['z']}, sharpness {result['sharpness']:.3f}")
    curve = result["curve"]
    print(f"  the focus curve, {len(curve)} rows of [z, sharpness]; the sharpest: {max(curve, key=lambda r: r[1])}")
    assert abs(result["z"] - 250) <= 50, result["z"]
    assert autofocus.last_focus == result["z"]
    sharp = camera.sharpness
    print(f"camera.sharpness in focus: {sharp:.3f}")
    assert sharp > blurred

    # A Blob: the image, downloaded when its content is read.
    image = camera.capture()
    image.save(args.out)
    print(f"camera.capture() -> {image.media_type}, {len(image.content)} bytes, saved to {args.out}")
    assert image.content[:2] == b"\xff\xd8", "a JPEG"

    # The live preview: a few MJPEG frames.
    with httpx.stream("GET", f"{base}/camera/preview", timeout=10) as preview:
        print(f"GET /camera/preview: {preview.headers['content-type']}")
        received = b""
        for chunk in preview.iter_bytes():
            received += chunk
            if received.count(b"--frame") >= 3:
                break
    print(f"  two frames received ({len(received)} bytes)")

    print(f"stage.home() -> {stage.home()}")
except BaseException:
    if server:
        log.flush()
        print("The server's log:\n" + open(os.path.join(folder.name, "server.log")).read())
    raise
finally:
    if server:
        server.terminate()
        server.wait(10)
        log.close()
    folder.cleanup()
