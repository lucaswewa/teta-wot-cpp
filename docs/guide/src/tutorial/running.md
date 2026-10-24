# Running a server

LabThings runs servers from configuration files, with its command line: `labthings-server -c config.json`. A teta-wot-cpp application does the same. It registers the Thing types it provides, and hands its command line to `tw::cli`.

## An application

The simulated microscope's `main` is a complete one:

```cpp
tw::ThingRegistry registry() {
    tw::ThingRegistry registry;
    registry.add<Stage>("microscope.stage:Stage")
        .add<Camera>("microscope.camera:Camera")
        .add<Autofocus>("microscope.autofocus:Autofocus");
    return registry;
}

int main(int argc, char** argv) {
    return tw::cli::serve_from_cli(argc, argv, registry());
}
```

The registry maps the names a configuration file uses to C++ types. The names can be anything. Using the import strings of the Python classes a C++ Thing replaces (such as `"labthings_fastapi.example_things:MyThing"`), or teta-wot's names for its Rust Things, lets one file configure every server.

## A configuration file

```json
{
    "things": {
        "stage": "microscope.stage:Stage",
        "camera": "microscope.camera:Camera",
        "autofocus": "microscope.autofocus:Autofocus"
    },
    "settings_folder": "./settings/microscope"
}
```

This is LabThings' format. Every Thing is named and given its type, either as just the type, or as an object with its `class` (or `cls`), `args`, `kwargs` and `thing_slots`. The same file keys are understood:

- `settings_folder`, where [settings](../settings.md) are saved;
- `api_prefix`, a prefix for every route (such as `/api/v1`);
- `enable_global_lock` and `global_lock_log_level`;
- `application_config`, anything for the Things to read.

teta-wot adds a few keys, which LabThings ignores: `server_id`, `wire_profile`, `security` and `mdns` ([Configuration](../configuration.md)).

## The command line

```text
example-simulated-microscope -c microscope.json
example-simulated-microscope -c microscope.json --host 0.0.0.0 --port 8000
example-simulated-microscope -j "{\"things\": {\"stage\": \"microscope.stage:Stage\"}}"
example-simulated-microscope -c microscope.json --fallback
```

The options are LabThings':

- `-c` names a file, and `-j` gives the configuration inline;
- `--host` and `--port` say where to listen (127.0.0.1:5000 by default);
- `--debug` logs more;
- `--fallback` serves an error page, instead of exiting, when a Thing can't start. It shows the error and the configuration, as LabThings' does.

The help, the error messages and the exit codes are teta-wot's:

| Code | When |
|---|---|
| 0 | stopped normally |
| 1 | no configuration, a missing file, a port in use, anything else |
| 2 | a command line that isn't valid |
| 3 | an invalid configuration (every problem is listed at once), or a Thing that couldn't start |

## What a running server offers

With the microscope running (`example-simulated-microscope -c examples/simulated-microscope/microscope.json`):

| URL | What |
|---|---|
| `/stage/`, `/camera/`, `/autofocus/` | Each Thing's Thing Description |
| `/things/`, `/thing_descriptions/` | Every Thing's URL, or every TD |
| `/docs`, `/redoc`, `/openapi.json` | The interactive API documentation and the OpenAPI 3.1 document |
| `/camera/preview/viewer` | The camera's live preview |
| `/.well-known/wot` | The server's Thing Description Directory ([Discovery](../discovery.md)) |

Stop it with Ctrl-C. Running invocations are cancelled, the Things stop in reverse order, and their devices close.
