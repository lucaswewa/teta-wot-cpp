// Request bodies: the first byte of the input picks a wire profile, a route
// of a probe Thing and a content type; the rest is the body. Whatever the
// body, the HTTP binding answers without an internal error (no 500), with a
// status it uses, and with JSON wherever it says it answers JSON.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fuzz.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;

// Named: Boost.PFR can't reflect a type with internal linkage.
namespace request_body_fuzz {

struct Point {
    std::int64_t x = 0;
    double y = 0;
    std::optional<std::string> label;
};

}  // namespace request_body_fuzz

namespace {

using request_body_fuzz::Point;

struct Probe {
    tw::Prop<std::int64_t> count{0};
    tw::Prop<double> ratio{0.5};
    tw::Prop<std::string> name{"probe"};
    tw::Prop<bool> flag{false};
    tw::Prop<Point> point;
    tw::Prop<std::vector<double>> values;
    tw::Prop<std::optional<std::int64_t>> maybe;
    tw::Prop<tw::NdArray<double, 2>> matrix{tw::NdArray<double, 2>::zeros({2, 2})};

    std::int64_t computed() const { return 1; }
    Point move(std::int64_t x, double y, std::optional<std::string> label) {
        return {x, y, std::move(label)};
    }
    std::string echo(std::string text) { return text; }
    double total(std::vector<double> numbers, double factor) {
        double sum = 0;
        for (const auto n : numbers) {
            sum += n;
        }
        return sum * factor;
    }
    std::int64_t trace(tw::NdArray<std::int64_t> array) { return static_cast<std::int64_t>(array.size()); }
    void fail() { throw std::runtime_error("failed"); }
    void noop() {}

    static void define(tw::ThingSpec<Probe>& t) {
        t.property("count", &Probe::count).ge(0).le(1000);
        t.property("ratio", &Probe::ratio).gt(0).lt(1);
        t.property("name", &Probe::name).min_length(1).max_length(8).pattern("^[a-z]+$");
        t.property("flag", &Probe::flag);
        t.property("point", &Probe::point);
        t.property("values", &Probe::values).setting();
        t.property("maybe", &Probe::maybe);
        t.property("matrix", &Probe::matrix);
        t.property("computed", &Probe::computed);
        t.action<"x", "y", "label">("move", &Probe::move).param("y").default_value(0.0);
        t.action<"text">("echo", &Probe::echo);
        t.action<"numbers", "factor">("total", &Probe::total).param("factor").default_value(1.0);
        t.action<"array">("trace", &Probe::trace);
        t.action("fail", &Probe::fail);
        t.action("noop", &Probe::noop);
    }
};

struct Route {
    const char* method;
    const char* path;
};

constexpr std::array<Route, 24> routes = {{
    {"PUT", "/probe/count"},      {"PUT", "/probe/ratio"},      {"PUT", "/probe/name"},
    {"PUT", "/probe/flag"},       {"PUT", "/probe/point"},      {"PUT", "/probe/values"},
    {"PUT", "/probe/maybe"},      {"PUT", "/probe/matrix"},     {"PUT", "/probe/computed"},
    {"PUT", "/probe/properties"}, {"POST", "/probe/move"},      {"POST", "/probe/echo"},
    {"POST", "/probe/total"},     {"POST", "/probe/trace"},     {"POST", "/probe/fail"},
    {"POST", "/probe/noop"},      {"POST", "/probe/count"},     {"PATCH", "/probe/count"},
    {"GET", "/probe/count"},      {"GET", "/probe/properties"}, {"DELETE", "/probe/move"},
    {"POST", "/probe/nothing"},   {"PUT", "/probe/"},           {"POST", "/action_invocations"},
}};

constexpr std::array<std::string_view, 8> content_types = {
    "application/json", "application/json; charset=utf-8",   "",
    "text/plain",       "application/x-www-form-urlencoded", "multipart/form-data; boundary=x",
    "APPLICATION/JSON", "application/merge-patch+json",
};

// One server per wire profile, for every input.
struct Fixture {
    explicit Fixture(tw::http::WireProfile profile)
        : server((tw::log::set_sink(nullptr),
                  tw::ThingServer::builder().thing<Probe>("probe").wire_profile(profile).build())),
          client(tw::testing::TestClient::start(server)) {}
    ~Fixture() { client.stop(); }

    tw::ThingServer server;
    tw::testing::TestClient client;
};

Fixture& fixture(bool wot) {
    static Fixture tetathing(tw::http::WireProfile::tetathing);
    static Fixture strict(tw::http::WireProfile::wot);
    return wot ? strict : tetathing;
}

bool is_json(std::string_view content_type) {
    return content_type.starts_with("application/json") ||
           content_type.starts_with("application/problem+json") ||
           content_type.starts_with("application/td+json") || content_type.starts_with("application/ld+json");
}

}  // namespace

void fuzz::one_input(std::string_view input) {
    if (input.empty()) {
        return;
    }
    const auto choice = static_cast<unsigned char>(input[0]);
    const auto& route = routes[choice % routes.size()];
    const auto content_type = content_types[(choice / routes.size()) % content_types.size()];
    const bool wot = (choice & 0x80) != 0;
    const std::string body(input.substr(1));
    std::vector<std::pair<std::string, std::string>> headers;
    if (!content_type.empty()) {
        headers.emplace_back("Content-Type", std::string(content_type));
    }
    const auto response = fixture(wot).client.request(route.method, route.path, headers, body);
    const auto shown = std::string(route.method) + " " + route.path + " (" + std::string(content_type) +
                       ", " + (wot ? "wot" : "tetathing") + ") answered " + std::to_string(response.status) +
                       " " + response.body;
    fuzz::check(response.status != 500, "an internal error: " + shown);
    fuzz::check(response.status >= 200 && response.status < 600, "not a status: " + shown);
    if (is_json(response.header("content-type").value_or("")) && !response.body.empty()) {
        try {
            (void)tw::json::parse(response.body);
        } catch (const tw::json::ParseError&) {
            throw fuzz::Failure("JSON that isn't: " + shown);
        }
    }
}
