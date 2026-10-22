// The fallback server's page and answers. conformance/scenarios/cli compares
// them, served, with the reference's.

#include <gtest/gtest.h>

#include <string>

#include "teta/wot.hpp"

namespace tw = teta::wot;
namespace http = teta::wot::http;

TEST(Fallback, TextIsEscaped) {
    EXPECT_EQ(http::escape_html("<script>alert('x') & \"y\"</script>"),
              "&lt;script&gt;alert(&#x27;x&#x27;) &amp; &quot;y&quot;&lt;/script&gt;");
}

TEST(Fallback, ThePageShowsWhatItIsGiven) {
    http::FallbackPage page;
    page.error_message = "Failed to enter 'stage' Thing: <no stage>";
    page.things = {"counter", "stage"};
    page.config = R"({"things": {}})";
    page.traceback = "no stage";
    auto html = page.html();
    EXPECT_NE(
        html.find("<div class=\"error\">Failed to enter &#x27;stage&#x27; Thing: &lt;no stage&gt;</div>\n"),
        std::string::npos);
    EXPECT_NE(
        html.find(
            "<h2>The following Things loaded successfully:</h2>\n<ul>\n<li>counter</li>\n<li>stage</li>\n"
            "</ul>\n"),
        std::string::npos);
    EXPECT_NE(html.find("<pre>{&quot;things&quot;: {}}</pre>"), std::string::npos);
    EXPECT_NE(html.find("<p class=\"muted\">No logging information available.</p>"), std::string::npos);
    EXPECT_TRUE(html.starts_with("<!DOCTYPE html>\n<html lang=\"en\">\n"));
    EXPECT_TRUE(
        html.ends_with("<footer class=\"muted\">teta-wot fallback server</footer>\n</body>\n</html>\n"));

    page.things.clear();
    page.error_message.clear();
    page.logging = "a log line";
    html = page.html();
    EXPECT_EQ(html.find("loaded successfully"), std::string::npos);
    EXPECT_EQ(html.find("class=\"error\""), std::string::npos);
    EXPECT_NE(html.find("<h2>Logging</h2>\n<pre>a log line</pre>\n"), std::string::npos);
}

TEST(Fallback, ItAnswersThePageItsNameOrARedirect) {
    const std::string html = "<p>page</p>";
    auto answer = http::fallback_answer(html, http::Request::make("GET", "/"));
    EXPECT_EQ(answer.status, 500);
    EXPECT_EQ(answer.headers.get("content-type"), "text/html; charset=utf-8");
    EXPECT_EQ(answer.body, html);
    EXPECT_EQ(answer.headers.get("vary"), "Origin");

    answer = http::fallback_answer(html, http::Request::make("POST", "/fallback"));
    EXPECT_EQ(answer.status, 200);
    EXPECT_EQ(answer.headers.get("content-type"), "application/json");
    EXPECT_EQ(answer.body, "true");

    answer = http::fallback_answer(html, http::Request::make("GET", "/counter/count?x=1"));
    EXPECT_EQ(answer.status, 307);
    EXPECT_EQ(answer.headers.get("location"), "/");
    EXPECT_FALSE(answer.headers.get("content-type"));
    EXPECT_EQ(answer.body, "");
}

TEST(Fallback, CorsIsTheBindings) {
    auto request = http::Request::make("GET", "/fallback");
    request.headers.add("origin", "http://example.com");
    auto answer = http::fallback_answer("", request);
    EXPECT_EQ(answer.headers.get("access-control-allow-origin"), "http://example.com");
    EXPECT_EQ(answer.headers.get("access-control-allow-credentials"), "true");

    request = http::Request::make("OPTIONS", "/anything");
    request.headers.add("origin", "http://example.com");
    request.headers.add("access-control-request-method", "PUT");
    answer = http::fallback_answer("", request);
    EXPECT_EQ(answer.status, 200);
    EXPECT_EQ(answer.body, "OK");
    EXPECT_EQ(answer.headers.get("access-control-max-age"), "600");
}
