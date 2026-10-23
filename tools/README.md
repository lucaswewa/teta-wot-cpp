# Tools

Programs for maintaining and releasing teta-wot-cpp, not for applications.

| Tool | What it does |
|---|---|
| [`soak/`](soak/main.cpp) | The soak test, a port of teta-wot's. It drives the simulated microscope, running as its own process, with every kind of client for a given time: properties, moves (some cancelled), autofocus runs, Blob captures, two MJPEG viewers, two server-sent events streams and a WebSocket. Each minute it records requests, errors, latency, frames, events and WebSocket messages, the server's working set, private bytes and handles, and the invocations kept; it fails on any error, an ended stream, or private bytes that keep growing after the warm-up. CI runs it for 3 minutes; releases, for 24 hours. With `--stop-under-load` it is the shutdown-under-load test (CTest `tools.shutdown-under-load`): at the end it stops the server as Ctrl-C does while every client is at work and a long move is running, and fails unless the server exits with 0 within 15 seconds, the move cancelled. |

```
cmake --build --preset windows-msvc --target soak
build\windows-msvc\tools\soak\soak --server build\windows-msvc\examples\simulated-microscope\example-simulated-microscope --duration 10m --report soak.csv
build\windows-msvc\tools\soak\soak --server build\windows-msvc\examples\simulated-microscope\example-simulated-microscope --duration 30s --stop-under-load
```

Options: `--duration` (`90s`, `10m`, `24h`; 5 minutes by default), `--profile tetathing|wot`, `--warmup` (a fifth of the run by default, at most 10 minutes), `--report FILE` (a CSV row per minute), `--stop-under-load`.

The server keeps finished invocations for 300 seconds, as teta-wot does, so its memory grows for the first five minutes of a run before it levels off: a run shorter than about 20 minutes can't judge a leak.
