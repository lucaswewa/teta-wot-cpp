// Discovery (W3C WoT Discovery): a server that can be found.
//
// The server advertises itself on the local network with DNS-SD over mDNS, as
// a Thing Description Directory (`_directory._sub._wot._tcp`), with the path
// of its TD in the TXT record. A client that knows nothing about it browses
// for directories, reads the TD at `/.well-known/wot`, and follows the
// directory's `things` form to every Thing's TD.
//
//     example-discovery                           # both in one process, then exit
//     example-discovery --serve                   # serve on every interface, port 8000 (or --port)
//
// The browsing uses mjansson/mdns, the library the server advertises with.
// Built without TETA_WOT_MDNS, the server can't advertise: the demo says so
// and exits 77. The C++ port of teta-wot's `discovery`.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "mdns.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// A lamp, to be found.
class Lamp {
  public:
    /// The brightness, in percent.
    tw::Prop<std::int64_t> level{50};
    static void define(tw::ThingSpec<Lamp>& t) {
        t.doc("A lamp, to be found.");
        t.property("level", &Lamp::level).doc("The brightness, in percent.").ge(0).le(100);
    }
};

/// A thermometer, to be found too.
class Thermometer {
  public:
    /// The temperature, in degrees Celsius.
    tw::Prop<double> temperature{21.5};
    static void define(tw::ThingSpec<Thermometer>& t) {
        t.doc("A thermometer, to be found too.");
        t.property("temperature", &Thermometer::temperature)
            .doc("The temperature, in degrees Celsius.")
            .read_only();
    }
};

/// The DNS-SD instance name: the server ID.
constexpr std::string_view instance = "teta-wot discovery example";

/// The exit code CTest reads as "skipped": built without TETA_WOT_MDNS.
constexpr int skipped = 77;

tw::ThingServer server() {
    return tw::ThingServer::builder()
        .server_id(std::string(instance))
        .mdns(true)
        .thing<Lamp>("lamp")
        .thing<Thermometer>("thermometer")
        .build();
}

void ensure(bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

// ---- Browsing, with mjansson/mdns -----------------------------------------------------------

/// What answers to the query said about each directory.
struct Found {
    std::set<std::string> instances;
    std::map<std::string, std::pair<std::string, std::uint16_t>> servers;  // instance: host, port
    std::map<std::string, std::map<std::string, std::string>> txt;         // instance: key, value
    std::map<std::string, std::set<std::string>> addresses;                // host: IPv4 addresses
};

int on_record(int, const sockaddr*, std::size_t, mdns_entry_type_t entry, std::uint16_t, std::uint16_t type,
              std::uint16_t, std::uint32_t, const void* data, std::size_t size, std::size_t name_offset,
              std::size_t, std::size_t record_offset, std::size_t record_length, void* user_data) {
    if (entry == MDNS_ENTRYTYPE_QUESTION) {
        return 0;
    }
    auto& found = *static_cast<Found*>(user_data);
    char name_buffer[256];
    std::size_t offset = name_offset;
    const auto name = mdns_string_extract(data, size, &offset, name_buffer, sizeof(name_buffer));
    const std::string owner(name.str, name.length);
    char buffer[256];
    if (type == MDNS_RECORDTYPE_PTR) {
        const auto target =
            mdns_record_parse_ptr(data, size, record_offset, record_length, buffer, sizeof(buffer));
        if (owner == tw::directory_service_type) {
            found.instances.emplace(target.str, target.length);
        }
    } else if (type == MDNS_RECORDTYPE_SRV) {
        const auto srv =
            mdns_record_parse_srv(data, size, record_offset, record_length, buffer, sizeof(buffer));
        found.servers[owner] = {std::string(srv.name.str, srv.name.length), srv.port};
    } else if (type == MDNS_RECORDTYPE_TXT) {
        mdns_record_txt_t entries[16];
        const auto count = mdns_record_parse_txt(data, size, record_offset, record_length, entries, 16);
        for (std::size_t i = 0; i < count; ++i) {
            found.txt[owner][std::string(entries[i].key.str, entries[i].key.length)] =
                std::string(entries[i].value.str, entries[i].value.length);
        }
    } else if (type == MDNS_RECORDTYPE_A) {
        sockaddr_in address{};
        mdns_record_parse_a(data, size, record_offset, record_length, &address);
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text));
        found.addresses[owner].insert(text);
    }
    return 0;
}

/// A directory, resolved: its instance, port, TXT entries and IPv4 addresses.
struct Directory {
    std::string instance;
    std::uint16_t port = 0;
    std::map<std::string, std::string> txt;
    std::vector<std::string> addresses;
};

/// Browses for TD Directories until one on `port` is resolved, or 20 s.
std::optional<Directory> browse(std::uint16_t port) {
    const int sock = mdns_socket_open_ipv4(nullptr);
    if (sock < 0) {
        throw std::runtime_error("can't open a socket to browse with");
    }
    std::cout << "browsing for " << tw::directory_service_type << "\n";
    std::vector<std::uint32_t> buffer(1024);
    const std::size_t capacity = buffer.size() * sizeof(std::uint32_t);
    const std::string type(tw::directory_service_type);
    Found found;
    std::set<std::string> reported;
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    auto next_query = std::chrono::steady_clock::now();
    int query = 0;
    std::optional<Directory> result;
    while (!result && std::chrono::steady_clock::now() < deadline) {
        // Asked again every second, in case an answer was lost.
        if (std::chrono::steady_clock::now() >= next_query) {
            query = mdns_query_send(sock, MDNS_RECORDTYPE_PTR, type.data(), type.size(), buffer.data(),
                                    capacity, 0);
            next_query = std::chrono::steady_clock::now() + 1s;
        }
        fd_set readable;
        FD_ZERO(&readable);
#ifdef _WIN32
        FD_SET(static_cast<SOCKET>(sock), &readable);
#else
        FD_SET(sock, &readable);
#endif
        timeval timeout{0, 200000};
        if (select(sock + 1, &readable, nullptr, nullptr, &timeout) > 0) {
            mdns_query_recv(sock, buffer.data(), capacity, &on_record, &found, query);
        }
        for (const auto& name : found.instances) {
            const auto server = found.servers.find(name);
            if (server == found.servers.end() || !found.txt.contains(name)) {
                continue;
            }
            const auto& [host, service_port] = server->second;
            if (reported.insert(name).second) {
                std::cout << "  found `" << name << "` at port " << service_port << "\n";
            }
            // Other directories on the network are someone else's.
            if (service_port == port && found.addresses.contains(host)) {
                const auto& addresses = found.addresses[host];
                result = Directory{name, service_port, found.txt[name], {addresses.begin(), addresses.end()}};
            }
        }
    }
    mdns_socket_close(sock);
    return result;
}

// ---- A minimal HTTP/1.1 client --------------------------------------------------------------

/// `GET path` from `address:port` as JSON.
Value get_json(const std::string& address, std::uint16_t port, const std::string& path) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &to.sin_addr);
#ifdef _WIN32
    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const auto close_socket = [&] { closesocket(sock); };
#else
    const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const auto close_socket = [&] { close(sock); };
#endif
    if (connect(sock, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
        close_socket();
        throw std::runtime_error("can't connect to " + address);
    }
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + address + ":" + std::to_string(port) +
                                "\r\nConnection: close\r\n\r\n";
    send(sock, request.data(), static_cast<int>(request.size()), 0);
    std::string response;
    char chunk[4096];
    for (int got; (got = static_cast<int>(recv(sock, chunk, sizeof(chunk), 0))) > 0;) {
        response.append(chunk, static_cast<std::size_t>(got));
    }
    close_socket();
    const auto head_end = response.find("\r\n\r\n");
    ensure(head_end != std::string::npos, "an HTTP response");
    const auto status = response.substr(response.find(' ') + 1, 3);
    ensure(status == "200", "GET " + path + ": " + status);
    return tw::json::parse(response.substr(head_end + 4));
}

/// Finds the server as a client would: by browsing for TD Directories.
void find(std::uint16_t port) {
    const auto directory = browse(port);
    ensure(directory.has_value(),
           "the advertisement wasn't found within 20 s: is multicast allowed on this network?");
    const auto txt = [&](const std::string& key) {
        const auto found = directory->txt.find(key);
        return found == directory->txt.end() ? std::string() : found->second;
    };
    std::cout << "  TXT: td=" << txt("td") << " type=" << txt("type") << " scheme=" << txt("scheme") << "\n";
    ensure(txt("type") == "Directory" && txt("scheme") == "http", "a TD Directory, over HTTP");

    // The advertised addresses are the computer's; the TD is at `td`.
    std::cout << "  addresses: [";
    for (std::size_t i = 0; i < directory->addresses.size(); ++i) {
        std::cout << (i ? ", " : "") << directory->addresses[i];
    }
    std::cout << "]\n";
    std::optional<std::pair<std::string, Value>> found;
    for (const auto& address : directory->addresses) {
        try {
            found = {address, get_json(address, port, txt("td"))};
            break;
        } catch (const std::exception&) {
        }
    }
    ensure(found.has_value(), "no advertised address answered");
    const auto& [address, td] = *found;
    const std::string at = address + ":" + std::to_string(port);
    std::cout << "\nGET http://" << at << txt("td") << "\n";
    std::cout << "  " << tw::json::dump(td["@type"]) << ": " << tw::json::dump(td["title"]) << "\n";

    // The directory's `things` form lists every TD.
    const auto base = td["base"].get<std::string>();
    const auto href = td["properties"]["things"]["forms"][0]["href"].get<std::string>();
    const auto split = base.find(at);
    const std::string path =
        (split == std::string::npos ? std::string("/") : base.substr(split + at.size())) +
        href.substr(0, href.find('{'));
    const auto tds = get_json(address, port, path);
    std::cout << "GET http://" << at << path << "\n";
    ensure(tds.is_array(), "the listing is an array");
    for (const auto& thing : tds) {
        std::cout << "  " << tw::json::dump(thing["title"]) << " (" << tw::json::dump(thing["id"])
                  << "): " << tw::json::dump(thing["base"]) << "\n";
    }
    ensure(tds.size() == 2, "both Things are listed");
    std::cout << "\nFound the server and both its Things.\n";
}

int main(int argc, char** argv) {
    try {
        bool serving = false;
        std::optional<std::uint16_t> port;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--serve") {
                serving = true;
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
            }
        }
        if (!tw::mdns_supported()) {
            std::cout << "built without TETA_WOT_MDNS: the server can't advertise itself\n";
            return skipped;
        }
#ifdef _WIN32
        WSADATA winsock;
        WSAStartup(MAKEWORD(2, 2), &winsock);
#endif
        // Every interface, since the advertisement names the computer's
        // addresses (Windows may ask whether to allow this).
        if (serving) {
            tw::log::init(false);
            server().serve("0.0.0.0", port.value_or(8000), tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "serving on port " << bound << ", advertised as `" << instance
                          << "`; Ctrl-C to stop" << std::endl;
            });
            return EXIT_SUCCESS;
        }
        std::promise<std::uint16_t> listening;
        auto bound = listening.get_future();
        std::stop_source stop;
        std::thread serving_thread([&] {
            try {
                server().serve("0.0.0.0", port.value_or(0), stop.get_token(),
                               [&](std::uint16_t at) { listening.set_value(at); });
            } catch (...) {
                try {
                    listening.set_exception(std::current_exception());
                } catch (const std::future_error&) {
                }
            }
        });
        int status = EXIT_SUCCESS;
        try {
            find(bound.get());
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << "\n";
            status = EXIT_FAILURE;
        }
        stop.request_stop();
        serving_thread.join();
        return status;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
