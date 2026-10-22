// The logging facade and invocation log buffers.

#include "teta/wot/log.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

namespace tw = teta::wot;
namespace log = teta::wot::log;
using namespace std::chrono_literals;

TEST(Log, UtcTimesAreWrittenAsPydanticWritesThem) {
    const auto epoch = std::chrono::system_clock::time_point{};
    EXPECT_EQ(tw::format_utc(epoch + 86400s + 3723s), "1970-01-02T01:02:03Z");
    EXPECT_EQ(tw::format_utc(epoch + 3723s + 450us), "1970-01-01T01:02:03.000450Z");
}

TEST(Log, EventsAreCapturedInTheCurrentScopeAtOrAboveTheCaptureLevel) {
    auto buffer = std::make_shared<tw::LogBuffer>(tw::Uuid::v4());
    log::info("outside");
    {
        log::CaptureScope scope(buffer);
        log::info("Moved to {}", 3);
        log::debug("not captured");
        const int line = __LINE__ + 1;
        log::error("failed: {}", "jammed");
        {
            auto inner = std::make_shared<tw::LogBuffer>(tw::Uuid::v4());
            log::CaptureScope nested(inner);
            log::warn("inner");
            EXPECT_EQ(inner->size(), 1u);
        }
        log::emit({log::Level::warn, "uncaptured", __FILE__, __LINE__, {}, {}, {}}, false);
        const auto records = buffer->records();
        ASSERT_EQ(records.size(), 2u);
        EXPECT_EQ(records[0].message, "Moved to 3");
        EXPECT_EQ(records[0].levelname, "INFO");
        EXPECT_EQ(records[0].levelno, 20);
        EXPECT_EQ(records[1].levelname, "ERROR");
        EXPECT_EQ(records[1].levelno, 40);
        EXPECT_EQ(records[1].lineno, static_cast<std::uint32_t>(line));
        EXPECT_EQ(records[1].filename, "log_test.cpp");
    }
    log::info("after");
    EXPECT_EQ(buffer->size(), 2u);
    EXPECT_FALSE(log::current_capture());
}

TEST(Log, AScopeIsPerThread) {
    auto buffer = std::make_shared<tw::LogBuffer>(tw::Uuid::v4());
    log::CaptureScope scope(buffer);
    std::thread([] { log::info("another thread"); }).join();
    EXPECT_TRUE(buffer->empty());
}

TEST(Log, ABufferKeepsTheLastThousandRecords) {
    tw::LogBuffer buffer(tw::Uuid::v4());
    for (int i = 0; i < 1005; ++i) {
        buffer.push({std::to_string(i), "INFO", 20, 1, "f", {}, {}, {}});
    }
    const auto records = buffer.records();
    EXPECT_EQ(records.size(), tw::log_capacity);
    EXPECT_EQ(records.front().message, "5");
    EXPECT_EQ(tw::json::dump(tw::to_json(records.front())),
              R"({"message":"5","levelname":"INFO","levelno":20,"lineno":1,"filename":"f",)"
              R"("created":"1970-01-01T00:00:00Z","exception_type":null,"traceback":null})");
}

TEST(Log, PythonLevels) {
    EXPECT_STREQ(log::python_level(log::Level::trace).first, "TRACE");
    EXPECT_EQ(log::python_level(log::Level::trace).second, 5);
    EXPECT_STREQ(log::python_level(log::Level::warn).first, "WARNING");
}

}  // namespace
