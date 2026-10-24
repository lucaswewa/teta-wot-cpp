# Configuration and the command line

A server is configured by a configuration file, or in code with `ThingServer::builder()`. The [tutorial](tutorial/running.md) runs one.

## The file

```json
{
    "things": {
        "stage": "microscope.stage:Stage",
        "camera": {"class": "microscope.camera:Camera", "kwargs": {}, "thing_slots": {"stage": "stage"}}
    },
    "settings_folder": "./settings",
    "api_prefix": "",
    "enable_global_lock": false,
    "global_lock_log_level": "INFO",
    "application_config": {"lab": "B12"}
}
```

**LabThings' keys:**

| Key | Meaning |
|---|---|
| `things` | Each Thing's name and type: a registered type name, or `{"class" (or "cls"), "args", "kwargs", "thing_slots"}` |
| `settings_folder` | Where settings files go (`./settings`) |
| `api_prefix` | A prefix for every route, such as `/api/v1` |
| `enable_global_lock` | Whether actions and writes take the global lock |
| `global_lock_log_level` | The level of the "Global lock was busy" message |
| `application_config` | Anything, for Things to read (`Server::application_config()`) |

A Thing's `kwargs` (or its one `args` value) become its typed configuration: a type with `using Config = StageConfig;` and a constructor taking a `StageConfig`, an aggregate struct read like any input (`describe` can make fields optional, or refuse unknown ones):

```cpp
struct StageConfig {
    std::string port = "COM3";
    static void describe(tw::StructInfo<StageConfig>& s) { s.field("port").defaulted(); }
};

class Stage {
  public:
    using Config = StageConfig;
    explicit Stage(StageConfig config);
    …
};
```

Unknown top-level keys are logged and ignored. A file with mistakes is reported as LabThings reports it, every problem at once.

**teta-wot's keys**, which LabThings ignores:

| Key | Meaning |
|---|---|
| `server_id` | Identifies the server in TD `id`s (the computer's name by default) |
| `wire_profile` | `"tetathing"` (the default) or `"wot"` ([Wire profiles](wire_profiles.md)) |
| `security` | `{"scheme": "basic", "username": "…", "password_env": "VAR"}` or `{"scheme": "bearer", "token_env": "VAR"}` ([Security](security.md)) |
| `mdns` | `true` to advertise with DNS-SD ([Discovery](discovery.md)) |

## The registry

An application registers the Thing types its configuration files may name:

```cpp
tw::ThingRegistry registry;
registry.add<Stage>("microscope.stage:Stage").add<Camera>({"microscope.camera:Camera", "camera:Camera"});
```

A type can have several names, so that files written for LabThings, teta-wot and teta-wot-cpp all work.

## In code

```cpp
auto server = tw::ThingServer::builder()
                  .thing<Stage>("stage")
                  .thing("camera", std::make_shared<Camera>(camera_config))
                  .settings_folder("C:/lab/settings")
                  .api_prefix("/api/v1")
                  .global_lock(true)
                  .wire_profile(tw::http::WireProfile::wot)
                  .shutdown_grace(10s)
                  .build();
server.serve("0.0.0.0", 5000);
```

`ThingServer::from_config(tw::ServerConfig::from_value(json), registry)` turns a file into a builder, which can still be changed (to add services, for example). `build()` throws `BuildError` (a slot that can't be filled, a service missing) and `serve` throws `StartupError` (a Thing that couldn't start) or `ServeError` (the port is in use).

## The command line

`tw::cli::serve_from_cli(argc, argv, registry)` is an application's whole command line: `-c`/`-j`, `--host`, `--port`, `--fallback`, `--debug`, teta-wot's help and messages, and its exit codes ([Running a server](tutorial/running.md)). It takes a fourth argument, a function that changes the builder before it builds (to add services, or set the grace period). `cli::serve_until(args, registry, customise, stop_token)` takes the arguments and its own stop signal, for tests and [services](services.md); `cli::arguments(argc, argv)` gives the process's arguments as UTF-8.

The [`config-and-cli`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/config-and-cli) example runs a configuration, a broken one, and the fallback page.
