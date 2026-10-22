// The binding's options, its route setup and stable Thing identifiers: a port
// of teta-wot-http's `tests/options.rs`, with the route table's and the
// problem renderer's unit tests (`routes.rs`, `problem.rs`).

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

#include "http/path_uuid.hpp"
#include "http/render.hpp"
#include "http/routes.hpp"
#include "http/sse.hpp"
#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
namespace http = teta::wot::http;
namespace detail = teta::wot::http::detail;
using tw::json::Value;

struct Empty {
    static void define(tw::ThingSpec<Empty>& t) { t.title("Empty"); }
};

http::HttpOptions options(std::string prefix) {
    http::HttpOptions out;
    out.api_prefix = std::move(prefix);
    out.server_id = "test-server";
    return out;
}

std::shared_ptr<tw::Runtime> runtime_with(const std::string& name = "") {
    tw::RuntimeBuilder builder;
    if (!name.empty()) {
        builder.thing<Empty>(name);
    }
    return std::make_shared<tw::Runtime>(builder.build());
}

TEST(HttpOptions, DefaultOptionsUseNoPrefixAndTheDefaultServerIdentity) {
    const http::HttpOptions defaults;
    EXPECT_TRUE(defaults.api_prefix.empty());
    EXPECT_EQ(defaults.server_id, http::default_server_id());
}

TEST(HttpOptions, ValidPrefixesBuildABinding) {
    for (const char* prefix : {"", "/api", "/api/v1", "/api_v1/test-server", "/1"}) {
        EXPECT_NO_THROW(http::HttpBinding(runtime_with(), options(prefix))) << prefix;
    }
}

TEST(HttpOptions, InvalidPrefixesAreReportedWithoutCrashing) {
    for (const char* prefix :
         {"api", "/", "/api/", "//api", "/api//v1", "/a.b", "/with space", "/api?x=1", "/api#fragment"}) {
        try {
            http::HttpBinding binding(runtime_with(), options(prefix));
            ADD_FAILURE() << prefix;
        } catch (const http::RouteError& error) {
            EXPECT_EQ(error.kind(), http::RouteError::Kind::invalid_prefix);
            EXPECT_EQ(error.subject(), prefix);
        }
    }
}

TEST(HttpOptions, ReservedNamesAreRejectedByTheBinding) {
    for (const auto name : http::reserved_thing_names) {
        try {
            http::HttpBinding binding(runtime_with(std::string(name)), options("/api"));
            ADD_FAILURE() << name;
        } catch (const http::RouteError& error) {
            EXPECT_EQ(error.kind(), http::RouteError::Kind::reserved_thing_name);
            EXPECT_EQ(error.subject(), name);
        }
    }
    for (const char* name : {"things_1", "my-docs", "counter"}) {
        EXPECT_NO_THROW(http::HttpBinding(runtime_with(name), options("")));
    }
}

TEST(HttpOptions, ThingIdsAreStableUuidV5UrnsAndDistinguishServersAndThings) {
    const auto id = tw::td::thing_id("server", "counter");
    EXPECT_EQ(id, tw::td::thing_id("server", "counter"));
    ASSERT_TRUE(id.starts_with("urn:uuid:"));
    const auto uuid = tw::Uuid::parse(id.substr(9));
    ASSERT_TRUE(uuid);
    EXPECT_EQ(uuid->bytes()[6] >> 4, 5);
    EXPECT_NE(id, tw::td::thing_id("other-server", "counter"));
    EXPECT_NE(id, tw::td::thing_id("server", "other-thing"));
}

// ---- routes.rs ----------------------------------------------------------------------------

detail::Routes table() {
    detail::Routes routes;
    routes.add("/t/p", "PUT", {detail::Kind::list_invocations, {}, {}});
    routes.add("/t/p", "GET", {detail::Kind::thing_paths, {}, {}});
    routes.add("/a/{id}", "GET", {detail::Kind::get_invocation, {}, {}});
    routes.add("/a/{id}/output", "GET", {detail::Kind::invocation_output, {}, {}});
    routes.add("/t/", "GET", {detail::Kind::thing_descriptions, {}, {}});
    return routes;
}

TEST(Routes, TheFirstFullMatchWins) {
    const auto routes = table();
    auto found = routes.find("GET", "/t/p");
    ASSERT_EQ(found.what, detail::Found::What::route);
    EXPECT_EQ(found.route->endpoint.kind, detail::Kind::thing_paths);
    found = routes.find("GET", "/a/xyz");
    ASSERT_EQ(found.what, detail::Found::What::route);
    EXPECT_EQ(found.param, "xyz");
    found = routes.find("GET", "/a/xyz/output");
    ASSERT_EQ(found.what, detail::Found::What::route);
    EXPECT_EQ(found.route->endpoint.kind, detail::Kind::invocation_output);
}

TEST(Routes, AWrongMethodReportsTheFirstRoute) {
    const auto routes = table();
    auto found = routes.find("DELETE", "/t/p");
    ASSERT_EQ(found.what, detail::Found::What::wrong_method);
    EXPECT_EQ(found.route->method, "PUT");
    found = routes.find("HEAD", "/t/");
    ASSERT_EQ(found.what, detail::Found::What::wrong_method);
    EXPECT_EQ(found.route->method, "GET");
}

TEST(Routes, TrailingSlashesMatter) {
    const auto routes = table();
    EXPECT_EQ(routes.find("GET", "/t").what, detail::Found::What::nothing);
    EXPECT_TRUE(routes.has_path("/t/"));
    EXPECT_EQ(routes.find("GET", "/a/").what, detail::Found::What::nothing)
        << "an empty parameter doesn't match";
}

// ---- problem.rs ---------------------------------------------------------------------------

TEST(Problems, TetathingProblemsGetTheFrameworksTypes) {
    const auto busy = tw::ProblemDetails::teta_wot_things("GlobalLockBusyError", "busy", 409);
    auto [status, body] = detail::wot_problem(busy);
    EXPECT_EQ(status, 503);
    EXPECT_EQ(tw::json::dump(body), tw::json::dump(Value{{"type", http::problem_type("global-lock-busy")},
                                                         {"title", "Global lock busy"},
                                                         {"status", 503},
                                                         {"detail", "busy"},
                                                         {"error", "GlobalLockBusyError"}}));
    std::tie(status, body) = detail::wot_problem(tw::ProblemDetails::untyped("DeviceError", "gone", 500));
    EXPECT_EQ(status, 500);
    EXPECT_EQ(body["type"], http::problem_type("failed"));
    EXPECT_EQ(body["error"], "DeviceError");
    EXPECT_FALSE(body.contains("instance")) << "no null members";
}

TEST(Problems, InvalidParamsPointIntoTheBody) {
    auto issue = [](tw::Loc loc) {
        return tw::ValidationIssue{"int_parsing", std::move(loc), "Input should be a valid integer", "x", {}};
    };
    EXPECT_EQ(
        tw::json::dump(detail::invalid_param(issue({"body", "a/b", std::size_t{2}}))),
        R"({"in":"body","name":"/a~1b/2","reason":"Input should be a valid integer","code":"int_parsing"})");
    EXPECT_EQ(detail::invalid_param(issue({"body"}))["name"], "");
    EXPECT_EQ(
        tw::json::dump(detail::invalid_param(issue({"query", "limit"}))),
        R"({"in":"query","name":"limit","reason":"Input should be a valid integer","code":"int_parsing"})");
}

// ---- observe.rs -----------------------------------------------------------------------------

TEST(Sse, AcceptHeaders) {
    auto accept = [](const char* value) {
        http::Headers headers;
        headers.add("Accept", value);
        return detail::wants_event_stream(headers);
    };
    EXPECT_TRUE(accept("text/event-stream"));
    EXPECT_TRUE(accept("application/json, Text/Event-Stream;q=0.5"));
    EXPECT_FALSE(accept("*/*"));
    EXPECT_FALSE(accept("application/json"));
    EXPECT_FALSE(detail::wants_event_stream(http::Headers{}));
}

}  // namespace
