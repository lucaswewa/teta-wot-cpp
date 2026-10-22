# http-walkthrough

**Concept:** the whole HTTP API of a Thing: every route, status code and header a client meets, over a real socket.

**Technologies:** `ThingServer::serve` on a free port (Drogon), stopped with a `std::stop_source`; a deliberately minimal HTTP/1.1 client over a socket; and `curl` in bash (`walkthrough.sh`) or PowerShell (`walkthrough.ps1`, with the `curl.exe` that ships with Windows). The C++ port of teta-wot's `http-walkthrough`; the scripts are teta-wot's, unchanged, since they only see the wire.

## Run it

```
cmake --build --preset linux-gcc --target example-http-walkthrough
./build/linux-gcc/examples/http-walkthrough/example-http-walkthrough        # serves on a free port, walks through it, exits
```

Or serve on port 5000 and run a script against it from another terminal (the scripts' comments say `cargo run`; the C++ server answers the same):

```
./build/linux-gcc/examples/http-walkthrough/example-http-walkthrough --serve
bash examples/http-walkthrough/walkthrough.sh            # needs curl and jq
powershell -File examples/http-walkthrough/walkthrough.ps1
```

It exits with a non-zero status if any answer's status isn't the expected one.

## What to look for

- Writes answer `201` and `null`; a coercible string (`"80"`) is accepted; a value out of range is a `422` with pydantic's error list; a read-only property refuses `PUT` with `405` and `allow`.
- An invocation is created with `201` and a `location`; `DELETE` cancels it once (`200`), then refuses (`503`).
- `/lamp` redirects to `/lamp/` with `307`, as Starlette does.
- Header order and `date`/`content-length` differ from the Rust server's (hyper's), and aren't part of the contract; the rest is the same.
