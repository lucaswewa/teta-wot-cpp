// The OpenAPI document and the docs pages: teta-wot-http's `openapi.rs` and
// `docs.rs` tests (operation IDs, duplicates numbered, summaries, tuples as
// `prefixItems`, escaped titles, where the pages' assets come from), and the
// routes that serve them.

#include "teta/wot/http/openapi.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>

#include "http/docs.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
namespace http = teta::wot::http;
using tw::json::Value;
using tw::testing::TestClient;

namespace openapi_test {

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

// A Thing `a_b` with a property `c`, and a Thing `a` with `b_c`: FastAPI's
// rule gives both reads the same `operationId`.
struct AB {
    tw::Prop<std::int64_t> c{0};
    static void define(tw::ThingSpec<AB>& t) { t.property("c", &AB::c); }
};
struct A {
    tw::Prop<std::int64_t> b_c{0};
    static void define(tw::ThingSpec<A>& t) { t.property("b_c", &A::b_c); }
};

struct Probe {
    tw::Prop<std::tuple<std::int64_t, std::string>> pair{std::tuple<std::int64_t, std::string>{1, "a"}};
    tw::MjpegStream stream;

    tw::http::Response readings() { return tw::http::Response::text("x"); }
    std::int64_t twice(std::int64_t n) { return 2 * n; }
    void nothing() {}

    static void define(tw::ThingSpec<Probe>& t) {
        t.property("pair", &Probe::pair);
        t.stream("stream", &Probe::stream);
        t.action<"n">("twice", &Probe::twice).synchronous();
        t.action("nothing", &Probe::nothing).synchronous();
        t.endpoint("GET", "readings.csv", &Probe::readings).name("readings_csv").doc("The readings.");
    }
};

tw::Runtime runtime_of_probe() {
    return tw::Runtime::builder().thing("probe", std::make_shared<Probe>()).build();
}

}  // namespace openapi_test

using namespace openapi_test;

TEST(OpenApi, OperationIds) {
    EXPECT_EQ(http::operation_id("get_property", "/mything/foo", "GET"), "get_property_mything_foo_get");
    EXPECT_EQ(http::operation_id("things.mything", "/mything/", "GET"), "things_mything_mything__get");
    EXPECT_EQ(http::operation_id("list_all_invocations", "/action_invocations", "GET"),
              "list_all_invocations_action_invocations_get");
    EXPECT_EQ(http::operation_id("action_invocation_output", "/action_invocations/{id}/output", "GET"),
              "action_invocation_output_action_invocations__id__output_get");
    EXPECT_EQ(http::operation_id("get_property", "/api/v1/counter/counter", "GET"),
              "get_property_api_v1_counter_counter_get");
    EXPECT_EQ(http::operation_id("mjpeg_stream_response", "/blob_probe/stream", "GET"),
              "mjpeg_stream_response_blob_probe_stream_get");
    EXPECT_EQ(http::operation_id("x", "/caf\xc3\xa9", "PUT"), "x_caf\xc3\xa9_put")
        << "letters beyond ASCII are kept";
}

TEST(OpenApi, DuplicateOperationIdsAreNumbered) {
    const auto runtime =
        tw::Runtime::builder().thing("a_b", std::make_shared<AB>()).thing("a", std::make_shared<A>()).build();
    const auto document = http::openapi(runtime, {});
    EXPECT_EQ(document["paths"]["/a_b/c"]["get"]["operationId"], "get_property_a_b_c_get");
    EXPECT_EQ(document["paths"]["/a/b_c"]["get"]["operationId"], "get_property_a_b_c_get_2");
}

TEST(OpenApi, TheDocumentDescribesEveryRoute) {
    const auto runtime = runtime_of_probe();
    http::HttpOptions options;
    options.api_title = "Probe";
    options.api_version = "1.2.3";
    const auto document = http::openapi(runtime, options);
    EXPECT_EQ(document["openapi"], "3.1.0");
    EXPECT_SAME(document["info"], (Value{{"title", "Probe"}, {"version", "1.2.3"}}));
    EXPECT_FALSE(document.contains("servers"));
    const auto& paths = document["paths"];
    // The framework's routes, discovery's, then the Things': affordances,
    // streams and endpoints by name, the top-level resources, the TD.
    std::vector<std::string> names;
    for (const auto& [path, _] : paths.items()) {
        names.push_back(path);
    }
    EXPECT_EQ(names, (std::vector<std::string>{
                         "/action_invocations", "/action_invocations/{id}", "/action_invocations/{id}/output",
                         "/blob/{blob_id}", "/thing_descriptions/", "/things/", "/.well-known/wot",
                         "/directory/things", "/directory/things/{id}", "/probe/nothing", "/probe/pair",
                         "/probe/pair/reset", "/probe/readings.csv", "/probe/stream", "/probe/stream/viewer",
                         "/probe/twice", "/probe/properties", "/probe/actions", "/probe/"}));
    // An endpoint: its name's summary, its description.
    const auto& endpoint = paths["/probe/readings.csv"]["get"];
    EXPECT_EQ(endpoint["summary"], "Readings Csv");
    EXPECT_EQ(endpoint["description"], "The readings.");
    EXPECT_EQ(endpoint["operationId"], "readings_csv_probe_readings_csv_get");
    // A tuple's `items` array is `prefixItems`, last.
    const auto& pair = document["components"]["schemas"]["probe_pair_value"];
    EXPECT_FALSE(pair.contains("items"));
    EXPECT_EQ(pair.items().begin().key(), "type");
    EXPECT_EQ(std::prev(pair.end()).key(), "prefixItems");
    EXPECT_EQ(pair["prefixItems"][1]["type"], "string");
    // Without parameters, the body isn't required.
    EXPECT_FALSE(paths["/probe/nothing"]["post"]["requestBody"].contains("required"));
    EXPECT_EQ(paths["/probe/twice"]["post"]["requestBody"]["required"], true);
    // Only the `wot` profile's synchronous actions answer with their output.
    EXPECT_TRUE(paths["/probe/twice"]["post"]["responses"].contains("201"));
    options.profile = http::WireProfile::wot;
    const auto wot = http::openapi(runtime, options);
    const auto& twice = wot["paths"]["/probe/twice"]["post"]["responses"];
    EXPECT_SAME(twice["200"]["content"]["application/json"]["schema"],
                (Value{{"$ref", "#/components/schemas/probe_twice_output"}}));
    EXPECT_TRUE(twice.contains("500") && twice.contains("503") && twice.contains("400"));
    EXPECT_TRUE(wot["paths"]["/probe/nothing"]["post"]["responses"].contains("204"));
    EXPECT_TRUE(wot["components"]["schemas"].contains("probe_twice_status"));
}

TEST(OpenApi, TitlesAreEscaped) {
    EXPECT_EQ(http::detail::escape_html("<A & B>"), "&lt;A &amp; B&gt;");
    EXPECT_EQ(http::detail::escape_html("\"quoted\""), "&quot;quoted&quot;");
}

TEST(OpenApi, AssetsComeFromThePinnedCdnOrTheServer) {
    const auto page = http::detail::swagger_ui("API").body;
    if (http::docs_offline()) {
        EXPECT_NE(page.find("src=\"/docs/swagger-ui-bundle.js\""), std::string::npos);
        EXPECT_EQ(http::detail::docs_assets().size(), 4u);
    } else {
        EXPECT_NE(
            page.find("src=\"https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.33.0/swagger-ui-bundle.js\""),
            std::string::npos);
        EXPECT_TRUE(http::detail::docs_assets().empty());
    }
    EXPECT_NE(http::detail::redoc("API").body.find(http::docs_offline()
                                                       ? "/redoc/redoc.standalone.js"
                                                       : "https://cdn.jsdelivr.net/npm/redoc@2.5.4/bundles/"),
              std::string::npos);
}

TEST(OpenApi, TheDocumentAndPagesAreServedAtTheRoot) {
    auto server = tw::ThingServer::builder()
                      .thing("probe", std::make_shared<Probe>())
                      .api_prefix("/api")
                      .api_info("A <Probe>", "1.0")
                      .build();
    const auto client = TestClient::start(server);
    const auto document = client.get("/openapi.json");
    EXPECT_EQ(document.status, 200);
    EXPECT_EQ(document.header("content-type"), "application/json");
    EXPECT_EQ(document.body, tw::json::dump(http::openapi(*client.runtime(), server.binding()->options())));
    EXPECT_TRUE(document.json()["paths"].contains("/api/probe/pair"));
    EXPECT_EQ(client.get("/api/openapi.json").status, 404) << "at the root, as FastAPI serves it";
    const auto docs = client.get("/docs");
    EXPECT_EQ(docs.header("content-type"), "text/html; charset=utf-8");
    EXPECT_NE(docs.body.find("<title>A &lt;Probe&gt; - Swagger UI</title>"), std::string::npos);
    EXPECT_NE(client.get("/redoc").body.find("<title>A &lt;Probe&gt; - ReDoc</title>"), std::string::npos);
    const auto redirect = client.get("/docs/oauth2-redirect");
    EXPECT_TRUE(redirect.body.starts_with("<!doctype html>\n<html lang=\"en-US\">"));
    EXPECT_NE(redirect.body.find("<script>\n\"use strict\";function run()"), std::string::npos)
        << "the script, inline";
    // `HEAD` too, with a `GET`'s headers; other methods are refused.
    const auto head = client.request("HEAD", "/openapi.json");
    EXPECT_EQ(head.status, 200);
    EXPECT_EQ(head.header("content-length"), std::to_string(document.body.size()));
    EXPECT_TRUE(head.body.empty());
    const auto post = client.post_json("/docs");
    EXPECT_EQ(post.status, 405);
    EXPECT_EQ(post.header("allow"), "GET, HEAD");
    EXPECT_EQ(client.get("/docs/").status, 307);
    client.stop();
}

TEST(OpenApi, OfflineAssetsAreServedWithACacheTime) {
    if (!http::docs_offline()) {
        GTEST_SKIP() << "built without TETA_WOT_DOCS_OFFLINE";
    }
    auto server = tw::ThingServer::builder().thing("probe", std::make_shared<Probe>()).build();
    const auto client = TestClient::start(server);
    for (const auto& asset : http::detail::docs_assets()) {
        const auto response = client.get(asset.path);
        EXPECT_EQ(response.status, 200) << asset.path;
        EXPECT_EQ(response.header("content-type"), std::string(asset.content_type));
        EXPECT_EQ(response.header("cache-control"), "public, max-age=86400");
        EXPECT_EQ(response.body.size(), asset.data.size());
    }
    client.stop();
}
