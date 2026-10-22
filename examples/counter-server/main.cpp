// The quickstart counter on a real server: the first end-to-end milestone.
//
//     example-counter-server                       # a short in-process demo
//     example-counter-server --serve               # serve on http://127.0.0.1:5000
//     example-counter-server --serve --port 0      # any free port
//
// The C++ port of teta-wot's `counter-server`. Served, it is Drogon that
// listens; the demonstration makes the same requests through TestClient.

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
        t.title("TestThing").description("A test thing with a counter property and a couple of actions.");
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

// The in-process demo: the same requests the Python client makes.
void demo() {
    const auto client = tw::testing::TestClient::start(server());
    std::cout << "GET /counter/counter → " << client.get("/counter/counter").text() << "\n";
    const auto invocation = client.post_json("/counter/increment_counter");
    std::cout << "POST /counter/increment_counter → " << invocation.status << " "
              << tw::http::reason_phrase(invocation.status) << " "
              << tw::json::dump(invocation.json()["status"]) << "\n";
    const std::string href = "/action_invocations/" + invocation.json()["id"].get<std::string>();
    for (;;) {
        const auto polled = client.get(href).json();
        if (polled["status"] != "pending" && polled["status"] != "running") {
            std::cout << "GET " << href << " → " << tw::json::dump(polled["status"]) << "\n";
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    const auto value = client.get("/counter/counter").json();
    std::cout << "GET /counter/counter → " << tw::json::dump(value) << "\n";
    if (value != 1) {
        throw std::runtime_error("the counter should be 1");
    }
    client.stop();
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 5000;
            if (argc > 2 && std::string_view(argv[2]) == "--port") {
                if (argc < 4) {
                    throw std::invalid_argument("--port needs a value");
                }
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server().serve("127.0.0.1", port, tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "listening on http://127.0.0.1:" << bound << std::endl;
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
