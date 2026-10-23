// One Thing, served in both wire profiles.
//
// The same requests go to two servers built from the same Thing: one in the
// default `tetathing` profile, and one in the `wot` profile, which follows the
// W3C WoT HTTP Basic and SSE Profiles where the two conflict. The example
// prints each answer side by side, and checks the differences.
//
//     example-strict-profile
//
// The C++ port of teta-wot's `strict-profile`.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

namespace tw = teta::wot;
using tw::json::Value;
using tw::testing::TestClient;
using tw::testing::TestResponse;
using namespace std::chrono_literals;

/// A dimmable lamp.
class Lamp {
  public:
    /// The brightness, in percent.
    tw::Prop<std::int64_t> level{50};

    /// Fade to a level, slowly.
    std::int64_t fade(tw::ActionCtx& ctx, std::int64_t to) {
        ctx.sleep(100ms);
        level.set(to);
        return to;
    }

    /// Wait until cancelled.
    void wait(tw::ActionCtx& ctx) { ctx.sleep(60s); }

    /// Double a number. It's quick, so it is synchronous: in the `wot`
    /// profile, the caller gets the answer at once.
    std::int64_t twice(std::int64_t n) { return 2 * n; }

    static void define(tw::ThingSpec<Lamp>& t) {
        t.doc("A dimmable lamp.");
        t.property("level", &Lamp::level).doc("The brightness, in percent.").ge(0).le(100);
        t.action<"level">("fade", &Lamp::fade).doc("Fade to a level, slowly.");
        t.action("wait", &Lamp::wait).doc("Wait until cancelled.");
        t.action<"n">("double", &Lamp::twice)
            .doc(
                "Double a number. It's quick, so it is synchronous: in the `wot`\nprofile, the caller gets "
                "the "
                "answer at once.")
            .synchronous();
    }
};

TestClient serve(tw::http::WireProfile profile) {
    return TestClient::start(tw::ThingServer::builder().wire_profile(profile).thing<Lamp>("lamp").build());
}

/// A response in one line: status, content type and body, shortened.
std::string line(const TestResponse& response) {
    std::string body = response.text();
    if (body.size() > 70) {
        std::size_t cut = 70;
        while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) {
            --cut;  // to a character boundary
        }
        body = body.substr(0, cut) + "\xe2\x80\xa6";
    }
    return std::to_string(response.status) + " " + response.header("content-type").value_or("-") + " " + body;
}

void show(const std::string& request, const TestResponse& tetathing, const TestResponse& wot) {
    std::cout << request << "\n  tetathing: " << line(tetathing) << "\n  wot:       " << line(wot) << "\n";
}

void ensure(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

std::string keys(const Value& value) {
    std::string out;
    for (const auto& [key, _] : value.items()) {
        out += (out.empty() ? "" : ", ") + key;
    }
    return out;
}

std::string path_of(const TestResponse& response) {
    auto location = response.header("location").value_or("");
    const std::string origin = "http://testserver";
    if (location.starts_with(origin)) {
        location.erase(0, origin.size());
    }
    return location;
}

void run() {
    const auto tetathing = serve(tw::http::WireProfile::tetathing);
    const auto wot = serve(tw::http::WireProfile::wot);

    // Writing a property: 201 and `null`, or 204.
    auto l = tetathing.put_json("/lamp/level", 30);
    auto w = wot.put_json("/lamp/level", 30);
    show("PUT /lamp/level 30", l, w);
    ensure(l.status == 201 && w.status == 204 && w.body.empty(), "writes answer 201 or 204");

    // An invalid value: FastAPI's 422, or a 400 problem.
    l = tetathing.put_json("/lamp/level", 500);
    w = wot.put_json("/lamp/level", 500);
    show("PUT /lamp/level 500", l, w);
    ensure(l.status == 422 && w.status == 400, "invalid values answer 422 or 400");
    ensure(w.header("content-type") == "application/problem+json", "the wot profile's errors are problems");
    std::cout << "  wot's invalid-params: " << tw::json::dump(w.json()["invalid-params"]) << "\n";

    // Invoking: TetaThing's invocation, or the Profile's ActionStatus.
    const Value input = {{"level", 80}};
    l = tetathing.post_json("/lamp/fade", input);
    w = wot.post_json("/lamp/fade", input);
    show("POST /lamp/fade {\"level\": 80}", l, w);
    ensure(l.status == 201 && w.status == 201, "invoking answers 201");
    std::cout << "  tetathing' fields: " << keys(l.json()) << "\n";
    std::cout << "  wot's fields:      " << keys(w.json()) << "\n";

    // ...and once it has finished.
    std::this_thread::sleep_for(300ms);
    l = tetathing.get(path_of(l));
    w = wot.get(path_of(w));
    show("GET the invocation, finished", l, w);
    ensure(l.json()["status"] == "completed" && l.json()["timeCompleted"].is_string(),
           "tetathing: completed");
    ensure(w.json()["status"] == "completed" && w.json()["timeEnded"].get<std::string>().ends_with('Z'),
           "wot: completed");
    std::cout << "  tetathing' time: " << tw::json::dump(l.json()["timeCompleted"])
              << " (naive local time)\n";
    std::cout << "  wot's time:      " << tw::json::dump(w.json()["timeEnded"]) << " (RFC 3339, UTC)\n";

    // A synchronous action: 201 and an invocation, or 200 and the output.
    l = tetathing.post_json("/lamp/double", Value{{"n", 21}});
    w = wot.post_json("/lamp/double", Value{{"n", 21}});
    show("POST /lamp/double {\"n\": 21}", l, w);
    ensure(l.status == 201 && w.status == 200 && w.json() == 42, "only the wot profile waits for the output");

    // Cancelling: 200 and `null`, or 204; the invocation ends `cancelled`, or
    // `failed` with a problem.
    const auto l_path = path_of(tetathing.post_json("/lamp/wait"));
    const auto w_path = path_of(wot.post_json("/lamp/wait"));
    l = tetathing.del(l_path);
    w = wot.del(w_path);
    show("DELETE the invocation", l, w);
    ensure(l.status == 200 && w.status == 204, "cancelling answers 200 or 204");
    std::this_thread::sleep_for(100ms);
    l = tetathing.get(l_path);
    w = wot.get(w_path);
    std::cout << "  then tetathing: " << tw::json::dump(l.json()["status"]) << "\n";
    std::cout << "  then wot:       " << tw::json::dump(w.json()["status"]) << " ("
              << tw::json::dump(w.json()["error"]["title"]) << ")\n";
    ensure(l.json()["status"] == "cancelled" && w.json()["status"] == "failed", "cancelled, or failed");

    // An unknown path.
    l = tetathing.get("/nothing");
    w = wot.get("/nothing");
    show("GET /nothing", l, w);
    ensure(w.header("content-type") == "application/problem+json", "a 404 problem");

    // The TDs: the `wot` profile's names the Profiles, and says which actions
    // are synchronous.
    const auto l_td = tetathing.get("/lamp/").json();
    const auto w_td = wot.get("/lamp/").json();
    std::cout << "TD\n";
    std::cout << "  tetathing: profile "
              << (l_td.contains("profile") ? tw::json::dump(l_td["profile"]) : "null")
              << ", double synchronous: " << tw::json::dump(l_td["actions"]["double"]["synchronous"]) << "\n";
    std::cout << "  wot:       profile " << tw::json::dump(w_td["profile"])
              << ", double synchronous: " << tw::json::dump(w_td["actions"]["double"]["synchronous"]) << "\n";
    ensure(w_td["actions"]["double"]["synchronous"] == true, "wot: synchronous");
    ensure(l_td["actions"]["double"]["synchronous"] == false, "tetathing: not synchronous");

    // What doesn't differ: the new top-level resources answer the W3C way in
    // both profiles.
    l = tetathing.get("/lamp/properties");
    w = wot.get("/lamp/properties");
    show("GET /lamp/properties (readallproperties)", l, w);
    ensure(l.body == w.body, "the same in both");

    tetathing.stop();
    wot.stop();
    std::cout << "\nThe profiles differ where expected.\n";
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
