// A Thing for Eclipse Thingweb node-wot to consume.
//
// The same Thing is served in each wire profile, because node-wot 0.9.2 needs
// something from each:
//
// - it reads an action's output from the `invokeaction` response, which only
//   a synchronous action in the `wot` profile answers with (200 and the
//   output);
// - it observes with `EventSource.onmessage`, which receives only unnamed
//   server-sent events: the `tetathing` profile's. The `wot` profile names
//   each event after its property, as the WoT SSE Profile requires.
//
//     example-node-wot-consumer                                   # check, in-process, what node-wot uses
//     example-node-wot-consumer --serve                           # tetathing on port 8000 (or --port)
//     example-node-wot-consumer --serve --profile wot --port 8001 # wot, in a second process
//
// The C++ port of teta-wot's `node-wot-consumer`. teta-wot serves both
// profiles from one process; Drogon runs one application per process, so
// here each `--serve` serves one profile.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// A dimmable lamp.
class Lamp {
  public:
    /// The brightness, in percent.
    tw::Prop<std::int64_t> level{50};

    /// Double a number.
    std::int64_t twice(std::int64_t n) { return 2 * n; }

    /// Fade to a level, slowly.
    std::int64_t fade(tw::ActionCtx& ctx, std::int64_t to) {
        ctx.sleep(200ms);
        level.set(to);
        return to;
    }

    static void define(tw::ThingSpec<Lamp>& t) {
        t.doc("A dimmable lamp.");
        t.property("level", &Lamp::level).doc("The brightness, in percent.").ge(0).le(100);
        t.action<"n">("double", &Lamp::twice).doc("Double a number.").synchronous();
        t.action<"level">("fade", &Lamp::fade).doc("Fade to a level, slowly.");
    }
};

tw::ThingServer server(tw::http::WireProfile profile) {
    return tw::ThingServer::builder().wire_profile(profile).thing<Lamp>("lamp").build();
}

void ensure(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

/// Checks, without Node, the TDs and answers that node-wot relies on.
void check() {
    for (const auto profile : {tw::http::WireProfile::tetathing, tw::http::WireProfile::wot}) {
        const auto client = tw::testing::TestClient::start(server(profile));
        const auto td = client.get("/lamp/").json();
        const auto& forms = td["properties"]["level"]["forms"];
        ensure(forms[0]["op"] == Value::array({"readproperty", "writeproperty"}), "a read and write form");
        ensure(forms[1]["subprotocol"] == "sse", "an SSE observation form");
        const bool synchronous = td["actions"]["double"]["synchronous"] == true;
        const auto invoked = client.post_json("/lamp/double", Value{{"n", 21}});
        auto events = client.events("/lamp/level");
        (void)client.put_json("/lamp/level", 30);
        const auto message = events.next_message();
        ensure(message.has_value(), "an event");
        const bool named = message->event.has_value();
        std::cout << tw::http::to_string(profile) << ":\n"
                  << "  double is synchronous: " << (synchronous ? "true" : "false") << "\n"
                  << "  invoking it answers " << invoked.status << " " << invoked.text() << "\n"
                  << "  observation events are named: " << (named ? "true" : "false") << "\n";
        if (profile == tw::http::WireProfile::tetathing) {
            // node-wot observes, but can't read an output.
            ensure(!synchronous && !named, "tetathing: asynchronous, unnamed events");
        } else {
            // node-wot reads the output, but can't observe.
            ensure(synchronous && invoked.json() == 42 && named, "wot: synchronous, named events");
        }
        client.stop();
    }
    std::cout << "\nRun `node consumer.mjs` against `--serve` to see node-wot use them.\n";
}

int main(int argc, char** argv) {
    try {
        bool serve = false;
        std::uint16_t port = 8000;
        auto profile = tw::http::WireProfile::tetathing;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--serve") {
                serve = true;
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
            } else if (arg == "--profile" && i + 1 < argc) {
                const auto named = tw::http::wire_profile_from_name(argv[++i]);
                if (!named) {
                    throw std::invalid_argument("--profile is `tetathing` or `wot`");
                }
                profile = *named;
            }
        }
        if (!serve) {
            check();
            return EXIT_SUCCESS;
        }
        tw::log::init(false);
        server(profile).serve("127.0.0.1", port, tw::shutdown_signal(), [&](std::uint16_t bound) {
            std::cout << tw::http::to_string(profile) << " http://127.0.0.1:" << bound << "/lamp/"
                      << std::endl;
        });
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
