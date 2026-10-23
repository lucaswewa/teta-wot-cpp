// Discovery and security: teta-wot-http's `discovery.rs` and `security.rs`
// tests (the listing sorted and paged, the collection format, bad arguments,
// retrieval, base64, credentials, the challenge), and both through
// `TestClient`: public descriptions, refused interactions and WebSockets,
// the TDs' and the OpenAPI document's security.

#include "http/discovery.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "http/security.hpp"
#include "teta/wot.hpp"
#include "teta/wot/td/validation.hpp"

namespace tw = teta::wot;
namespace http = teta::wot::http;
using tw::json::Value;
using tw::testing::TestClient;

namespace discovery_test {

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

std::vector<Value> tds() {
    return {Value{{"id", "urn:c"}}, Value{{"id", "urn:a"}}, Value{{"id", "urn:b"}}};
}

const http::detail::Urls urls{"http://host", "/api"};

std::vector<std::string> links(const http::Response& response) {
    std::vector<std::string> out;
    for (const auto link : response.headers.get_all("link")) {
        out.emplace_back(link);
    }
    return out;
}

http::Response listing(std::string query) {
    return http::detail::listing(tds(), query.empty() ? std::nullopt : std::optional(query), urls);
}

struct Lamp {
    tw::Prop<std::int64_t> level{50};
    tw::Event<std::int64_t> changed;
    static void define(tw::ThingSpec<Lamp>& t) {
        t.property("level", &Lamp::level);
        t.event("changed", &Lamp::changed);
    }
};

TestClient secured(http::Security security, http::WireProfile profile = http::WireProfile::tetathing) {
    auto server = tw::ThingServer::builder()
                      .thing("lamp", std::make_shared<Lamp>())
                      .server_id("test")
                      .wire_profile(profile)
                      .security(std::move(security))
                      .build();
    return TestClient::start(server);
}

}  // namespace discovery_test

using namespace discovery_test;

TEST(Discovery, TheListingIsSortedAndPaged) {
    const auto all = listing("");
    EXPECT_EQ(all.headers.get("content-type"), "application/ld+json");
    EXPECT_TRUE(links(all).empty()) << "not paged";
    EXPECT_EQ(all.body, R"([{"id":"urn:a"},{"id":"urn:b"},{"id":"urn:c"}])");

    const auto first = listing("limit=2");
    const auto first_links = links(first);
    ASSERT_EQ(first_links.size(), 2u);
    EXPECT_EQ(first_links[0], R"(<http://host/api/directory/things?offset=2&limit=2>; rel="next")");
    EXPECT_TRUE(first_links[1].starts_with(R"(<http://host/api/directory/things>; rel="canonical"; etag=")"));
    EXPECT_EQ(first.body, R"([{"id":"urn:a"},{"id":"urn:b"}])");

    const auto last = listing("offset=2&limit=2");
    EXPECT_EQ(links(last).size(), 1u) << "no next link on the last page";
    EXPECT_EQ(last.body, R"([{"id":"urn:c"}])");
    EXPECT_EQ(links(last)[0], first_links[1]) << "the same collection, the same etag";
}

TEST(Discovery, TheCollectionFormat) {
    const auto body = tw::json::parse(listing("limit=1&format=collection").body);
    EXPECT_EQ(body["@type"], "ThingCollection");
    EXPECT_EQ(body["total"], 3);
    EXPECT_SAME(body["members"], Value::array({Value{{"id", "urn:a"}}}));
    EXPECT_EQ(body["next"], "http://host/api/directory/things?offset=1&limit=1&format=collection");
    EXPECT_EQ(body["@id"], "http://host/api/directory/things?offset=0&limit=1&format=collection");
}

TEST(Discovery, BadArgumentsAre400) {
    for (const char* query : {"limit=0", "limit=x", "offset=-1", "format=xml", "limit=", "limit=+1",
                              "offset=99999999999999999999999"}) {
        const auto response = listing(query);
        EXPECT_EQ(response.status, 400) << query;
        EXPECT_EQ(response.headers.get("content-type"), "application/problem+json") << query;
    }
    EXPECT_EQ(listing("offset=%2B1").status, 200) << "`+1` is a number, as Rust reads it";
    EXPECT_EQ(listing("offset=2&offset=0").body, R"([{"id":"urn:a"},{"id":"urn:b"},{"id":"urn:c"}])")
        << "the last of a repeated argument";
}

TEST(Discovery, RetrievalDecodesTheId) {
    const auto found = http::detail::retrieve(tds(), "urn%3Ab");
    EXPECT_EQ(found.headers.get("content-type"), "application/td+json");
    EXPECT_EQ(found.body, R"({"id":"urn:b"})");
    const auto missing = http::detail::retrieve(tds(), "urn:x");
    EXPECT_EQ(missing.status, 404);
    EXPECT_EQ(missing.headers.get("content-type"), "application/problem+json");
    EXPECT_EQ(http::detail::percent_decode("%+3A%zz%"),
              "\x03"
              "A%zz%")
        << "Rust's from_str_radix takes a sign";
    EXPECT_EQ(http::detail::percent_decode("%FF%C3%A9%C3"), "\xef\xbf\xbd\xc3\xa9\xef\xbf\xbd")
        << "lossy UTF-8";
}

TEST(Discovery, TheDirectoryTd) {
    http::HttpOptions options;
    options.api_title = "Lab";
    options.server_id = "test";
    const auto td = http::detail::directory_td(options, urls);
    EXPECT_EQ(td["base"], "http://host/api/directory/");
    EXPECT_EQ(td["@type"], "ThingDirectory");
    EXPECT_EQ(td["id"], tw::td::thing_id("test", "directory"));
    EXPECT_EQ(td["description"], "The Things served by Lab: a read-only Thing Description Directory.");
    EXPECT_NO_THROW(tw::td::validation::validate_json(td)) << "valid against the W3C TD schema";
}

TEST(Security, Base64MatchesRfc4648) {
    for (const auto& [input, output] :
         std::vector<std::pair<std::string, std::string>>{{"", ""},
                                                          {"f", "Zg=="},
                                                          {"fo", "Zm8="},
                                                          {"foo", "Zm9v"},
                                                          {"foob", "Zm9vYg=="},
                                                          {"fooba", "Zm9vYmE="},
                                                          {"foobar", "Zm9vYmFy"}}) {
        EXPECT_EQ(http::detail::base64(input), output);
    }
}

TEST(Security, CredentialsAreChecked) {
    const auto headers = [](std::string value) {
        http::Headers h;
        h.add("authorization", std::move(value));
        return h;
    };
    const auto basic = http::Security::basic("Aladdin", "open sesame");
    EXPECT_TRUE(http::detail::authorized(basic, headers("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==")));
    EXPECT_TRUE(http::detail::authorized(basic, headers("basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==")));
    EXPECT_FALSE(http::detail::authorized(basic, headers("Basic QWxhZGRpbjpvcGVuIHNlc2FtZR==")));
    EXPECT_FALSE(http::detail::authorized(basic, {}));
    const auto bearer = http::Security::bearer("s3cret");
    EXPECT_TRUE(http::detail::authorized(bearer, headers("Bearer s3cret")));
    EXPECT_FALSE(http::detail::authorized(bearer, headers("Bearer s3cre")));
    EXPECT_FALSE(http::detail::authorized(bearer, headers("Basic s3cret")));
    http::Headers two;
    two.add("authorization", "Basic nope").add("authorization", "Bearer s3cret");
    EXPECT_TRUE(http::detail::authorized(bearer, two)) << "any of them";
}

TEST(Security, A401Challenges) {
    const auto response = http::detail::unauthorized(http::Security::bearer("x"), http::WireProfile::wot);
    EXPECT_EQ(response.status, 401);
    EXPECT_EQ(response.headers.get("www-authenticate"), "Bearer realm=\"teta-wot\"");
    EXPECT_EQ(response.headers.get("content-type"), "application/problem+json");
    EXPECT_EQ(http::detail::unauthorized(http::Security::basic("a", "b"), http::WireProfile::tetathing)
                  .headers.get("www-authenticate"),
              "Basic realm=\"teta-wot\", charset=\"UTF-8\"");
}

TEST(Security, DescriptionsArePublicAndInteractionsNeedCredentials) {
    const auto client = secured(http::Security::basic("lab", "pw"));
    const auto td = client.get("/lamp/").json();
    EXPECT_SAME(td["securityDefinitions"]["basic_sc"],
                (Value{{"description", "HTTP Basic authentication (RFC 7617)"},
                       {"scheme", "basic"},
                       {"in", "header"},
                       {"name", "Authorization"}}));
    EXPECT_EQ(td["security"], "basic_sc");
    for (const char* path : {"/things/", "/thing_descriptions/", "/openapi.json", "/docs", "/.well-known/wot",
                             "/directory/things"}) {
        EXPECT_EQ(client.get(path).status, 200) << path;
    }
    const auto refused = client.get("/lamp/level");
    EXPECT_EQ(refused.status, 401);
    EXPECT_EQ(refused.json()["detail"], "Not authenticated");
    const auto authorized = client.with_header("Authorization", "Basic " + http::detail::base64("lab:pw"));
    EXPECT_EQ(authorized.get("/lamp/level").json(), 50);
    EXPECT_EQ(client.get("/lamp/changed").status, 401) << "events too";
    EXPECT_THROW((void)client.websocket("/lamp/ws"), std::runtime_error) << "no upgrade without credentials";
    auto socket = authorized.websocket("/lamp/ws");
    socket.close();
    client.stop();
}

TEST(Security, TheOpenApiDocumentDescribesTheScheme) {
    const auto client = secured(http::Security::bearer("t"), http::WireProfile::wot);
    const auto document = client.get("/openapi.json").json();
    EXPECT_SAME(document["components"]["securitySchemes"],
                (Value{{"bearer", Value{{"type", "http"}, {"scheme", "bearer"}}}}));
    const auto& read = document["paths"]["/lamp/level"]["get"];
    EXPECT_SAME(read["security"], Value::array({Value{{"bearer", Value::array()}}}));
    EXPECT_EQ(read["responses"]["401"]["description"], "Not authenticated");
    for (const char* path : {"/lamp/", "/things/", "/.well-known/wot", "/directory/things"}) {
        EXPECT_FALSE(document["paths"][path]["get"].contains("security")) << path;
    }
    EXPECT_EQ(client.get("/lamp/").json()["securityDefinitions"]["bearer_sc"]["format"], "opaque");
    client.stop();
}
