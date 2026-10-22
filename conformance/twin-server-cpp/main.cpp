// The C++ twin server: the twins (conformance/twins) served with teta-wot's
// command line, so that conformance/run.py runs its scenarios against it as
// against the reference server (ADR-0011):
//
//     twin-server -c config.json [--host 127.0.0.1] [--port 5000]
//
// Until Phase 5 brings the registry, the configuration model and
// `serve_from_cli`, it reads what the scenarios use (`twins::server_builder`):
// `things`, `server_id`, `api_prefix`, `enable_global_lock` and
// `wire_profile`. Exit codes are teta-wot's: 2 for a wrong command line,
// 3 for an invalid configuration or a Thing that can't start, 1 otherwise.

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

#include "twins.hpp"

namespace tw = teta::wot;

namespace {

struct Arguments {
    std::string config;
    std::string host = "127.0.0.1";
    std::uint16_t port = 5000;
};

int usage(std::string_view problem) {
    std::cerr << "error: " << problem
              << "\n\nUsage: twin-server -c <CONFIG> [--host <HOST>] [--port <PORT>]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Arguments arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) {
            return usage("a value is required for '" + std::string(flag) + "'");
        }
        const std::string value = argv[++i];
        if (flag == "-c" || flag == "--config") {
            arguments.config = value;
        } else if (flag == "--host") {
            arguments.host = value;
        } else if (flag == "--port") {
            try {
                arguments.port = static_cast<std::uint16_t>(std::stoul(value));
            } catch (const std::exception&) {
                return usage("invalid value '" + value + "' for '--port'");
            }
        } else {
            return usage("unexpected argument '" + std::string(flag) + "'");
        }
    }
    if (arguments.config.empty()) {
        return usage("a configuration is required: -c <CONFIG>");
    }

    tw::log::init(false);
    std::ifstream file(arguments.config, std::ios::binary);
    if (!file) {
        std::cerr << "error: can't read " << arguments.config << "\n";
        return 1;
    }
    tw::json::Value config;
    try {
        config = tw::json::Value::parse(file);
    } catch (const std::exception& error) {
        std::cerr << "error: the configuration isn't JSON: " << error.what() << "\n";
        return 3;
    }

    std::optional<tw::ThingServerBuilder> builder;
    try {
        builder = twins::server_builder(config);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 3;
    }

    try {
        auto server = builder->build();
        server.serve(arguments.host, arguments.port);
    } catch (const tw::StartupError&) {
        return 3;  // logged by `serve`
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
