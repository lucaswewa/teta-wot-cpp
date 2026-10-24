# Actions

An action is something a Thing does when asked: move a stage, capture an image, run an autofocus. Clients invoke it with a `POST`. The server answers at once with an *invocation*, which runs in the background and can be followed, and cancelled.

```cpp
class Stage {
  public:
    Position move_to(std::int64_t x, std::int64_t y, std::int64_t z) { return travel({x, y, z}); }

    static void define(tw::ThingSpec<Stage>& t) {
        t.action<"x", "y", "z">("move_to", &Stage::move_to)
            .doc("Move to a position.\n\nThe stage moves in small steps at its speed, and stops where it is if\n"
                 "the move is cancelled.")
            .global_lock(false);
    }
};
```

## Inputs and outputs

**Parameters are the input's fields,** named in order by the template arguments: `move_to` takes `{"x": 1, "y": 2, "z": 3}`, and its TD describes an object with three required integers. Each parameter has options:

```cpp
auto& run = t.action<"range", "steps">("run", &Autofocus::run);
run.param("range").default_value(1000);              // optional, with a default
run.param("steps").defaulted().description("…");     // optional, defaulting to the type's default value
```

Some parameters are supplied by the server, not the client, and need no name:

- `tw::ActionCtx&`, the invocation's context;
- `tw::Server`, the server;
- `tw::Dep<S>`, a service registered with the server ([`dependency-injection`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/dependency-injection)).

`t.action(name, &T::f, tw::whole_input)` makes the only other parameter's type the whole input. An action without data parameters accepts an empty body, `null` or `{}`.

**The return value is the output.**

- A value, `tw::Expected<V>` (a value or an `ActionError`), or `void`.
- Any type with a codec: numbers, strings, `std::optional`, `std::vector`, maps, enums described with `Describe`, aggregate structs (reflected with Boost.PFR, described with a static `describe`), [Blobs](blobs.md), [arrays](arrays.md).

**Inputs are validated as teta-wot validates them.** pydantic's coercions and error messages are reproduced: `"2"` is accepted for an integer, and an invalid input is a 422 with pydantic's list of errors, before the action runs.

## Invocations

`POST /stage/move_to` answers `201 Created`, with the invocation's URL in `location` and its JSON in the body. Its `status` goes:

- `pending` → `running` → `completed`, with the `output`;
- or ends `error`, with the problem in `error`;
- or ends `cancelled`.

| Route | What |
|---|---|
| `GET /action_invocations/{id}` | The invocation: status, times, input, output, log, error |
| `DELETE /action_invocations/{id}` | Cancel it |
| `GET /action_invocations/{id}/output` | Its output (a Blob's data, for a Blob) |
| `GET /stage/move_to`, `GET /action_invocations` | The invocations of one action, or of all |

Finished invocations are kept for 5 minutes (`.retention(60s)` changes it), then forgotten. In the [`wot` wire profile](wire_profiles.md), invocations are the WoT Profile's `ActionStatus` objects instead.

## Cancellation

Cancellation is cooperative: an action stops where it checks.

- `ctx.sleep(duration)` and `tw::cancellable_sleep(duration)` wait, but throw `tw::Cancelled` as soon as the invocation is cancelled, which ends the action.
- `ctx.check_cancelled()` checks between steps.
- `ctx.cancel_token()` gives the token itself, to register a callback (`on_cancel`) that interrupts a blocking wait, such as a driver's.

A cancelled action ends `cancelled`. Code that must tidy up does it as the exception passes (in a destructor, or a `catch` that rethrows); catching `tw::Cancelled` and returning normally completes the invocation instead. `ctx.spawn_child(f)` runs part of the work as a child invocation on another thread, cancelled with its parent. See the [`cancellable-action`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/cancellable-action) example.

## Errors

- **An anticipated failure,** such as "the stage is at its limit", is `throw tw::HandledError("message")` (or `return tw::unexpected(tw::ActionError::handled("message"))`): it is logged without a traceback, and the invocation's `error` has the message.
- **Any other exception** ends the invocation as `error`, with its message, its nested causes and its type's short name (the problem's `title`, `Error` for the standard library's exceptions) in the log.
- **Something that isn't a `std::exception`** is a panic, as in teta-wot: the invocation ends `error`, and the server carries on.

## Logs

`tw::log::info(…)` (and `debug`, `warn`, `error`), called while an action runs, on its thread or a child invocation's, is recorded in its invocation's `log`, which clients read. `tw::log::init` (or `cli::serve_from_cli`, which calls it) also writes it to standard error; `<teta/wot/log_spdlog.hpp>` sends it to spdlog instead. See the [`invocation-logs`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/invocation-logs) example.

## Options

| Option | Effect |
|---|---|
| `.global_lock(false)` | Don't hold the server's global lock while running ([Concurrency](concurrency.md)) |
| `.retention(60s)` | Keep finished invocations 60 seconds |
| `.synchronous()` | In the `wot` wire profile, the caller waits and gets the output ([Wire profiles](wire_profiles.md)) |
| `.doc("…")`, `.title("…")`, `.description("…")`, `.semantic_type("…")` | The title (the first paragraph) and description; an `@type` |

Every action runs on a worker thread, so blocking code (a driver call, a long computation) is fine in an action; `ctx.blocking(f)` is kept for teta-wot's sake, and calls `f` directly.

## Calling actions in-process

A `ThingRef<T>` calls a Thing's actions by their member functions, typed:

```cpp
const auto stage = *runtime->thing_ref<Stage>("stage");
const Position here = stage.call(&Stage::move_to, 0, 0, 250);
```

Other Things do the same through their [slots](thing_slots.md) (`stage->call(…)`). A call is validated as a client's request would be, and runs in the calling invocation's context: it shares its cancellation, its log and its hold on the global lock. `call_action("move_to", json)` calls one by name, with JSON.
