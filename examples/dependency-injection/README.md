# dependency-injection

**Concept:** sharing a service between Things. A record store, registered once with the server, is injected into the actions that take it; an action that takes the `Server` reads every Thing's state and the application configuration.

**Technologies:** `ThingServerBuilder::service`, `tw::Dep<S>` and `tw::Server` action parameters, `t.thing_state(...)`, `Server::thing_states` and `Server::application_config`. The C++ port of teta-wot's `dependency-injection`.

## Run it

```
cmake --build --preset linux-gcc --target example-dependency-injection
./build/linux-gcc/examples/dependency-injection/example-dependency-injection
```

It records a temperature and two images (over HTTP, then in-process), prints the store, then shows that a Thing whose actions need a service can't be built without it. It exits with a non-zero status if the records aren't as expected.

## What to look for

- `Dep<DataStore>` and `Server` aren't part of an action's input: `capture`'s input schema is the empty object, over HTTP and in the TD.
- In-process calls (`ThingRef::call`) get the same injection as invocations.
- A missing service is found when the server is built, not when the action first runs: `Thing `camera` needs the service `DataStore`, which isn't registered with the server`. teta-wot names the service with its module path (`dependency_injection::DataStore`).
