// A Thing that requires a bearer token.
//
// Every interaction needs `Authorization: Bearer <token>`: reading and
// writing properties, invoking and following actions, observing. The
// descriptions stay public, so that a client can find out what to send: the
// TD's `securityDefinitions` and `security` say "bearer token in the
// `Authorization` header", and so does the OpenAPI document (Swagger UI shows
// an "Authorize" button).
//
// The token comes from the `WOT_TOKEN` environment variable, or is made up and
// printed. In a configuration file, the same is
// `"security": {"scheme": "bearer", "token_env": "WOT_TOKEN"}`: secrets are
// never read from the file.
//
//     example-bearer-security                     # the answers, in-process
//     example-bearer-security --serve             # serve on port 8000 (or --port), for curl
//
// The C++ port of teta-wot's `bearer-security`.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

/// A safe with a combination.
class Safe {
  public:
    /// Whether the door is open.
    tw::Prop<bool> open{false};

    /// Close and lock the door.
    void lock() { open.set(false); }

    static void define(tw::ThingSpec<Safe>& t) {
        t.doc("A safe with a combination.");
        t.property("open", &Safe::open).doc("Whether the door is open.");
        t.action("lock", &Safe::lock).doc("Close and lock the door.").synchronous();
    }
};

tw::ThingServer server(const std::string& token) {
    return tw::ThingServer::builder().security(tw::http::Security::bearer(token)).thing<Safe>("safe").build();
}

void ensure(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

std::string environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    std::string out;
    if (_dupenv_s(&value, &size, name) == 0 && value) {
        out = value;
        std::free(value);
    }
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void demo(const std::string& token) {
    auto client = tw::testing::TestClient::start(server(token));

    // Without the token: 401, with a challenge.
    auto r = client.get("/safe/open");
    std::cout << "GET /safe/open, no token\n"
              << "  " << r.status << " www-authenticate: " << r.header("www-authenticate").value_or("-")
              << "  " << r.text() << "\n";
    ensure(r.status == 401, "reads need the token");
    ensure(client.put_json("/safe/open", true).status == 401, "writes need the token too");

    // The TD is public, and says what to send.
    const auto td = client.get("/safe/").json();
    std::cout << "GET /safe/ (public)\n"
              << "  security: " << tw::json::dump(td["security"]) << "\n"
              << "  securityDefinitions: " << tw::json::dump(td["securityDefinitions"]) << "\n";
    ensure(td["securityDefinitions"]["bearer_sc"]["scheme"] == "bearer", "the TD names the scheme");
    ensure(td["securityDefinitions"]["bearer_sc"]["in"] == "header", "and where the token goes");
    const auto openapi = client.get("/openapi.json").json();
    std::cout << "GET /openapi.json (public): securitySchemes "
              << tw::json::dump(openapi["components"]["securitySchemes"]) << "\n";

    // A wrong token is refused; the right one works.
    ensure(client.with_header("authorization", "Bearer not-the-token").get("/safe/open").status == 401,
           "a wrong token is refused");
    const auto authorized =
        tw::testing::TestClient::start(server(token)).with_header("authorization", "Bearer " + token);
    r = authorized.put_json("/safe/open", true);
    std::cout << "PUT /safe/open true, with the token: " << r.status << "\n";
    ensure(r.status >= 200 && r.status < 300, "the token opens it");
    r = authorized.post_json("/safe/lock");
    std::cout << "POST /safe/lock, with the token: " << r.status << "\n";
    ensure(r.status >= 200 && r.status < 300, "and locks it");

    // The same from a configuration file: the token's variable, not the token.
    const auto config = tw::ServerConfig::from_value(
        Value{{"things", Value::object()}, {"security", {{"scheme", "bearer"}, {"token_env", "WOT_TOKEN"}}}});
    ensure(config.security && config.security->scheme == tw::SecurityConfig::Scheme::bearer &&
               config.security->token_env == "WOT_TOKEN",
           "the configuration names the variable");
    std::cout << "\nA configuration file names the variable: bearer, token_env " << config.security->token_env
              << "\n";
    client.stop();
    authorized.stop();
}

int main(int argc, char** argv) {
    try {
        std::string token = environment("WOT_TOKEN");
        if (token.empty()) {
            token = tw::Uuid::v4().to_string();
            std::erase(token, '-');
        }
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 8000;
            if (argc > 3 && std::string_view(argv[2]) == "--port") {
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server(token).serve("127.0.0.1", port, tw::shutdown_signal(), [&](std::uint16_t bound) {
                std::cout << "serving on http://127.0.0.1:" << bound << "/safe/\n"
                          << "  curl -H \"Authorization: Bearer " << token << "\" http://127.0.0.1:" << bound
                          << "/safe/open" << std::endl;
            });
            return EXIT_SUCCESS;
        }
        demo(token);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
