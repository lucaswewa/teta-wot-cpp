// The interactive API docs with their assets compiled in: built with
// `TETA_WOT_DOCS_OFFLINE=ON`, Swagger UI (`/docs`) and ReDoc (`/redoc`) load
// nothing from the internet, so they work on a lab network without it.
//
//     example-docs-offline                        # an in-process check
//     example-docs-offline --serve                # serve on http://127.0.0.1:5000
//     example-docs-offline --serve --port 0       # any free port
//
// While it serves, open http://127.0.0.1:5000/docs.
// The C++ port of teta-wot's `docs-offline`.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <vector>

namespace tw = teta::wot;

/// The exit code CTest reads as "skipped": built without the option.
constexpr int skipped = 77;

/// A dimmable lamp.
class Lamp {
  public:
    /// The brightness, in percent.
    tw::Prop<std::uint8_t> brightness{50};

    /// Switch the lamp off.
    std::uint8_t switch_off() {
        brightness.set(0);
        return 0;
    }

    static void define(tw::ThingSpec<Lamp>& t) {
        t.doc("A dimmable lamp.");
        t.property("brightness", &Lamp::brightness).doc("The brightness, in percent.").ge(0).le(100);
        t.action("switch_off", &Lamp::switch_off).doc("Switch the lamp off.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Lamp>("lamp").api_info("Lamp", "1.0.0").build();
}

// The URLs in quotes that leave the server.
std::vector<std::string> external_urls(const std::string& html) {
    std::vector<std::string> found;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= html.size(); ++i) {
        if (i == html.size() || html[i] == '"' || html[i] == '\'') {
            const std::string_view piece(html.data() + start, i - start);
            if (piece.starts_with("http://") || piece.starts_with("https://")) {
                found.emplace_back(piece);
            }
            start = i + 1;
        }
    }
    return found;
}

int demo() {
    // The in-process check: the pages refer only to the server.
    if (!tw::http::docs_offline()) {
        std::cout << "built without TETA_WOT_DOCS_OFFLINE: the pages load their assets from jsDelivr\n";
        return skipped;
    }
    std::cout << "Swagger UI " << tw::http::swagger_ui_version << " and ReDoc " << tw::http::redoc_version
              << ", compiled in\n";
    const auto client = tw::testing::TestClient::start(server());
    for (const char* page : {"/docs", "/redoc", "/docs/oauth2-redirect"}) {
        const auto html = client.get(page).text();
        const auto external = external_urls(html);
        std::cout << "GET " << page << " → " << html.size() << " bytes, external URLs: [";
        for (std::size_t i = 0; i < external.size(); ++i) {
            std::cout << (i ? ", " : "") << tw::json::dump(external[i]);
        }
        std::cout << "]\n";
        if (!external.empty()) {
            throw std::runtime_error(std::string(page) + " loads something from outside");
        }
    }
    for (const char* asset : {"/docs/swagger-ui-bundle.js", "/docs/swagger-ui.css", "/docs/favicon-32x32.png",
                              "/redoc/redoc.standalone.js"}) {
        const auto response = client.get(asset);
        std::cout << "GET " << asset << " → " << response.status << " "
                  << tw::http::reason_phrase(response.status) << " "
                  << response.header("content-type").value_or("-") << ", " << response.body.size()
                  << " bytes\n";
        if (response.status != 200) {
            throw std::runtime_error(std::string(asset) + " isn't served");
        }
    }
    client.stop();
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 5000;
            if (argc > 2 && std::string_view(argv[2]) == "--port") {
                if (argc < 4) {
                    throw std::invalid_argument("--port needs a value");
                }
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server().serve("127.0.0.1", port, tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "listening on http://127.0.0.1:" << bound << "\n"
                          << "docs at http://127.0.0.1:" << bound << "/docs and http://127.0.0.1:" << bound
                          << "/redoc" << std::endl;
            });
            return EXIT_SUCCESS;
        }
        return demo();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
