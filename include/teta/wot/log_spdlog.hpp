// An spdlog sink that sends spdlog's messages into teta-wot-cpp's logging
// (ADR-0010), so that driver code that already logs with spdlog is captured
// in invocation logs like `tw::log`'s own events: add it to a logger, and
// whatever that logger writes from an invocation's thread (or a device call)
// lands in that invocation's log.
//
//     spdlog::default_logger()->sinks().push_back(std::make_shared<tw::log::SpdlogSink>());
//
// Header-only: it needs spdlog (the vcpkg feature `spdlog`); teta-wot-cpp
// itself doesn't.

#pragma once

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "teta/wot/log.hpp"

namespace teta::wot::log {

template <typename Mutex>
class BasicSpdlogSink final : public spdlog::sinks::base_sink<Mutex> {
  protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        Level level = Level::info;
        switch (message.level) {
            case spdlog::level::trace:
                level = Level::trace;
                break;
            case spdlog::level::debug:
                level = Level::debug;
                break;
            case spdlog::level::info:
                level = Level::info;
                break;
            case spdlog::level::warn:
                level = Level::warn;
                break;
            case spdlog::level::err:
            case spdlog::level::critical:
                level = Level::error;
                break;
            default:
                return;  // `off`
        }
        Event event;
        event.level = level;
        event.message.assign(message.payload.data(), message.payload.size());
        if (message.source.filename) {
            event.file = message.source.filename;
            event.line = static_cast<std::uint32_t>(message.source.line);
        }
        event.time = message.time;
        emit(std::move(event));
    }
    void flush_() override {}
};

/// The sink for loggers used from several threads.
using SpdlogSink = BasicSpdlogSink<std::mutex>;

}  // namespace teta::wot::log
