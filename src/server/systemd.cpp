#include "teta/wot/systemd.hpp"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#endif

namespace teta::wot::systemd {

namespace detail {

std::optional<std::string> notify_address(std::string_view variable) {
    // sizeof(sockaddr_un::sun_path) on Linux.
    constexpr std::size_t sun_path_size = 108;
    std::string address;
    if (variable.size() > 1 && variable.front() == '@') {
        address = std::string(1, '\0') + std::string(variable.substr(1));
    } else if (variable.size() > 1 && variable.front() == '/') {
        address = std::string(variable) + '\0';
    } else {
        return std::nullopt;
    }
    if (address.size() > sun_path_size) {
        return std::nullopt;
    }
    return address;
}

}  // namespace detail

bool notify(std::string_view state) {
#ifdef _WIN32
    (void)state;
    return false;
#else
    const char* variable = std::getenv("NOTIFY_SOCKET");
    if (!variable) {
        return false;
    }
    const auto address = detail::notify_address(variable);
    if (!address) {
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un to{};
    to.sun_family = AF_UNIX;
    std::memcpy(to.sun_path, address->data(), address->size());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + address->size());
    const auto sent = ::sendto(fd, state.data(), state.size(), MSG_NOSIGNAL,
                               reinterpret_cast<const sockaddr*>(&to), length);
    ::close(fd);
    return sent == static_cast<ssize_t>(state.size());
#endif
}

bool notify_ready() {
    return notify("READY=1");
}

std::string exec_word(std::string_view word) {
    std::string escaped;
    bool quoted = word.empty();
    for (const char c : word) {
        switch (c) {
            case '%':
                escaped += "%%";
                break;
            case '$':
                escaped += "$$";
                break;
            case '\\':
            case '"':
                escaped += '\\';
                escaped += c;
                quoted = true;
                break;
            case ' ':
            case '\t':
            case '\'':
            case ';':
                escaped += c;
                quoted = true;
                break;
            case '\n':
                escaped += "\\n";
                quoted = true;
                break;
            default:
                escaped += c;
        }
    }
    return quoted ? "\"" + escaped + "\"" : escaped;
}

std::string unit_file(const UnitOptions& options) {
    const auto executable = options.executable.u8string();
    std::string exec_start = exec_word(std::string(executable.begin(), executable.end()));
    for (const auto& argument : options.arguments) {
        exec_start += " " + exec_word(argument);
    }
    std::string unit =
        "[Unit]\nDescription=" + options.description +
        "\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=notify\n"
        "ExecStart=" +
        exec_start + "\nTimeoutStopSec=" + std::to_string(options.stop_timeout.count()) + "\n";
    if (options.user) {
        unit += "User=" + *options.user + "\n";
    }
    unit += "\n[Install]\nWantedBy=multi-user.target\n";
    return unit;
}

}  // namespace teta::wot::systemd
