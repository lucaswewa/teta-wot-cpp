#include "mdns.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// winsock2.h first.
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

// The vendored library, with its warnings left to it.
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

namespace teta::wot::detail {

namespace {

mdns_string_t text(const std::string& value) {
    return {value.data(), value.size()};
}

bool same_name(const mdns_string_t& a, const std::string& b) {
    return a.length == b.size() && std::equal(a.str, a.str + a.length, b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

mdns_record_t record(mdns_record_type_t type, const std::string& name) {
    mdns_record_t out{};
    out.name = text(name);
    out.type = type;
    return out;
}

/// The computer's addresses, as the advertisement names them: those of the
/// interfaces that are up, but loopback and link-local ones.
void local_addresses(std::vector<sockaddr_in>& ipv4, std::vector<sockaddr_in6>& ipv6) {
    const auto keep6 = [&](const sockaddr_in6& address) {
        static constexpr unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (address.sin6_scope_id != 0 || std::memcmp(&address.sin6_addr, loopback, 16) == 0) {
            return;
        }
        ipv6.push_back(address);
    };
    const auto keep4 = [&](const sockaddr_in& address) {
        if ((ntohl(address.sin_addr.s_addr) >> 24) != 127) {
            ipv4.push_back(address);
        }
    };
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        storage.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    }
    if (result != NO_ERROR) {
        return;
    }
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); adapter;
         adapter = adapter->Next) {
        if (adapter->TunnelType == TUNNEL_TYPE_TEREDO || adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const auto* address = unicast->Address.lpSockaddr;
            if (address->sa_family == AF_INET) {
                keep4(*reinterpret_cast<const sockaddr_in*>(address));
            } else if (address->sa_family == AF_INET6 && unicast->DadState == NldsPreferred) {
                keep6(*reinterpret_cast<const sockaddr_in6*>(address));
            }
        }
    }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return;
    }
    for (auto* entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || !(entry->ifa_flags & IFF_UP) || !(entry->ifa_flags & IFF_MULTICAST) ||
            (entry->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        if (entry->ifa_addr->sa_family == AF_INET) {
            keep4(*reinterpret_cast<const sockaddr_in*>(entry->ifa_addr));
        } else if (entry->ifa_addr->sa_family == AF_INET6) {
            keep6(*reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr));
        }
    }
    freeifaddrs(interfaces);
#endif
}

}  // namespace

struct MdnsAdvertisement::State {
    std::string dns_sd = "_services._dns-sd._udp.local.";
    std::string service = "_wot._tcp.local.";
    std::string subtype = "_directory._sub._wot._tcp.local.";
    std::string fullname;
    std::string host;
    std::uint16_t port = 0;
    std::vector<sockaddr_in> ipv4;
    std::vector<sockaddr_in6> ipv6;
    std::vector<int> sockets;
    // The library wants 32-bit aligned buffers: one to receive in, one to
    // answer from.
    std::vector<std::uint32_t> received = std::vector<std::uint32_t>(1024);
    std::vector<std::uint32_t> sent = std::vector<std::uint32_t>(1024);

    mdns_record_t services_ptr{};
    mdns_record_t service_ptr{};
    mdns_record_t subtype_ptr{};
    mdns_record_t srv{};
    std::vector<mdns_record_t> addresses;
    std::vector<mdns_record_t> txt;
#ifdef _WIN32
    bool winsock = false;
#endif

    void build_records() {
        services_ptr = record(MDNS_RECORDTYPE_PTR, dns_sd);
        services_ptr.data.ptr.name = text(service);
        service_ptr = record(MDNS_RECORDTYPE_PTR, service);
        service_ptr.data.ptr.name = text(fullname);
        subtype_ptr = record(MDNS_RECORDTYPE_PTR, subtype);
        subtype_ptr.data.ptr.name = text(fullname);
        srv = record(MDNS_RECORDTYPE_SRV, fullname);
        srv.data.srv.name = text(host);
        srv.data.srv.port = port;
        for (const auto& address : ipv4) {
            auto a = record(MDNS_RECORDTYPE_A, host);
            a.data.a.addr = address;
            addresses.push_back(a);
        }
        for (const auto& address : ipv6) {
            auto aaaa = record(MDNS_RECORDTYPE_AAAA, host);
            aaaa.data.aaaa.addr = address;
            addresses.push_back(aaaa);
        }
        static const std::string keys[] = {"td", "type", "scheme"};
        static const std::string values[] = {"/.well-known/wot", "Directory", "http"};
        for (std::size_t i = 0; i < 3; ++i) {
            auto entry = record(MDNS_RECORDTYPE_TXT, fullname);
            entry.data.txt.key = text(keys[i]);
            entry.data.txt.value = text(values[i]);
            txt.push_back(entry);
        }
    }

    // What resolving the instance needs, besides the answer: its SRV, the
    // host's addresses and the TXT entries (`but` left out).
    std::vector<mdns_record_t> additional(const mdns_record_t* but = nullptr) const {
        std::vector<mdns_record_t> out;
        if (but != &srv) {
            out.push_back(srv);
        }
        for (const auto& address : addresses) {
            if (&address != but) {
                out.push_back(address);
            }
        }
        out.insert(out.end(), txt.begin(), txt.end());
        return out;
    }

    void answer(int sock, const sockaddr* from, std::size_t length, std::uint16_t query_id,
                std::uint16_t type, std::uint16_t rclass, const mdns_string_t& name,
                const mdns_record_t& reply, const std::vector<mdns_record_t>& extra) {
        // Unicast if asked, or to a legacy resolver (not from port 5353).
        bool unicast = (rclass & MDNS_UNICAST_RESPONSE) != 0;
        if (from->sa_family == AF_INET) {
            unicast = unicast || reinterpret_cast<const sockaddr_in*>(from)->sin_port != htons(MDNS_PORT);
        } else if (from->sa_family == AF_INET6) {
            unicast = unicast || reinterpret_cast<const sockaddr_in6*>(from)->sin6_port != htons(MDNS_PORT);
        }
        const std::size_t capacity = sent.size() * sizeof(std::uint32_t);
        if (unicast) {
            mdns_query_answer_unicast(sock, from, length, sent.data(), capacity, query_id,
                                      static_cast<mdns_record_type_t>(type), name.str, name.length, reply,
                                      nullptr, 0, extra.data(), extra.size());
        } else {
            mdns_query_answer_multicast(sock, sent.data(), capacity, reply, nullptr, 0, extra.data(),
                                        extra.size());
        }
    }

    void question(int sock, const sockaddr* from, std::size_t length, std::uint16_t query_id,
                  std::uint16_t type, std::uint16_t rclass, const void* data, std::size_t size,
                  std::size_t name_offset) {
        char buffer[256];
        std::size_t offset = name_offset;
        const mdns_string_t name = mdns_string_extract(data, size, &offset, buffer, sizeof(buffer));
        const bool any = type == MDNS_RECORDTYPE_ANY;
        if (same_name(name, dns_sd) && (any || type == MDNS_RECORDTYPE_PTR)) {
            this->answer(sock, from, length, query_id, type, rclass, name, services_ptr, {});
        } else if (same_name(name, service) && (any || type == MDNS_RECORDTYPE_PTR)) {
            this->answer(sock, from, length, query_id, type, rclass, name, service_ptr, additional());
        } else if (same_name(name, subtype) && (any || type == MDNS_RECORDTYPE_PTR)) {
            this->answer(sock, from, length, query_id, type, rclass, name, subtype_ptr, additional());
        } else if (same_name(name, fullname) &&
                   (any || type == MDNS_RECORDTYPE_SRV || type == MDNS_RECORDTYPE_TXT)) {
            // A TXT record can't be the answer itself (the library writes TXT
            // entries in one record, after the others): the SRV is, with
            // the TXT entries as additional records.
            this->answer(sock, from, length, query_id, type, rclass, name, srv, additional(&srv));
        } else if (same_name(name, host)) {
            const auto wanted = any ? MDNS_RECORDTYPE_A : static_cast<mdns_record_type_t>(type);
            const auto found =
                std::ranges::find_if(addresses, [&](const auto& a) { return a.type == wanted; });
            if (found != addresses.end()) {
                std::vector<mdns_record_t> others;
                for (const auto& address : addresses) {
                    if (&address != &*found) {
                        others.push_back(address);
                    }
                }
                this->answer(sock, from, length, query_id, type, rclass, name, *found, others);
            }
        }
    }

    static int on_record(int sock, const sockaddr* from, std::size_t length, mdns_entry_type_t entry,
                         std::uint16_t query_id, std::uint16_t type, std::uint16_t rclass, std::uint32_t,
                         const void* data, std::size_t size, std::size_t name_offset, std::size_t,
                         std::size_t, std::size_t, void* state) {
        if (entry == MDNS_ENTRYTYPE_QUESTION) {
            static_cast<State*>(state)->question(sock, from, length, query_id, type, rclass, data, size,
                                                 name_offset);
        }
        return 0;
    }

    // An unsolicited announcement, or with `goodbye` its withdrawal.
    void announce(bool goodbye) {
        auto extra = additional();
        extra.insert(extra.begin(), subtype_ptr);
        const std::size_t capacity = sent.size() * sizeof(std::uint32_t);
        for (const int sock : sockets) {
            if (goodbye) {
                mdns_goodbye_multicast(sock, sent.data(), capacity, service_ptr, nullptr, 0, extra.data(),
                                       extra.size());
            } else {
                mdns_announce_multicast(sock, sent.data(), capacity, service_ptr, nullptr, 0, extra.data(),
                                        extra.size());
            }
        }
    }

    void run(const std::stop_token& stop) {
        announce(false);
        const auto started = std::chrono::steady_clock::now();
        bool announced_again = false;
        while (!stop.stop_requested()) {
            // RFC 6762: a second announcement, a second later.
            if (!announced_again && std::chrono::steady_clock::now() - started > std::chrono::seconds(1)) {
                announce(false);
                announced_again = true;
            }
            fd_set readable;
            FD_ZERO(&readable);
            int highest = 0;
            for (const int sock : sockets) {
#ifdef _WIN32
                FD_SET(static_cast<SOCKET>(sock), &readable);
#else
                FD_SET(sock, &readable);
#endif
                highest = std::max(highest, sock);
            }
            timeval timeout{0, 100000};
            if (select(highest + 1, &readable, nullptr, nullptr, &timeout) < 0) {
                break;
            }
            const std::size_t capacity = received.size() * sizeof(std::uint32_t);
            for (const int sock : sockets) {
#ifdef _WIN32
                const bool ready = FD_ISSET(static_cast<SOCKET>(sock), &readable);
#else
                const bool ready = FD_ISSET(sock, &readable);
#endif
                if (ready) {
                    mdns_socket_listen(sock, received.data(), capacity, &State::on_record, this);
                }
            }
        }
        announce(true);
    }

    ~State() {
        for (const int sock : sockets) {
            mdns_socket_close(sock);
        }
#ifdef _WIN32
        if (winsock) {
            WSACleanup();
        }
#endif
    }
};

std::unique_ptr<MdnsAdvertisement> MdnsAdvertisement::start(const std::string& instance, std::uint16_t port) {
    auto state = std::make_unique<State>();
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("can't start Winsock");
    }
    state->winsock = true;
#endif
    std::string host;
    for (const char c : instance) {
        host += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    }
    state->fullname = instance + "." + state->service;
    state->host = host + ".local.";
    state->port = port;
    local_addresses(state->ipv4, state->ipv6);
    // One socket per address family, on port 5353 of every interface.
    sockaddr_in any4{};
    any4.sin_family = AF_INET;
    any4.sin_addr.s_addr = htonl(INADDR_ANY);
    any4.sin_port = htons(MDNS_PORT);
    if (const int sock = mdns_socket_open_ipv4(&any4); sock >= 0) {
        state->sockets.push_back(sock);
    }
    sockaddr_in6 any6{};
    any6.sin6_family = AF_INET6;
    any6.sin6_addr = in6addr_any;
    any6.sin6_port = htons(MDNS_PORT);
    if (const int sock = mdns_socket_open_ipv6(&any6); sock >= 0) {
        state->sockets.push_back(sock);
    }
    if (state->sockets.empty()) {
        throw std::runtime_error("can't open an mDNS socket on port 5353");
    }
    state->build_records();
    return std::unique_ptr<MdnsAdvertisement>(new MdnsAdvertisement(std::move(state)));
}

MdnsAdvertisement::MdnsAdvertisement(std::unique_ptr<State> state)
    : state_(std::move(state)), thread_([state = state_.get()](std::stop_token stop) { state->run(stop); }) {}

MdnsAdvertisement::~MdnsAdvertisement() {
    stop();
}

void MdnsAdvertisement::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

}  // namespace teta::wot::detail
