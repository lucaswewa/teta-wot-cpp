# Settings, slots and configuration files

A real instrument is several Things that use each other, keep their settings across restarts, and are put together by a configuration file rather than by code. This chapter shows the three pieces; the examples `settings`, `thing-slots`, `dependency-injection` and `config-and-cli` show each at length.

## Settings

A property becomes a setting with `.setting()`:

```cpp
class Microscope {
  public:
    tw::Prop<double> exposure{10.0};
    tw::Prop<std::string> objective{"10x"};

    static void define(tw::ThingSpec<Microscope>& t) {
        t.property("exposure", &Microscope::exposure).setting().gt(0).unit("ms");
        t.property("objective", &Microscope::objective).setting();
    }
};

auto server = tw::ThingServer::builder()
                  .thing<Microscope>("microscope")
                  .settings_folder("settings")
                  .build();
```

After every change, from a client or from C++ (`exposure.set(25)`), the Thing's settings are written to `settings/microscope/Settings-Microscope.json`: every setting, keys sorted, indented four spaces, as teta-wot and LabThings write them, so the files move freely between the servers. When the server starts, the file is loaded before any Thing starts, each value checked as a client's write would be. A value that doesn't validate, or a key that isn't a setting, only logs a warning.

A functional property can be a setting too, if it has a setter: what is saved is its getter's value, read after each write through the property.

## Slots

A Thing that uses another declares a slot, and the server connects it once every Thing is built:

```cpp
class Autofocus {
  public:
    tw::Slot<Stage> stage;            // exactly one Stage
    tw::OptSlot<Lamp> lamp;           // a Lamp, if there is one
    tw::SlotMap<CameraApi> cameras;   // every camera, by name

    static void define(tw::ThingSpec<Autofocus>& t) {
        t.slot("stage", &Autofocus::stage);
        t.slot("lamp", &Autofocus::lamp);
        t.slot("cameras", &Autofocus::cameras);
    }
};
```

A slot of a Thing type holds a `ThingRef<T>`: `stage->call(&Stage::move_to, 5)` calls the action as a client would, validated and under the global lock. By default a slot connects by type; `.defaults_to("camera")` picks a Thing by name, and a configuration file's `thing_slots` can choose another. The Things a Thing's slots use start before it, and stop after it.

A slot can also hold an *interface*: an abstract class that Things declare they provide.

```cpp
class CameraApi {
  public:
    virtual ~CameraApi() = default;
    virtual double sharpness(std::int64_t z) = 0;
};

class RealCamera : public CameraApi {
    ...
    static void define(tw::ThingSpec<RealCamera>& t) { t.provides<CameraApi>(); }
};
```

`t.provides<CameraApi>()` hands out the Thing itself; `t.provides<CameraApi>(make)` hands out what `make(ThingRef<T>)` returns, usually an adapter that calls the Thing's actions through the `ThingRef`. A slot's type must be complete where the slot is declared, so two Things that use each other connect one way through an interface.

## Configuration files and the command line

An application registers the Thing types its configuration files may name, and hands its command line to teta-wot-cpp:

```cpp
int main(int argc, char** argv) {
    tw::ThingRegistry registry;
    registry.add<Stage>("lab.stage:Stage").add<Autofocus>("lab.autofocus:Autofocus");
    return tw::cli::serve_from_cli(argc, argv, registry);
}
```

```json
{
    "things": {
        "stage": "lab.stage:Stage",
        "autofocus": {"class": "lab.autofocus:Autofocus", "thing_slots": {"camera": "real"}}
    },
    "settings_folder": "./settings",
    "api_prefix": "/api/v1"
}
```

```text
$ my-server -c config.json --port 5000
listening on http://127.0.0.1:5000
```

The file is LabThings' configuration file, and a mistake is reported as LabThings reports it, every problem at once. A Thing whose type has a `Config` (`using Config = StageConfig;` and a constructor taking one) reads it from its `kwargs`. The exit code says what happened: 0 when the server stopped normally, 2 for a wrong command line, 3 for an invalid configuration or a Thing that couldn't start, 1 for anything else. With `--fallback`, a server that can't start serves a page explaining why, at its usual address, until it is stopped.
