// A minimal blocking HTTP/1.1 client over a real socket, for examples that
// talk to a server they started on the loopback interface (the Rust
// examples use tokio's TcpStream the same way).

#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace examples::http {

namespace detail {

#ifdef _WIN32
using Socket = SOCKET;
inline void close_socket(Socket s) {
    closesocket(s);
}
#else
using Socket = int;
inline void close_socket(Socket s) {
    close(s);
}
#endif

inline Socket open_socket() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started) {
        throw std::runtime_error("can't start Winsock");
    }
#endif
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

inline sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    return address;
}

}  // namespace detail

/// A port nothing listens on, on the loopback interface.
inline std::uint16_t free_port() {
    const auto s = detail::open_socket();
    sockaddr_in address = detail::loopback(0);
    bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof address);
    socklen_t length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    detail::close_socket(s);
    return ntohs(address.sin_port);
}

/// An answer: its status line and headers, and its body.
struct Response {
    int status = 0;
    std::string head;
    std::string body;
};

/// Sends a request to `127.0.0.1:port`, with a JSON body if there is one, and
/// reads the answer to its end. Throws std::runtime_error if nothing listens.
inline Response request(std::uint16_t port, std::string_view method, std::string_view path,
                        std::string_view json = {}) {
    const auto s = detail::open_socket();
    const sockaddr_in address = detail::loopback(port);
    if (connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        detail::close_socket(s);
        throw std::runtime_error("can't connect to port " + std::to_string(port));
    }
    std::string text = std::string(method) + " " + std::string(path) +
                       " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: close\r\n";
    if (!json.empty()) {
        text += "Content-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\n";
    }
    text += "\r\n" + std::string(json);
    send(s, text.data(), static_cast<int>(text.size()), 0);
    std::string answer;
    char buffer[4096];
    for (;;) {
        const auto n = recv(s, buffer, sizeof buffer, 0);
        if (n <= 0) {
            break;
        }
        answer.append(buffer, static_cast<std::size_t>(n));
    }
    detail::close_socket(s);
    Response response;
    const auto end = answer.find("\r\n\r\n");
    response.head = answer.substr(0, end);
    response.body = end == std::string::npos ? "" : answer.substr(end + 4);
    const auto space = answer.find(' ');
    response.status = space == std::string::npos ? 0 : std::stoi(answer.substr(space + 1, 3));
    return response;
}

}  // namespace examples::http
