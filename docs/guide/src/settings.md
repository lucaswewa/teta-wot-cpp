# Settings

A setting is a property that survives restarts: a calibration, an exposure, a stage's speed. Declare a data property with `.setting()`:

```cpp
tw::Prop<double> speed{2000.0};
// in define:
t.property("speed", &Stage::speed).setting().gt(0).le(100000).doc("How fast the stage moves, in steps per second.");
```

- **Saving.** After every change, whether a client's write or the Thing's own `set`, the Thing's settings are saved to `{settings_folder}/{thing}/Settings-{Class}.json` (the class is the Thing's title, or `t.class_name(…)`). The file is replaced atomically, so a crash leaves the old one.
- **Loading.** When the server starts, before any Thing starts, the file is loaded if it exists, each value checked as a client's write would be.
- **Functional properties** can be settings too, if they have a setter: what is saved is the getter's value, read after each write through the property.

## The file

It is LabThings' and teta-wot's format, so the files move freely between the three servers: indented JSON, one key per setting, keys sorted, with CRLF line endings on Windows and LF elsewhere:

```json
{
    "speed": 2000.0
}
```

- **An unknown key** is logged, and dropped from the file at the next save.
- **A value that isn't valid** is logged, and its default is used.
- **A file that isn't a JSON object** is logged, and every setting keeps its default. The file is overwritten at the next save.

The server still starts.

## Where

The configuration file's `settings_folder` (default is `./settings`), or `ThingServerBuilder::settings_folder`. A service, or anything else started from another folder, should use an absolute path. A Thing finds its own folder, for files of its own, with `ThingCtx::settings_folder()` in its `on_start`.

The [`settings`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/settings) example writes and reloads a settings file, and loads a hand-edited one with unknown keys and invalid values; the conformance scenarios check that files move between teta-wot and teta-wot-cpp.
