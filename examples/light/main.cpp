// A light with a constrained brightness, a read-only `is_on`, a `toggle`
// action and a functional `status` property: the guide's tutorial Thing.
//
//     example-light            # a short in-process demo
//     example-light --serve    # serve on http://127.0.0.1:5000
//
// The C++ port of teta-wot's `light` example: it prints the same.

#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

/// A computer-controlled light, our first example Thing.
class Light {
  public:
    /// The brightness of the light, in % of maximum.
    tw::Prop<std::int64_t> brightness{100};
    /// Whether the light is currently on.
    tw::Prop<bool> is_on{false};

    void toggle() {
        is_on.update([](bool& on) { on = !on; });
    }

    std::string status() const {
        if (is_on.get()) {
            return std::format("The light is on at {}% brightness.", brightness.get());
        }
        return "The light is off.";
    }

    static void define(tw::ThingSpec<Light>& t) {
        t.doc("A computer-controlled light, our first example Thing.");
        t.property("brightness", &Light::brightness)
            .doc("The brightness of the light, in % of maximum.")
            .ge(0)
            .le(100);
        t.property("is_on", &Light::is_on).read_only().doc("Whether the light is currently on.");
        t.action("toggle", &Light::toggle).doc("Swap the light between on and off.");
        t.property("status", &Light::status).doc("A human-readable status of the light.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Light>("light").build();
}

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

// A status as axum's `StatusCode` displays it: `422 Unprocessable Entity`.
std::string status_line(int status) {
    return std::format("{} {}", status, tw::http::reason_phrase(status));
}

void demo() {
    const auto client = tw::testing::TestClient::start(server());
    const auto td = client.get("/light/").json();
    const auto& brightness = td["properties"]["brightness"];
    std::cout << std::format("brightness: {} (minimum {}, maximum {}, default {})\n",
                             tw::json::dump(brightness["title"]), tw::json::dump(brightness["minimum"]),
                             tw::json::dump(brightness["maximum"]), tw::json::dump(brightness["default"]));
    std::cout << std::format("is_on: readOnly {}; status: readOnly {}\n",
                             tw::json::dump(td["properties"]["is_on"]["readOnly"]),
                             tw::json::dump(td["properties"]["status"]["readOnly"]));

    auto show = [&](std::string_view label) {
        std::cout << std::format("{:<34} status → {}\n", label, client.get("/light/status").text());
    };
    show("at first:");
    const auto light = *client.runtime()->thing_ref<Light>("light");
    light.call(&Light::toggle);
    show("after toggle():");

    const auto refused = client.put_json("/light/brightness", 150);
    std::cout << std::format("PUT /light/brightness 150 → {}: {}\n", status_line(refused.status),
                             tw::json::dump(refused.json()["detail"][0]["msg"]));
    check(refused.status == 422, "150 should be refused");
    client.put_json("/light/brightness", 40);
    show("after PUT /light/brightness 40:");
    const auto read_only = client.put_json("/light/is_on", false);
    std::cout << "PUT /light/is_on false → " << status_line(read_only.status) << " (read-only)\n";
    check(read_only.status == 405, "is_on should be read-only");
    check(client.get("/light/status").json() == "The light is on at 40% brightness.", "the status is wrong");
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
