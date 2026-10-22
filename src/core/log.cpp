#include "teta/wot/log.hpp"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <format>
#include <shared_mutex>
#include <string_view>

namespace teta::wot {

namespace {

std::tm utc_fields(std::chrono::system_clock::time_point time) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm fields{};
#ifdef _WIN32
    gmtime_s(&fields, &seconds);
#else
    gmtime_r(&seconds, &fields);
#endif
    return fields;
}

std::string_view basename(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string format_utc(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto since_epoch = time.time_since_epoch();
    auto micros = duration_cast<microseconds>(since_epoch).count() % 1'000'000;
    if (micros < 0) {
        micros += 1'000'000;
    }
    const auto whole = time_point_cast<seconds>(time - microseconds(micros));
    const std::tm fields = utc_fields(whole);
    std::string text =
        std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", fields.tm_year + 1900, fields.tm_mon + 1,
                    fields.tm_mday, fields.tm_hour, fields.tm_min, fields.tm_sec);
    if (micros != 0) {
        text += std::format(".{:06}", micros);
    }
    return text + "Z";
}

json::Value to_json(const LogRecord& record) {
    auto optional = [](const std::optional<std::string>& value) -> json::Value {
        if (value) {
            return json::Value(*value);
        }
        return nullptr;
    };
    return {{"message", record.message},
            {"levelname", record.levelname},
            {"levelno", record.levelno},
            {"lineno", record.lineno},
            {"filename", record.filename},
            {"created", format_utc(record.created)},
            {"exception_type", optional(record.exception_type)},
            {"traceback", optional(record.traceback)}};
}

void LogBuffer::push(LogRecord record) {
    std::lock_guard lock(mutex_);
    if (records_.size() == log_capacity) {
        records_.pop_front();
    }
    records_.push_back(std::move(record));
}

std::vector<LogRecord> LogBuffer::records() const {
    std::lock_guard lock(mutex_);
    return {records_.begin(), records_.end()};
}

std::size_t LogBuffer::size() const {
    std::lock_guard lock(mutex_);
    return records_.size();
}

namespace log {

namespace {

struct Settings {
    std::shared_mutex mutex;
    Sink sink;
    Level sink_level = Level::info;
};

Settings& settings() {
    static Settings instance;
    return instance;
}

std::atomic<Level> capture{Level::info};

thread_local std::shared_ptr<LogBuffer> current;

}  // namespace

std::pair<const char*, std::uint8_t> python_level(Level level) {
    switch (level) {
        case Level::error:
            return {"ERROR", 40};
        case Level::warn:
            return {"WARNING", 30};
        case Level::info:
            return {"INFO", 20};
        case Level::debug:
            return {"DEBUG", 10};
        case Level::trace:
            break;
    }
    return {"TRACE", 5};
}

void set_sink(Sink sink, Level level) {
    std::unique_lock lock(settings().mutex);
    settings().sink = std::move(sink);
    settings().sink_level = level;
}

Sink console_sink() {
    return [](const Event& event) {
        static std::mutex output;
        static constexpr const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
        const std::string line = std::format("{} {:>5} {}\n", format_utc(event.time),
                                             names[static_cast<int>(event.level)], event.message);
        std::lock_guard lock(output);
        std::fputs(line.c_str(), stderr);
    };
}

void set_capture_level(Level level) {
    capture.store(level);
}

Level capture_level() {
    return capture.load();
}

void init(bool debug) {
    const Level level = debug ? Level::debug : Level::info;
    set_capture_level(level);
    set_sink(console_sink(), level);
}

void emit(Event event, bool capture_it) {
    if (capture_it && current && event.level >= capture.load()) {
        const auto [levelname, levelno] = python_level(event.level);
        current->push(LogRecord{event.message, levelname, levelno, event.location.line(),
                                std::string(basename(event.location.file_name())), event.time,
                                event.exception_type, event.traceback});
    }
    std::shared_lock lock(settings().mutex);
    if (settings().sink && event.level >= settings().sink_level) {
        settings().sink(event);
    }
}

CaptureScope::CaptureScope(std::shared_ptr<LogBuffer> buffer) : previous_(std::move(current)) {
    current = std::move(buffer);
}

CaptureScope::~CaptureScope() {
    current = std::move(previous_);
}

std::shared_ptr<LogBuffer> current_capture() {
    return current;
}

}  // namespace log

}  // namespace teta::wot
