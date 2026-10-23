// The soak tool's network clients, on blocking sockets with timeouts: a
// keep-alive HTTP/1.1 client, a reader of long answers (MJPEG and
// server-sent events), and a WebSocket client.

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
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace soak {

#ifdef _WIN32
using SocketHandle = SOCKET;
inline constexpr SocketHandle no_socket = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle no_socket = -1;
#endif

/// Starts the platform's sockets (Winsock), once.
inline void start_sockets() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started) {
        throw std::runtime_error("can't start Winsock");
    }
#endif
}

/// A port nothing listens on, on the loopback interface.
inline std::uint16_t free_port();

/// A TCP connection to `127.0.0.1:port`, closed when destroyed.
class Connection {
  public:
    Connection() = default;
    explicit Connection(std::uint16_t port) {
        start_sockets();
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (socket_ == no_socket ||
            ::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
            close();
            throw std::runtime_error("can't connect to port " + std::to_string(port));
        }
        int on = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept
        : socket_(std::exchange(other.socket_, no_socket)), buffer_(std::move(other.buffer_)) {}
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            close();
            socket_ = std::exchange(other.socket_, no_socket);
            buffer_ = std::move(other.buffer_);
        }
        return *this;
    }
    ~Connection() { close(); }

    bool open() const { return socket_ != no_socket; }

    void close() {
        if (socket_ != no_socket) {
#ifdef _WIN32
            closesocket(socket_);
#else
            ::close(socket_);
#endif
            socket_ = no_socket;
        }
    }

    /// Sends all of `bytes`; throws if the connection is gone.
    void send_all(std::string_view bytes) {
        while (!bytes.empty()) {
            const auto sent =
                ::send(socket_, bytes.data(), static_cast<int>(std::min<std::size_t>(bytes.size(), 1 << 20)),
#ifdef _WIN32
                       0);
#else
                       MSG_NOSIGNAL);
#endif
            if (sent <= 0) {
                throw std::runtime_error("the connection closed while sending");
            }
            bytes.remove_prefix(static_cast<std::size_t>(sent));
        }
    }

    /// Reads what has arrived into the buffer, waiting up to `timeout` for
    /// something: the number of bytes read, 0 if nothing arrived in time, or
    /// nullopt if the connection ended.
    std::optional<std::size_t> fill(std::chrono::milliseconds timeout) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket_, &readable);
        timeval wait{};
        wait.tv_sec = static_cast<long>(timeout.count() / 1000);
        wait.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        const int ready = ::select(static_cast<int>(socket_ + 1), &readable, nullptr, nullptr, &wait);
        if (ready < 0) {
            return std::nullopt;
        }
        if (ready == 0) {
            return 0;
        }
        std::array<char, 64 * 1024> chunk;
        const auto n = ::recv(socket_, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (n <= 0) {
            return std::nullopt;
        }
        buffer_.append(chunk.data(), static_cast<std::size_t>(n));
        return static_cast<std::size_t>(n);
    }

    /// What has been read and not yet taken.
    std::string& buffer() { return buffer_; }

    /// Takes a line (without its CRLF), reading until there is one; throws if
    /// none arrives within `timeout` or the connection ends.
    std::string line(std::chrono::milliseconds timeout) {
        for (;;) {
            if (const auto end = buffer_.find("\r\n"); end != std::string::npos) {
                std::string taken = buffer_.substr(0, end);
                buffer_.erase(0, end + 2);
                return taken;
            }
            must_fill(timeout);
        }
    }

    /// Takes `n` bytes, reading until there are that many.
    std::string bytes(std::size_t n, std::chrono::milliseconds timeout) {
        while (buffer_.size() < n) {
            must_fill(timeout);
        }
        std::string taken = buffer_.substr(0, n);
        buffer_.erase(0, n);
        return taken;
    }

  private:
    void must_fill(std::chrono::milliseconds timeout) {
        const auto read = fill(timeout);
        if (!read) {
            throw std::runtime_error("the connection ended");
        }
        if (*read == 0) {
            throw std::runtime_error("no answer within " + std::to_string(timeout.count()) + " ms");
        }
    }

    SocketHandle socket_ = no_socket;
    std::string buffer_;
};

inline std::uint16_t free_port() {
    start_sockets();
    const SocketHandle s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    ::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof address);
    socklen_t length = sizeof address;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    return ntohs(address.sin_port);
}

/// An answer: the status and the body.
struct Answer {
    int status = 0;
    std::string body;
};

/// One connection, reused for every request, as a browser or `requests`
/// session would: thousands of short connections a minute would fill
/// Windows' ephemeral ports with sockets in `TIME_WAIT`.
class Http {
  public:
    explicit Http(std::uint16_t port) : port_(port) {}

    /// Sends a request; a connection the server closed while idle is
    /// reopened once. Throws on failure.
    Answer request(std::string_view method, std::string_view path, std::string_view body = {}) {
        try {
            return try_request(method, path, body);
        } catch (const std::exception&) {
            connection_.close();
            return try_request(method, path, body);
        }
    }

  private:
    Answer try_request(std::string_view method, std::string_view path, std::string_view body) {
        using namespace std::chrono_literals;
        if (!connection_.open()) {
            connection_ = Connection(port_);
        }
        std::string head =
            std::string(method) + " " + std::string(path) +
            " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port_) +
            "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\n\r\n";
        connection_.send_all(head + std::string(body));
        const auto status_line = connection_.line(30s);
        const auto space = status_line.find(' ');
        if (!status_line.starts_with("HTTP/1.") || space == std::string::npos) {
            throw std::runtime_error("not an HTTP status line: " + status_line);
        }
        Answer answer;
        answer.status = std::stoi(status_line.substr(space + 1, 3));
        std::size_t length = 0;
        bool chunked = false;
        bool close = false;
        for (;;) {
            const auto header = connection_.line(30s);
            if (header.empty()) {
                break;
            }
            const auto colon = header.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string name = header.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string value = header.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "content-length") {
                length = std::stoull(value);
            } else if (name == "transfer-encoding") {
                chunked = lower == "chunked";
            } else if (name == "connection") {
                close = lower == "close";
            }
        }
        if (chunked) {
            for (;;) {
                const auto size = std::stoull(connection_.line(30s), nullptr, 16);
                const auto chunk = connection_.bytes(size + 2, 30s);
                if (size == 0) {
                    break;
                }
                answer.body += chunk.substr(0, size);
            }
        } else {
            answer.body = connection_.bytes(length, 30s);
        }
        if (close) {
            connection_.close();
        }
        return answer;
    }

    std::uint16_t port_;
    Connection connection_;
};

/// A WebSocket client: the handshake, masked text frames out, frames in.
class WebSocket {
  public:
    /// Opens `path`; throws if the server refuses.
    WebSocket(std::uint16_t port, std::string_view path) : connection_(port) {
        using namespace std::chrono_literals;
        connection_.send_all(
            "GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        const auto status = connection_.line(10s);
        if (status.find(" 101 ") == std::string::npos) {
            throw std::runtime_error("the WebSocket was refused: " + status);
        }
        while (!connection_.line(10s).empty()) {
        }
    }

    void send_text(std::string_view text) { send_frame(0x1, text); }

    /// Waits up to `timeout` for frames: how many text messages arrived, or
    /// nullopt if the socket closed.
    std::optional<std::size_t> receive(std::chrono::milliseconds timeout) {
        if (!connection_.fill(timeout)) {
            return std::nullopt;
        }
        std::size_t texts = 0;
        auto& buffer = connection_.buffer();
        for (;;) {
            if (buffer.size() < 2) {
                return texts;
            }
            const auto opcode = static_cast<unsigned char>(buffer[0]) & 0x0f;
            std::size_t length = static_cast<unsigned char>(buffer[1]) & 0x7f;
            std::size_t header = 2;
            if (length == 126) {
                if (buffer.size() < 4) {
                    return texts;
                }
                length = (std::size_t{static_cast<unsigned char>(buffer[2])} << 8) |
                         static_cast<unsigned char>(buffer[3]);
                header = 4;
            } else if (length == 127) {
                if (buffer.size() < 10) {
                    return texts;
                }
                length = 0;
                for (std::size_t i = 2; i < 10; ++i) {
                    length = (length << 8) | static_cast<unsigned char>(buffer[i]);
                }
                header = 10;
            }
            if (buffer.size() < header + length) {
                return texts;
            }
            const std::string payload = buffer.substr(header, length);
            buffer.erase(0, header + length);
            if (opcode == 0x1) {
                ++texts;
            } else if (opcode == 0x8) {
                return std::nullopt;
            } else if (opcode == 0x9) {
                send_frame(0xa, payload);
            }
        }
    }

  private:
    void send_frame(unsigned char opcode, std::string_view payload) {
        std::string frame(1, static_cast<char>(0x80 | opcode));
        if (payload.size() < 126) {
            frame += static_cast<char>(0x80 | payload.size());
        } else {
            frame += static_cast<char>(0x80 | 126);
            frame += static_cast<char>((payload.size() >> 8) & 0xff);
            frame += static_cast<char>(payload.size() & 0xff);
        }
        const std::array<char, 4> mask = {'\x12', '\x34', '\x56', '\x78'};
        frame.append(mask.begin(), mask.end());
        for (std::size_t i = 0; i < payload.size(); ++i) {
            frame += static_cast<char>(payload[i] ^ mask[i % 4]);
        }
        connection_.send_all(frame);
    }

    Connection connection_;
};

}  // namespace soak
