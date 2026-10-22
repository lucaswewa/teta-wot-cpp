// The quickstart counter: a first Thing, served.
//
//     example-quickstart-counter            # a short in-process demo
//     example-quickstart-counter --serve    # serve on http://127.0.0.1:5000
//
// The C++ port of teta-wot's `quickstart-counter`: the Thing is a class with
// a static `define`, which declares its property and actions with member
// pointers (what teta-wot's macros generate from a struct and an impl
// block). The demonstration goes through HTTP with TestClient, then calls the
// action in-process, as another Thing would.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// A test thing with a counter property and a couple of actions.
class TestThing {
  public:
    /// A pointless counter.
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
        t.action("increment_counter", &TestThing::increment_counter)
            .doc(
                "Increment the counter property.\n\n"
                "This action doesn't do very much - all it does, in fact,\n"
                "is increment the counter (which may be read using the\n"
                "`counter` property).");
        t.action("slowly_increase_counter", &TestThing::slowly_increase_counter)
            .doc("Increment the counter slowly over a minute.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<TestThing>("counter").build();
}

// The keys of a JSON object, as Rust's `{:?}` writes a list of strings.
std::string keys(const Value& object) {
    std::string out = "[";
    for (const auto& [key, value] : object.items()) {
        out += (out.size() > 1 ? ", " : "") + tw::json::dump(key);
    }
    return out + "]";
}

void demo() {
    const auto client = tw::testing::TestClient::start(server());
    const auto td = client.get("/counter/").json();
    std::cout << "The TD's affordances, from the definition:\n";
    std::cout << "  properties: " << keys(td["properties"]) << "\n";
    std::cout << "  actions:    " << keys(td["actions"]) << "\n";
    std::cout << "  increment_counter: title " << tw::json::dump(td["actions"]["increment_counter"]["title"])
              << ", description " << tw::json::dump(td["actions"]["increment_counter"]["description"])
              << "\n";

    // Over HTTP, as a client would.
    const auto invocation = client.post_json("/counter/increment_counter");
    std::string href = invocation.header("location").value_or("");
    href.erase(0, std::string_view("http://testserver").size());
    for (;;) {
        const auto status = client.get(href).json()["status"];
        if (status != "pending" && status != "running") {
            break;
        }
        std::this_thread::sleep_for(5ms);
    }
    std::cout << "\nPOST /counter/increment_counter, then GET /counter/counter → "
              << client.get("/counter/counter").text() << "\n";

    // In-process, as another Thing would: a typed call through the member
    // function.
    const auto counter = *client.runtime()->thing_ref<TestThing>("counter");
    counter.call(&TestThing::increment_counter);
    const auto value = counter.thing().counter.get();
    std::cout << "counter.call(&TestThing::increment_counter) in-process, then the counter is " << value
              << "\n";
    if (value != 2) {
        throw std::runtime_error("the counter should be 2");
    }
    client.stop();
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            tw::log::init(false);
            server().serve("127.0.0.1", 5000, tw::shutdown_signal(), [](std::uint16_t port) {
                std::cout << "listening on http://127.0.0.1:" << port << std::endl;
            });
            return EXIT_SUCCESS;
        }
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
