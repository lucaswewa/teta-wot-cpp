// Logging, and invocation logs (ADR-0010): a port of teta-wot-core's `logs`
// and `logging`, with a small facade in place of `tracing`.
//
// `log::info("Moved to {}", x)` (and `trace`, `debug`, `warn`, `error`)
// formats with `std::format` and records where it was called. An event goes
// to the console sink, if any, and, when it is emitted while an invocation
// runs (on the invocation's thread, or a thread that carries its scope), into
// that invocation's log: the last 1000 records, in Python's record shape.
// Capture takes INFO and above (DEBUG in debug mode).

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/uuid.hpp"

namespace teta::wot {

/// How many records an invocation's log keeps.
inline constexpr std::size_t log_capacity = 1000;

/// One record of an invocation's log.
///
/// `created` is UTC with microseconds and a `Z`. `levelname` and `levelno`
/// are Python's: `ERROR` 40, `WARNING` 30, `INFO` 20, `DEBUG` 10, and
/// `TRACE` 5. `exception_type` and `traceback` are set for errors logged
/// with their cause.
struct LogRecord {
    std::string message;
    std::string levelname;
    std::uint8_t levelno = 0;
    std::uint32_t lineno = 0;
    /// The source file's name, without its directory (Python's `filename`).
    std::string filename;
    std::chrono::system_clock::time_point created;
    std::optional<std::string> exception_type;
    std::optional<std::string> traceback;

    bool operator==(const LogRecord&) const = default;
};

/// The record as teta-wot writes it.
json::Value to_json(const LogRecord& record);

/// A UTC time as pydantic writes it: microseconds (left out when zero) and
/// a `Z`.
std::string format_utc(std::chrono::system_clock::time_point time);

/// The log of one invocation: its last `log_capacity` records.
class LogBuffer {
  public:
    explicit LogBuffer(Uuid id) : id_(id) {}

    const Uuid& id() const { return id_; }
    /// Adds a record, dropping the oldest if the buffer is full.
    void push(LogRecord record);
    /// A copy of the records, oldest first.
    std::vector<LogRecord> records() const;
    std::size_t size() const;
    bool empty() const { return size() == 0; }

  private:
    Uuid id_;
    mutable std::mutex mutex_;
    std::deque<LogRecord> records_;
};

namespace log {

enum class Level : std::uint8_t { trace, debug, info, warn, error };

/// Python's name and number for a level.
std::pair<const char*, std::uint8_t> python_level(Level level);

/// One log event.
struct Event {
    Level level = Level::info;
    std::string message;
    /// Where it was emitted: the source file (with or without its
    /// directory) and line, or "" and 0 if unknown.
    std::string_view file;
    std::uint32_t line = 0;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    std::optional<std::string> exception_type;
    std::optional<std::string> traceback;
};

/// Where events go besides invocation logs (the console). Called from the
/// logging thread.
using Sink = std::function<void(const Event&)>;

/// Sets the sink and the lowest level it receives. A null sink drops them.
void set_sink(Sink sink, Level level = Level::info);
/// A sink that writes to standard error, one line per event.
Sink console_sink();

/// The lowest level captured into invocation logs (INFO by default).
void set_capture_level(Level level);
Level capture_level();

/// The usual setup: the console at INFO and capture at INFO, or both at
/// DEBUG in debug mode (teta-wot's `logging::init`).
void init(bool debug);

/// Emits an event: into the current invocation's log (unless `capture` is
/// false), and to the sink.
void emit(Event event, bool capture = true);

/// Makes events on this thread go to `buffer` (an invocation's log) until
/// destroyed. Invocation scopes set it; nested ones replace it.
class [[nodiscard]] CaptureScope {
  public:
    explicit CaptureScope(std::shared_ptr<LogBuffer> buffer);
    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;
    ~CaptureScope();

  private:
    std::shared_ptr<LogBuffer> previous_;
};

/// The buffer events on this thread are captured into, if any.
std::shared_ptr<LogBuffer> current_capture();

/// A format string, with the location of the call it is written in.
template <typename... Args>
struct Format {
    template <typename S>
        requires std::convertible_to<const S&, std::string_view>
    consteval Format(const S& text, std::source_location where = std::source_location::current())
        : format(text), location(where) {}

    std::format_string<Args...> format;
    std::source_location location;
};

template <typename... Args>
void write(Level level, Format<std::type_identity_t<Args>...> format, Args&&... args) {
    emit({level, std::format(format.format, std::forward<Args>(args)...), format.location.file_name(),
          format.location.line(), std::chrono::system_clock::now(), std::nullopt, std::nullopt});
}

template <typename... Args>
void trace(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    write<Args...>(Level::trace, format, std::forward<Args>(args)...);
}
template <typename... Args>
void debug(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    write<Args...>(Level::debug, format, std::forward<Args>(args)...);
}
template <typename... Args>
void info(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    write<Args...>(Level::info, format, std::forward<Args>(args)...);
}
template <typename... Args>
void warn(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    write<Args...>(Level::warn, format, std::forward<Args>(args)...);
}
template <typename... Args>
void error(Format<std::type_identity_t<Args>...> format, Args&&... args) {
    write<Args...>(Level::error, format, std::forward<Args>(args)...);
}

}  // namespace log

}  // namespace teta::wot
