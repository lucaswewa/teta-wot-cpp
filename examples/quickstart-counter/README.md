# quickstart-counter

**Concept:** writing a first Thing and serving it: a class with a static `define` that declares its property and actions.

**Technologies:** `ThingSpec` (`property`, `action`, `.doc()`, `.read_only()`), `ActionCtx::sleep`, `ThingServer`, `tw::testing::TestClient`, and a typed in-process call through `ThingRef::call`. The C++ port of teta-wot's `quickstart-counter`, where the macros generate what `define` declares here.

## Run it

```
cmake --build --preset linux-gcc --target example-quickstart-counter
./build/linux-gcc/examples/quickstart-counter/example-quickstart-counter            # a short in-process demo, then exit
./build/linux-gcc/examples/quickstart-counter/example-quickstart-counter --serve    # serve on http://127.0.0.1:5000 until Ctrl-C
```

## What to look for

- The TD's title and description come from `.doc()`, split as teta-wot splits doc comments: the first paragraph is the title.
- `POST /counter/increment_counter` answers at once with `201` and a `location`; the client polls it until the invocation has finished.
- `counter.call(&TestThing::increment_counter)` is the same action called in-process: validated, under the global lock if it is on, and in the caller's invocation context.
