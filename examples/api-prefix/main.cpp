// A server mounted under `/api/v1`.
//
// Every route moves under the prefix, and so does every URL the server
// writes: form `href`s in the TD, the Thing list, invocation `href`s and
// links, and the `location` header. The TD's `base` stays the server's root.
// The example also shows that URLs are built from the request: the same
// server answers with the host name the client used. The C++ port of
// teta-wot's `api-prefix`: it prints the same.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

class Counter {
  public:
    tw::Prop<std::int64_t> count{0};

    std::int64_t increment() {
        count.update([](std::int64_t& c) { ++c; });
        return count.get();
    }

    static void define(tw::ThingSpec<Counter>& t) {
        t.title("Counter");
        t.property("count", &Counter::count).read_only();
        t.action("increment", &Counter::increment);
    }
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void run() {
    const auto server = tw::ThingServer::builder().api_prefix("/api/v1").thing<Counter>("counter").build();
    const auto client = tw::testing::TestClient::start(server).with_host("lab-pc.local:8000");

    const auto things = client.get("/api/v1/things/").json();
    std::cout << "GET /api/v1/things/\n  " << tw::json::dump(things) << "\n";
    check(things["counter"] == "http://lab-pc.local:8000/api/v1/counter/", "the Thing list is wrong");

    const auto td = client.get("/api/v1/counter/").json();
    std::cout << "GET /api/v1/counter/\n";
    std::cout << "  base: " << tw::json::dump(td["base"]) << "\n";
    std::cout << "  id:   " << tw::json::dump(td["id"]) << "\n";
    std::cout << "  count form:     " << tw::json::dump(td["properties"]["count"]["forms"][0]["href"])
              << "\n";
    std::cout << "  increment form: " << tw::json::dump(td["actions"]["increment"]["forms"][0]["href"])
              << "\n";
    check(td["properties"]["count"]["forms"][0]["href"] == "/api/v1/counter/count", "the form is wrong");

    const auto invocation = client.post_json("/api/v1/counter/increment");
    const auto body = invocation.json();
    std::cout << "POST /api/v1/counter/increment → " << invocation.status << " "
              << tw::http::reason_phrase(invocation.status) << "\n";
    std::cout << "  action:   " << tw::json::dump(body["action"]) << "\n";
    std::cout << "  href:     " << tw::json::dump(body["href"]) << "\n";
    std::cout << "  Location: " << invocation.header("location").value_or("-") << "\n";
    check(body["action"] == "/api/v1/counter/increment", "the action is wrong");

    const auto unprefixed = client.get("/counter/");
    std::cout << "GET /counter/ → " << unprefixed.status << " " << tw::http::reason_phrase(unprefixed.status)
              << " " << unprefixed.text() << "\n";
    check(unprefixed.status == 404, "the unprefixed route should be unknown");
    client.stop();
}

int main() {
    try {
        run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
