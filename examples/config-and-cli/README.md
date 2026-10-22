# config-and-cli

**Concept:** an application binary that serves Things from configuration files. The application registers the Thing types its files may name; everything else is the configuration's.

**Technologies:** `tw::ThingRegistry`, a Thing with a typed configuration (`using Config = CounterConfig;` and a constructor taking it, read from `kwargs`), `tw::cli::serve_from_cli` (`-c`, `-j`, `--host`, `--port`, `--fallback`, `--debug`, and teta-wot's exit codes), and the fallback server. The C++ port of teta-wot's `config-and-cli`.

## Run it

```
cmake --build --preset linux-gcc --target example-config-and-cli
./build/linux-gcc/examples/config-and-cli/example-config-and-cli                                         # a demonstration, then exit
./build/linux-gcc/examples/config-and-cli/example-config-and-cli -c examples/config-and-cli/config.json   # serve until Ctrl-C
./build/linux-gcc/examples/config-and-cli/example-config-and-cli -c examples/config-and-cli/failing.json --fallback
```

The demonstration runs the command line in-process three times (`tw::cli::serve_until`): with a working configuration, which it serves and queries over a socket; with a Thing that can't start; and with an unknown class. It prints what the server answered and the exit codes, and exits with a non-zero status if one isn't as expected.

## What to look for

- `config.json` gives the counter `"kwargs": {"start": 41}`: `CounterConfig` reads it, refusing unknown keys, and `spare` takes the defaults.
- Exit codes: 0 when the server stopped normally, 3 for a Thing that can't start or an invalid configuration, 1 for anything else, 2 for a wrong command line.
- With `--fallback`, a server that can't start serves an error page at its address instead of exiting: `/` is the page (500), `/fallback` says `true`, and every other path redirects to `/`. Drogon serves once per process, so the demonstration shows the page's answers in-process; `-c failing.json --fallback` serves it for real, and `conformance/scenarios/cli` compares it with teta-wot's.
