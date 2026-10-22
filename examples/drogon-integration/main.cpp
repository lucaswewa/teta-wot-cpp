// Serving Things inside an application that already uses Drogon.
//
//     example-drogon-integration            # a short demo on a free port
//     example-drogon-integration --serve    # serve on http://127.0.0.1:5000
//
// The application keeps its own Drogon: its routes, its listener, its event
// loops and its shutdown. `tw::drogon_adapter::mount` makes the Things'
// HTTP binding Drogon's default handler, so it answers every request the
// application's routes don't. The application starts the Things before
// `run()` and shuts them down after.

#include <drogon/HttpClient.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <teta/wot/drogon.hpp>
#include <thread>

namespace tw = teta::wot;
using namespace std::chrono_literals;

/// A counter.
class Counter {
  public:
    tw::Prop<std::int64_t> count{0};

    std::int64_t increment() {
        count.update([](std::int64_t& c) { ++c; });
        return count.get();
    }

    static void define(tw::ThingSpec<Counter>& t) {
        t.doc("A counter.");
        t.property("count", &Counter::count).read_only().doc("The count.");
        t.action("increment", &Counter::increment).doc("Add one to the count.");
    }
};

// The application's own route, as it had before it served Things.
void hello(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    response->setBody("Hello from the application.");
    callback(response);
}

// GET or POST on the application's server, with Drogon's client.
drogon::HttpResponsePtr send(const drogon::HttpClientPtr& client, drogon::HttpMethod method,
                             const std::string& path) {
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(method);
    request->setPath(path);
    const auto [result, response] = client->sendRequest(request, 5.0);
    if (result != drogon::ReqResult::Ok) {
        throw std::runtime_error("the request failed: " + path);
    }
    return response;
}

bool demo(std::uint16_t port) {
    const auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:" + std::to_string(port));
    bool ok = true;
    auto show = [&](drogon::HttpMethod method, const std::string& path, int expected) {
        const auto response = send(client, method, path);
        std::cout << (method == drogon::Post ? "POST " : "GET ") << path << " → "
                  << static_cast<int>(response->statusCode()) << " " << response->body() << "\n";
        ok = ok && static_cast<int>(response->statusCode()) == expected;
        return response;
    };
    show(drogon::Get, "/hello", 200);
    show(drogon::Get, "/counter/count", 200);
    const auto invocation = show(drogon::Post, "/counter/increment", 201);
    std::this_thread::sleep_for(100ms);
    show(drogon::Get, "/counter/count", 200);
    show(drogon::Get, "/things/", 200);
    show(drogon::Get, "/nothing", 404);
    return ok && send(client, drogon::Get, "/counter/count")->body() == "1";
}

int main(int argc, char** argv) {
    const bool serve = argc > 1 && std::string_view(argv[1]) == "--serve";
    try {
        // The Things and their HTTP binding.
        auto runtime =
            std::make_shared<tw::Runtime>(tw::Runtime::builder().thing<Counter>("counter").build());
        auto binding = std::make_shared<tw::http::HttpBinding>(runtime, tw::http::HttpOptions{});

        auto& app = drogon::app();
        app.registerHandler("/hello", &hello, {drogon::Get});
        tw::drogon_adapter::mount(binding, app);
        app.setLogLevel(trantor::Logger::kWarn).addListener("127.0.0.1", serve ? 5000 : 0).setThreadNum(2);

        bool ok = true;
        std::jthread client;
        app.registerBeginningAdvice([&] {
            const auto port = app.getListeners().front().toPort();
            std::cout << "listening on http://127.0.0.1:" << port << std::endl;
            if (!serve) {
                client = std::jthread([&ok, port] {
                    try {
                        ok = demo(port);
                    } catch (const std::exception& error) {
                        std::cerr << "error: " << error.what() << "\n";
                        ok = false;
                    }
                    drogon::app().quit();
                });
            }
        });

        runtime->start();
        app.run();  // until Ctrl-C (Drogon's own handler), or the demo quits
        if (client.joinable()) {
            client.join();
        }
        runtime->shutdown(5s);
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
