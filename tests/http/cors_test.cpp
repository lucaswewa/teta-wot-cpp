// CORS through the binding: a port of teta-wot-http's `tests/cors.rs`.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
using tw::testing::TestClient;
using tw::testing::TestResponse;

TestResponse request(const std::string& method, const std::string& path,
                     const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    const auto server = tw::ThingServer::builder().build();
    return TestClient::start(server).request(method, path, headers);
}

TEST(Cors, ResponsesWithoutOriginVaryByOriginWithoutCorsCredentials) {
    for (const auto& [path, status] :
         std::vector<std::pair<std::string, int>>{{"/things/", 200}, {"/unknown", 404}}) {
        const auto response = request("GET", path);
        EXPECT_EQ(response.status, status);
        EXPECT_EQ(response.header("vary"), "Origin");
        EXPECT_FALSE(response.header("access-control-allow-origin"));
        EXPECT_FALSE(response.header("access-control-allow-credentials"));
    }
}

TEST(Cors, OriginAndCredentialsAreMirroredOnSuccessErrorsAndRedirects) {
    for (const auto& [path, status] :
         std::vector<std::pair<std::string, int>>{{"/things/", 200}, {"/unknown", 404}, {"/things", 307}}) {
        const auto response = request("GET", path, {{"origin", "https://client.example"}});
        EXPECT_EQ(response.status, status);
        EXPECT_EQ(response.header("access-control-allow-origin"), "https://client.example");
        EXPECT_EQ(response.header("access-control-allow-credentials"), "true");
        EXPECT_EQ(response.header("vary"), "Origin");
    }
}

TEST(Cors, PreflightOnUnknownPathsAllowsMethodsAndEchoesRequestedHeaders) {
    const auto response = request("OPTIONS", "/not-a-route",
                                  {{"origin", "https://client.example"},
                                   {"access-control-request-method", "PUT"},
                                   {"access-control-request-headers", "X-Custom, Content-Type"}});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.header("content-type"), "text/plain; charset=utf-8");
    EXPECT_EQ(response.header("access-control-allow-origin"), "https://client.example");
    EXPECT_EQ(response.header("access-control-allow-credentials"), "true");
    EXPECT_EQ(response.header("access-control-allow-headers"), "X-Custom, Content-Type");
    EXPECT_EQ(response.header("access-control-allow-methods"),
              "DELETE, GET, HEAD, OPTIONS, PATCH, POST, PUT, QUERY");
    EXPECT_EQ(response.header("access-control-max-age"), "600");
    EXPECT_EQ(response.header("vary"),
              "Origin, Access-Control-Request-Method, Access-Control-Request-Headers, "
              "Access-Control-Request-Private-Network");
    EXPECT_EQ(response.body, "OK");
}

TEST(Cors, PreflightWithoutRequestedHeadersOmitsAllowHeaders) {
    const auto response =
        request("OPTIONS", "/things/", {{"origin", "null"}, {"access-control-request-method", "GET"}});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.header("access-control-allow-origin"), "null");
    EXPECT_FALSE(response.header("access-control-allow-headers"));
}

TEST(Cors, OptionsWithoutBothPreflightHeadersUsesNormalDispatch) {
    for (const auto& headers : std::vector<std::vector<std::pair<std::string, std::string>>>{
             {}, {{"origin", "https://client.example"}}, {{"access-control-request-method", "GET"}}}) {
        const auto response = request("OPTIONS", "/things/", headers);
        EXPECT_EQ(response.status, 405);
        EXPECT_EQ(response.header("allow"), "GET");
        EXPECT_FALSE(response.header("access-control-allow-methods"));
    }
}

}  // namespace
