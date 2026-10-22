#include "teta/wot/cli.hpp"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <variant>

#include "teta/wot/log.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h.
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace teta::wot::cli {

namespace {

// ---- Parsing, as clap parses teta-wot's `CliArgs` ------------------------------------

constexpr const char* about = "Serve Things over HTTP, from a configuration file.";

// What parsing ends with instead of arguments: help, or an error.
struct Exit {
    int code;
    std::string text;
    bool to_stdout;
};

std::string usage(const std::string& bin, const std::string& arguments = "[OPTIONS]") {
    return "Usage: " + bin + " " + arguments;
}

std::string help(const std::string& bin) {
    return std::string(about) + "\n\n" + usage(bin) +
           "\n\nOptions:\n"
           "  -c, --config <CONFIG>  Path to configuration file\n"
           "  -j, --json <JSON>      Configuration as JSON string\n"
           "      --fallback         Serve an error page instead of exiting, if we can't start\n"
           "      --host <HOST>      Bind socket to this host [default: 127.0.0.1]\n"
           "      --port <PORT>      Bind socket to this port. If 0, an available port will be picked "
           "[default: 5000]\n"
           "      --debug            Enable debug logging\n"
           "  -h, --help             Print help\n";
}

// clap's error: the message, the usage (if given), and a hint.
Exit error(const std::string& message, const std::optional<std::string>& usage_line) {
    std::string text = "error: " + message + "\n\n";
    if (usage_line) {
        text += *usage_line + "\n\n";
    }
    return {2, text + "For more information, try '--help'.\n", false};
}

// A port as clap's `value_parser!(u16)` reads it: an i64 (Rust's
// `from_str`), then the range.
std::variant<std::uint16_t, std::string> parse_port(const std::string& text) {
    if (text.empty()) {
        return std::string("cannot parse integer from empty string");
    }
    std::string_view digits = text;
    const bool negative = digits.front() == '-';
    if (digits.front() == '+' || negative) {
        digits.remove_prefix(1);
    }
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string_view::npos) {
        return std::string("invalid digit found in string");
    }
    std::int64_t value = 0;
    const auto [end, problem] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (problem == std::errc::result_out_of_range) {
        return std::string(negative ? "number too small to fit in target type"
                                    : "number too large to fit in target type");
    }
    if (negative) {
        value = -value;
    }
    if (value < 0 || value > 65535) {
        return std::to_string(value) + " is not in 0..=65535";
    }
    return static_cast<std::uint16_t>(value);
}

std::variant<CliArgs, Exit> parse(const std::vector<std::string>& args) {
    const std::string bin = args.empty() ? "teta-wot" : [&] {
        const auto name =
            std::filesystem::path(std::u8string(args[0].begin(), args[0].end())).filename().u8string();
        return std::string(name.begin(), name.end());
    }();
    CliArgs parsed;
    bool seen_config = false;
    bool seen_json = false;
    bool seen_host = false;
    bool seen_port = false;
    bool positional = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (positional || arg.size() < 2 || arg.front() != '-') {
            return error("unexpected argument '" + arg + "' found", usage(bin));
        }
        if (arg == "--") {
            positional = true;
            continue;
        }
        // The option's long name, and a value given with it (`--port=80`,
        // `-c=file`, `-cfile`).
        std::string name;
        std::optional<std::string> inline_value;
        if (arg.starts_with("--")) {
            const auto equals = arg.find('=');
            name = arg.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
            if (equals != std::string::npos) {
                inline_value = arg.substr(equals + 1);
            }
        } else {
            const char letter = arg[1];
            name = letter == 'c' ? "config" : letter == 'j' ? "json" : letter == 'h' ? "help" : "";
            if (name.empty()) {
                return error("unexpected argument '-" + std::string(1, letter) + "' found", usage(bin));
            }
            if (arg.size() > 2) {
                inline_value = arg.substr(arg[2] == '=' ? 3 : 2);
            }
        }
        if (name == "help") {
            return Exit{0, help(bin), true};
        }
        if (name == "fallback" || name == "debug") {
            if (inline_value) {
                return error("unexpected value '" + *inline_value + "' for '--" + name +
                                 "' found; no more were expected",
                             usage(bin, "--" + name));
            }
            bool& flag = name == "fallback" ? parsed.fallback : parsed.debug;
            if (flag) {
                return error("the argument '--" + name + "' cannot be used multiple times", usage(bin));
            }
            flag = true;
            continue;
        }
        std::string placeholder;
        bool* seen = nullptr;
        if (name == "config") {
            placeholder = "--config <CONFIG>";
            seen = &seen_config;
        } else if (name == "json") {
            placeholder = "--json <JSON>";
            seen = &seen_json;
        } else if (name == "host") {
            placeholder = "--host <HOST>";
            seen = &seen_host;
        } else if (name == "port") {
            placeholder = "--port <PORT>";
            seen = &seen_port;
        } else {
            return error("unexpected argument '--" + name + "' found", usage(bin));
        }
        std::string value;
        if (inline_value) {
            value = *inline_value;
        } else if (i + 1 < args.size() && (args[i + 1] == "-" || !args[i + 1].starts_with('-'))) {
            value = args[++i];
        } else {
            return error("a value is required for '" + placeholder + "' but none was supplied", std::nullopt);
        }
        if (*seen) {
            return error("the argument '" + placeholder + "' cannot be used multiple times", usage(bin));
        }
        *seen = true;
        if (name == "config") {
            parsed.config = value;
        } else if (name == "json") {
            parsed.json = value;
        } else if (name == "host") {
            parsed.host = value;
        } else {
            auto port = parse_port(value);
            if (const auto* problem = std::get_if<std::string>(&port)) {
                return error("invalid value '" + value + "' for '" + placeholder + "': " + *problem,
                             std::nullopt);
            }
            parsed.port = std::get<std::uint16_t>(port);
        }
    }
    return parsed;
}

// ---- Serving --------------------------------------------------------------------------

// Why serving failed, and so the exit code.
struct Failure {
    enum class Kind {
        /// The configuration is invalid (exit code 3).
        config,
        /// A Thing failed to start (exit code 3).
        startup,
        /// Anything else (exit code 1).
        other,
    };
    Kind kind;
    std::string message;
    // For `startup`.
    std::string thing;
    std::string error;
    std::vector<std::string> things;

    std::string text() const {
        if (kind == Kind::startup) {
            return "Failed to enter '" + thing + "' Thing: " + error;
        }
        return message;
    }
};

Failure other(std::string message) {
    return {Failure::Kind::other, std::move(message), {}, {}, {}};
}

// An OS error as the Rust standard library describes it.
std::string os_error(int code) {
    std::string text = std::system_category().message(code);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text + " (os error " + std::to_string(code) + ")";
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t point = 0;
        if (b < 0x80) {
            ++i;
            continue;
        }
        if (b >= 0xC2 && b <= 0xDF) {
            length = 2;
            point = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            length = 3;
            point = b & 0x0F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            length = 4;
            point = b & 0x07;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto c = static_cast<unsigned char>(text[i + k]);
            if ((c & 0xC0) != 0x80) {
                return false;
            }
            point = (point << 6) | (c & 0x3F);
        }
        if ((length == 3 && (point < 0x800 || (point >= 0xD800 && point <= 0xDFFF))) ||
            (length == 4 && (point < 0x10000 || point > 0x10FFFF))) {
            return false;
        }
        i += length;
    }
    return true;
}

// The file's text, or why it can't be read (as `fs::read_to_string` says).
std::variant<std::string, std::string> read_text(const std::string& path) {
    std::string bytes;
    const auto file = teta::wot::detail::path_from_text(path);
#ifdef _WIN32
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::variant<std::string, std::string>(std::in_place_index<1>,
                                                      os_error(static_cast<int>(GetLastError())));
    }
    char buffer[65536];
    DWORD read = 0;
    BOOL ok = TRUE;
    while ((ok = ReadFile(handle, buffer, sizeof buffer, &read, nullptr)) && read > 0) {
        bytes.append(buffer, read);
    }
    const DWORD failed = GetLastError();
    CloseHandle(handle);
    if (!ok) {
        return std::variant<std::string, std::string>(std::in_place_index<1>,
                                                      os_error(static_cast<int>(failed)));
    }
#else
    const int fd = open(file.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::variant<std::string, std::string>(std::in_place_index<1>, os_error(errno));
    }
    char buffer[65536];
    ssize_t read_bytes = 0;
    while ((read_bytes = read(fd, buffer, sizeof buffer)) > 0) {
        bytes.append(buffer, static_cast<std::size_t>(read_bytes));
    }
    const int failed = errno;
    close(fd);
    if (read_bytes < 0) {
        return std::variant<std::string, std::string>(std::in_place_index<1>, os_error(failed));
    }
#endif
    if (!valid_utf8(bytes)) {
        return std::variant<std::string, std::string>(std::in_place_index<1>,
                                                      std::string("stream did not contain valid UTF-8"));
    }
    return std::variant<std::string, std::string>(std::in_place_index<0>, std::move(bytes));
}

void say(const std::string& line) {
    std::cout << line << std::endl;
}

void complain(const std::string& line) {
    std::cerr << line << std::endl;
}

// Says where the server listens, when teta-wot does (once it has bound, before
// the Things start): at once for a given port, or once listening for port 0
// (any free port).
std::function<void(std::uint16_t)> announce(const detail::ListenAddress& address) {
    auto say_port = [ip = address.ip](std::uint16_t port) {
        say("listening on http://" + (ip.find(':') != std::string::npos ? "[" + ip + "]" : ip) + ":" +
            std::to_string(port));
    };
    if (address.port != 0) {
        say_port(address.port);
        return {};
    }
    return say_port;
}

std::optional<Failure> run(const CliArgs& args, const ThingRegistry& registry, const Customise& customise,
                           std::optional<std::string>& config_text, std::stop_token stop) {
    std::string text;
    if (args.config && args.json) {
        return other("Can't use both --config and --json simultaneously.");
    }
    if (args.config) {
        auto read = read_text(*args.config);
        if (read.index() == 1) {
            return other("Could not find configuration file " + *args.config + " (" + std::get<1>(read) +
                         ")");
        }
        text = std::move(std::get<0>(read));
    } else if (args.json) {
        text = *args.json;
    } else {
        return other("No configuration (or empty configuration) provided");
    }
    config_text = text;
    std::optional<ThingServerBuilder> builder;
    try {
        const auto config = ServerConfig::from_json(text);
        config_text = json::dump_pretty(json::parse(text));
        builder = ThingServer::from_config(config, registry);
    } catch (const ConfigError& error) {
        return Failure{Failure::Kind::config, error.what(), {}, {}, {}};
    }
    if (customise) {
        customise(*builder);
    }
    std::optional<ThingServer> server;
    try {
        server = builder->build();
    } catch (const std::exception& error) {
        return other(error.what());
    }
    std::vector<std::string> things;
    for (const auto& thing : server->runtime()->things()) {
        things.push_back(thing->name());
    }

    detail::ListenAddress address;
    try {
        address = detail::check_listen(args.host, args.port);
    } catch (const std::runtime_error& error) {
        return other(error.what());
    }
    try {
        server->serve(address.ip, address.port, std::move(stop), announce(address));
    } catch (const StartupError& failure) {
        return Failure{Failure::Kind::startup, {}, failure.thing(), failure.error(), std::move(things)};
    } catch (const std::exception& error) {
        return other(error.what());
    }
    return std::nullopt;
}

}  // namespace

int serve_until(const std::vector<std::string>& args, const ThingRegistry& registry,
                const Customise& customise, std::stop_token stop) {
    auto parsed = parse(args);
    if (const auto* exit = std::get_if<Exit>(&parsed)) {
        (exit->to_stdout ? std::cout : std::cerr) << exit->text << std::flush;
        return exit->code;
    }
    const auto& cli = std::get<CliArgs>(parsed);
    log::init(cli.debug);
    std::optional<std::string> config_text;
    const auto failure = run(cli, registry, customise, config_text, stop);
    if (!failure) {
        return 0;
    }

    if (cli.fallback) {
        say("Error: " + failure->text());
        say("Starting fallback server.");
        http::FallbackPage page;
        page.error_message = failure->text();
        if (failure->kind == Failure::Kind::startup) {
            page.things = failure->things;
        }
        page.config = config_text.value_or("");
        page.traceback = failure->kind == Failure::Kind::startup ? failure->error : failure->text();
        try {
            const auto address = detail::check_listen(cli.host, cli.port);
            serve_fallback(page, address.ip, address.port, stop, announce(address));
        } catch (const std::exception& error) {
            complain(std::string("Error: the fallback server couldn't start: ") + error.what());
        }
        return 3;
    }
    switch (failure->kind) {
        case Failure::Kind::config:
            say("Error reading configuration:\n" + failure->text());
            return 3;
        case Failure::Kind::startup:
            complain("Error: " + failure->text());
            return 3;
        case Failure::Kind::other:
            break;
    }
    complain("Error: " + failure->text());
    return 1;
}

int serve_from_args(const std::vector<std::string>& args, const ThingRegistry& registry,
                    const Customise& customise) {
    return serve_until(args, registry, customise, shutdown_signal());
}

int serve_from_cli(int argc, char** argv, const ThingRegistry& registry, const Customise& customise) {
    std::vector<std::string> args;
#ifdef _WIN32
    // Lines end with `\n`, as teta-wot's do, not the C runtime's `\r\n`.
    std::fflush(stdout);
    std::fflush(stderr);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
    // The wide command line, as UTF-8: `argv` is in the ANSI code page.
    (void)argc;
    (void)argv;
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 0; i < count; ++i) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(static_cast<std::size_t>((std::max)(length, 1)) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), length, nullptr, nullptr);
            args.push_back(std::move(arg));
        }
        LocalFree(wide);
    }
#else
    args.assign(argv, argv + argc);
#endif
    return serve_from_args(args, registry, customise);
}

}  // namespace teta::wot::cli
