# Quickstart

A counter Thing, written in C++, served, and used from a browser and `curl`. It is the [`quickstart-counter`](https://github.com/lucaswewa/teta-wot-cpp/tree/main/examples/quickstart-counter) example.

## A Thing

A program links `teta::wot` (see [Installing](tutorial/installing.md)):

```cmake
find_package(teta-wot CONFIG REQUIRED)
add_executable(counter main.cpp)
target_link_libraries(counter PRIVATE teta::wot)
```

The Thing is a class with a static `define`:

```cpp
#include <teta/wot.hpp>
namespace tw = teta::wot;
using namespace std::chrono_literals;

class TestThing {
  public:
    tw::Prop<std::int64_t> counter{0};

    void increment_counter() {
        counter.update([](std::int64_t& c) { ++c; });
    }

    void slowly_increase_counter(tw::ActionCtx& ctx) {
        for (int i = 0; i < 60; ++i) {
            ctx.sleep(1s);
            counter.update([](std::int64_t& c) { ++c; });
        }
    }

    static void define(tw::ThingSpec<TestThing>& t) {
        t.doc("A test thing with a counter property and a couple of actions.");
        t.property("counter", &TestThing::counter).read_only().doc("A pointless counter.");
        t.action("increment_counter", &TestThing::increment_counter).doc("Increment the counter property.");
        t.action("slowly_increase_counter", &TestThing::slowly_increase_counter)
            .doc("Increment the counter slowly over a minute.");
    }
};
```

- **`define`** declares what clients see, with member pointers, so the compiler checks every type. `t.doc` gives the Thing's description.
- **`t.property`** on a `Prop<T>` member makes a property, which clients read and, unless it's `read_only`, write.
- **`t.action`** on a member function makes an action, which clients invoke. Its parameters are the input, its return value the output.
- **`ActionCtx`** is the invocation's context: `ctx.sleep` waits, but stops early if the invocation is cancelled.

## A server

```cpp
int main() {
    tw::log::init(false);
    auto server = tw::ThingServer::builder().thing<TestThing>("counter").build();
    server.serve("127.0.0.1", 5000);  // until Ctrl-C
}
```

`serve` starts the Things and serves until the process is asked to stop (Ctrl-C, or SIGTERM on Linux), then stops gracefully.

## Using it

Run it, then:

- **Its Thing Description** is at <http://127.0.0.1:5000/counter/>. It describes the property and the actions, and how to use them.
- **The interactive API documentation** is at <http://127.0.0.1:5000/docs>. "Try it out" invokes `increment_counter`, and reads `counter`.
- **From `curl`** (on Windows, `curl.exe`):

  ```text
  curl http://127.0.0.1:5000/counter/counter
  curl -X POST http://127.0.0.1:5000/counter/increment_counter
  ```

  The `POST` answers `201 Created` with the invocation, whose `href` shows its progress.

## Next

- The [tutorial](tutorial/index.md) serves Things from a configuration file, and writes a Thing with more features.
- [Actions](actions.md) and [Properties](properties.md) describe everything `define` can declare.
