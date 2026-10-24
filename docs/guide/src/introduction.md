# Introduction

teta-wot-cpp makes laboratory instruments, and the software around them, available over HTTP as [W3C Web of Things](https://www.w3.org/WoT/) *Things*, from C++. It is a C++20 implementation of [teta-wot](https://github.com/lucaswewa/teta-wot) v0.1.0: the same features, and the same behaviour on the wire, byte for byte, so a client can't tell which one it is talking to. A [few differences](https://github.com/lucaswewa/teta-wot-cpp/blob/main/docs/wire-contract.md#3-deltas), all at the edges, are documented.

Its key features and aims are teta-wot's:

- **Things describe units of hardware or software.** A Thing is a C++ class with a static `define` function.
- **Properties and actions** are members and member functions, named in `define`, and added to the HTTP API and its documentation from those declarations alone.
- **Types describe the data.** The types of properties, action inputs and outputs are C++ types, whose JSON Schemas become the Thing Description's data schemas and the OpenAPI document's.
- **The lifecycle and concurrency suit hardware:**
  - each Thing is created, started and stopped once;
  - every action runs on a worker thread, and drivers that must stay on one thread run behind a device;
  - long actions can be cancelled;
  - an optional global lock keeps actions from overlapping.
- **The vocabulary and concepts are the W3C Web of Things'**: every Thing has a [Thing Description](wot_core_concepts.md), and a stricter wire profile follows the W3C WoT Profile.
- **More of the Web of Things:** events, observation described in Thing Descriptions, the W3C WoT Profile as a [wire profile](wire_profiles.md), [discovery](discovery.md) and [security](security.md).
- **A service.** A server can [run as a Windows service or a systemd unit](services.md), and stop gracefully with it.
- **One binary.** On Windows a server is a single executable, with no runtime to install.

It is built on [Drogon](https://github.com/drogonframework/drogon), with CMake and vcpkg, for Windows (MSVC) and Linux (GCC, Clang).

## Where to start

- [Quickstart](quickstart.md): a counter Thing, served in a few minutes.
- [Tutorial](tutorial/index.md): building, running a server from a configuration file, and writing a Thing.
- The [examples](examples.md): each one teaches one thing, from a hand-built Thing Description to a simulated microscope.

## Parity with teta-wot

teta-wot v0.1.0 is the reference: what it answers is the contract ([ADR-0001](https://github.com/lucaswewa/teta-wot-cpp/blob/main/docs/adr/0001-parity-reference.md)). The repository's conformance harness replays its recorded answers against teta-wot-cpp, and runs both servers side by side; every example is compared with its Rust original the same way. Settings files and configuration files move freely between the two, and LabThings' Python client works with both.

## Licence

MIT. The repository also holds work by others under their own licences ([`THIRD_PARTY_NOTICES.md`](https://github.com/lucaswewa/teta-wot-cpp/blob/main/THIRD_PARTY_NOTICES.md)).
