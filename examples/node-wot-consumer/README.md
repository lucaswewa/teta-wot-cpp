# node-wot-consumer

**Concept:** interoperability with another WoT implementation. [Eclipse Thingweb node-wot](https://github.com/eclipse-thingweb/node-wot) consumes a Thing served by teta-wot-cpp, knowing only its TD: it reads and writes a property, invokes an action and observes the property.

**Technologies:** the two wire profiles; the TD's observation (SSE) forms; a synchronous action. The C++ port of teta-wot's `node-wot-consumer`.

## Run it

The C++ side checks, in-process, that both TDs have the forms node-wot uses, and that each profile answers as node-wot needs:

```
cmake --build --preset linux-gcc --target example-node-wot-consumer
./build/linux-gcc/examples/node-wot-consumer/example-node-wot-consumer
```

node-wot 0.9.2 needs something from each profile: the output of a synchronous action, which only `wot` answers with, and unnamed server-sent events, which only `tetathing` sends. teta-wot serves both from one process; Drogon runs one application per process, so serve each in its own:

```
./build/linux-gcc/examples/node-wot-consumer/example-node-wot-consumer --serve                            # tetathing on port 8000
./build/linux-gcc/examples/node-wot-consumer/example-node-wot-consumer --serve --profile wot --port 8001  # wot on port 8001
```
