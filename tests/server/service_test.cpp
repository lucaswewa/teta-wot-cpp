// The Windows service and systemd's notifications, without the Service
// Control Manager or systemd: command lines, unit files, the notification
// socket's address and protocol, and a process the SCM didn't start.
// examples/microscope-service/service_test.ps1 (and service_test.sh) run a
// real service.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "teta/wot.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h.
#include <shellapi.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#endif

namespace tw = teta::wot;
using namespace std::chrono_literals;

namespace service_test {

TEST(Service, CommandLinesQuoteWhatNeedsIt) {
    EXPECT_EQ(tw::service::command_line("C:/teta/microscope-service.exe", {"run", "-c", "C:/a b/c.json"}),
              R"("C:/teta/microscope-service.exe" run -c "C:/a b/c.json")");
    EXPECT_EQ(tw::service::command_line("C:/x.exe",
                                        {"", R"(say "hi")", R"(back\slash\)", R"(a\\"b)", R"(C:\dir\)"}),
              R"("C:/x.exe" "" "say \"hi\"" back\slash\ "a\\\\\"b" C:\dir\)");
    EXPECT_EQ(tw::service::command_line("C:/x.exe", {R"(C:\a dir\)"}), R"("C:/x.exe" "C:\a dir\\")");
}

#ifdef _WIN32
TEST(Service, CommandLinesReadBackAsTheirArguments) {
    const std::vector<std::string> arguments = {"run",     "-c",     "C:\\Program Files\\teta\\c.json",
                                                "",        "a\"b",   "trailing\\",
                                                "two\\\\", "x\\\"y", "tab\there",
                                                "end \\"};
    const auto line = tw::service::command_line("C:\\teta\\microscope-service.exe", arguments);
    int count = 0;
    const std::wstring wide(line.begin(), line.end());
    LPWSTR* parsed = CommandLineToArgvW(wide.c_str(), &count);
    ASSERT_NE(parsed, nullptr);
    std::vector<std::string> read;
    for (int i = 0; i < count; ++i) {
        std::string argument;
        for (const wchar_t* c = parsed[i]; *c; ++c) {
            argument += static_cast<char>(*c);  // ASCII
        }
        read.push_back(std::move(argument));
    }
    LocalFree(parsed);
    ASSERT_EQ(read.size(), arguments.size() + 1);
    EXPECT_EQ(read[0], "C:\\teta\\microscope-service.exe");
    EXPECT_EQ(std::vector<std::string>(read.begin() + 1, read.end()), arguments);
}
#endif

TEST(Service, OutsideTheServiceControlManagerAProcessIsNotAService) {
    bool served = false;
    const auto serve = [&](std::vector<std::string>, std::stop_token) {
        served = true;
        return 0;
    };
    EXPECT_THROW(tw::service::run_as_service("teta-wot-test", 1s, serve), tw::service::NotAService);
#ifdef _WIN32
    // Windows takes one service dispatcher per process, even after a failed
    // attempt.
    EXPECT_THROW(tw::service::run_as_service("teta-wot-test", 1s, serve), tw::service::AlreadyRunning);
#endif
    EXPECT_FALSE(served);
}

TEST(Service, UninstallingAMissingServiceIsRefused) {
    try {
        tw::service::uninstall("teta-wot-test-" + tw::Uuid::v4().to_string());
        FAIL() << "uninstalled a service that doesn't exist";
    } catch (const tw::service::ServiceError& error) {
#ifdef _WIN32
        EXPECT_NE(std::string(error.what()).find("(os error 1060)"), std::string::npos) << error.what();
#else
        EXPECT_NE(std::string(error.what()).find("only on Windows"), std::string::npos) << error.what();
#endif
    }
}

TEST(Systemd, ExecWordsAreQuotedAndEscaped) {
    EXPECT_EQ(tw::systemd::exec_word("/opt/teta/microscope-service"), "/opt/teta/microscope-service");
    EXPECT_EQ(tw::systemd::exec_word("--port"), "--port");
    EXPECT_EQ(tw::systemd::exec_word(""), R"("")");
    EXPECT_EQ(tw::systemd::exec_word("/srv/my lab/c.json"), R"("/srv/my lab/c.json")");
    EXPECT_EQ(tw::systemd::exec_word(R"(say "hi")"), R"("say \"hi\"")");
    EXPECT_EQ(tw::systemd::exec_word(R"(a\b)"), R"("a\\b")");
    EXPECT_EQ(tw::systemd::exec_word("100%"), "100%%");
    EXPECT_EQ(tw::systemd::exec_word("$HOME"), "$$HOME");
    EXPECT_EQ(tw::systemd::exec_word(";"), R"(";")");
    EXPECT_EQ(tw::systemd::exec_word("it's"), R"("it's")");
}

TEST(Systemd, UnitFiles) {
    tw::systemd::UnitOptions options;
    options.description = "teta-wot simulated microscope";
    options.executable = "/opt/teta/microscope-service";
    options.arguments = {"run", "-c", "/opt/teta/microscope service.json", "--port", "5000"};
    EXPECT_EQ(
        tw::systemd::unit_file(options),
        "[Unit]\n"
        "Description=teta-wot simulated microscope\n"
        "Wants=network-online.target\n"
        "After=network-online.target\n"
        "\n"
        "[Service]\n"
        "Type=notify\n"
        "ExecStart=/opt/teta/microscope-service run -c \"/opt/teta/microscope service.json\" --port 5000\n"
        "TimeoutStopSec=20\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n");
    options.user = "lab";
    options.stop_timeout = 45s;
    const auto unit = tw::systemd::unit_file(options);
    EXPECT_NE(unit.find("\nTimeoutStopSec=45\nUser=lab\n"), std::string::npos) << unit;
}

TEST(Systemd, NotificationAddresses) {
    using tw::systemd::detail::notify_address;
    EXPECT_EQ(notify_address("/run/systemd/notify"), std::string("/run/systemd/notify") + '\0');
    EXPECT_EQ(notify_address("@/org/freedesktop/systemd1/notify/42"),
              std::string(1, '\0') + "/org/freedesktop/systemd1/notify/42");
    EXPECT_EQ(notify_address(""), std::nullopt);
    EXPECT_EQ(notify_address("@"), std::nullopt);
    EXPECT_EQ(notify_address("run/notify"), std::nullopt);
    EXPECT_EQ(notify_address("vsock:2:1234"), std::nullopt);
    // sun_path holds 108 bytes: a path of 107 and its NUL, or an abstract
    // name of 107 after its NUL.
    EXPECT_TRUE(notify_address("/" + std::string(106, 'a')).has_value());
    EXPECT_EQ(notify_address("/" + std::string(107, 'a')), std::nullopt);
    EXPECT_TRUE(notify_address("@" + std::string(107, 'a')).has_value());
    EXPECT_EQ(notify_address("@" + std::string(108, 'a')), std::nullopt);
}

#ifdef _WIN32

TEST(Systemd, WindowsHasNoNotifications) {
    EXPECT_FALSE(tw::systemd::notify_ready());
}

#else

// NOTIFY_SOCKET for one test, restored afterwards.
class NotifySocket {
  public:
    explicit NotifySocket(const char* value) {
        if (const char* old = std::getenv("NOTIFY_SOCKET")) {
            old_ = old;
        }
        if (value) {
            ::setenv("NOTIFY_SOCKET", value, 1);
        } else {
            ::unsetenv("NOTIFY_SOCKET");
        }
    }
    NotifySocket(const NotifySocket&) = delete;
    NotifySocket& operator=(const NotifySocket&) = delete;
    ~NotifySocket() {
        if (old_) {
            ::setenv("NOTIFY_SOCKET", old_->c_str(), 1);
        } else {
            ::unsetenv("NOTIFY_SOCKET");
        }
    }

  private:
    std::optional<std::string> old_;
};

// A datagram socket listening at `address` (`notify_address`'s bytes), as
// systemd's is.
int listen_at(const std::string& address) {
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    sockaddr_un at{};
    at.sun_family = AF_UNIX;
    std::memcpy(at.sun_path, address.data(), address.size());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + address.size());
    EXPECT_EQ(::bind(fd, reinterpret_cast<const sockaddr*>(&at), length), 0) << std::strerror(errno);
    return fd;
}

std::string receive(int fd) {
    char buffer[256];
    const auto n = ::recv(fd, buffer, sizeof buffer, MSG_DONTWAIT);
    return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string();
}

TEST(Systemd, WithoutASocketNothingIsSent) {
    const NotifySocket none(nullptr);
    EXPECT_FALSE(tw::systemd::notify_ready());
    const NotifySocket relative("relative/notify");
    EXPECT_FALSE(tw::systemd::notify_ready());
}

TEST(Systemd, NotificationsReachAPathSocket) {
    const auto path =
        std::filesystem::temp_directory_path() / ("teta-wot-notify-" + tw::Uuid::v4().to_string());
    const int fd = listen_at(*tw::systemd::detail::notify_address(path.string()));
    const NotifySocket notify_socket(path.c_str());
    EXPECT_TRUE(tw::systemd::notify_ready());
    EXPECT_EQ(receive(fd), "READY=1");
    EXPECT_TRUE(tw::systemd::notify("STOPPING=1\nSTATUS=stopping"));
    EXPECT_EQ(receive(fd), "STOPPING=1\nSTATUS=stopping");
    ::close(fd);
    std::filesystem::remove(path);
}

TEST(Systemd, NotificationsReachAnAbstractSocket) {
    const std::string name = "@teta-wot-notify-" + tw::Uuid::v4().to_string();
    const int fd = listen_at(*tw::systemd::detail::notify_address(name));
    const NotifySocket notify_socket(name.c_str());
    EXPECT_TRUE(tw::systemd::notify_ready());
    EXPECT_EQ(receive(fd), "READY=1");
    ::close(fd);
}

#endif

}  // namespace service_test
