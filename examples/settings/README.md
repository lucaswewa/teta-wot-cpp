# settings

**Concept:** properties that survive a restart. A setting is saved to the Thing's settings file after every change, and loaded from it when the server starts; the files are the ones teta-wot (and LabThings) writes, so they move freely between the servers.

**Technologies:** `.setting()` on a property, `ThingServerBuilder::settings_folder`, the settings file `{folder}/{thing}/Settings-{Class}.json`. The C++ port of teta-wot's `settings`.

## Run it

```
cmake --build --preset linux-gcc --target example-settings
./build/linux-gcc/examples/settings/example-settings                     # in a temporary folder
./build/linux-gcc/examples/settings/example-settings --folder settings   # keep the files in ./settings
```

It starts a server three times on the same folder: with the defaults, after changes, and after the file was edited by hand. It prints the same lines as the Rust example, and exits with a non-zero status if a value isn't as expected.

## What to look for

- No file exists until a setting changes. Then the whole file is written, keys in alphabetical order, indented four spaces (with CRLF line ends on Windows), atomically: a temporary file, then a rename.
- `frames` is a property, not a setting: it isn't in the file.
- A restarted server loads the file before any Thing starts.
- A hand-edited file with an invalid value (`exposure: -5`, against `gt(0)`) or an unknown key (`lamp`) only logs warnings: the other values load, and the next save leaves the unknown key out.
