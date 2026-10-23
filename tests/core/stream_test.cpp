// MJPEG streams: teta-wot-core's `stream.rs` tests (JPEG markers, the ring
// buffer, waiters and receivers), and the notifiers that drive the HTTP
// binding's clients.

#include "teta/wot/stream.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace tw = teta::wot;
using namespace std::chrono_literals;

namespace stream_test {

std::string jpeg(char n) {
    return std::string{'\xff', '\xd8', n, '\xff', '\xd9'};
}

tw::StreamError::Kind error_of(const std::function<void()>& call) {
    try {
        call();
    } catch (const tw::StreamError& error) {
        return error.kind();
    }
    ADD_FAILURE() << "no StreamError";
    return tw::StreamError::Kind::invalid_jpeg;
}

}  // namespace stream_test

using namespace stream_test;
using Kind = tw::StreamError::Kind;

TEST(MjpegStream, FramesMustBeJpegs) {
    const tw::MjpegStream stream;
    for (const auto& bad :
         std::vector<std::string>{"", "\xff", "\xff\xd8", "\xff\xd8\xd9", std::string("\0\xd8\xff\xd9", 4)}) {
        EXPECT_EQ(error_of([&] { (void)stream.add_frame(bad); }), Kind::invalid_jpeg);
    }
    EXPECT_EQ(stream.add_frame("\xff\xd8\xff\xd9"), 0u);
    EXPECT_STREQ(tw::StreamError(Kind::invalid_jpeg).what(), "Invalid JPEG");
    EXPECT_STREQ(tw::StreamError(Kind::stopped).what(), "the stream has stopped");
}

TEST(MjpegStream, TheRingBufferKeepsTheLastFramesByIndex) {
    const tw::MjpegStream stream(3);
    for (char n = 0; n < 5; ++n) {
        EXPECT_EQ(stream.add_frame(jpeg(n)), static_cast<std::uint64_t>(n));
    }
    EXPECT_EQ(error_of([&] { (void)stream.frame(5); }), Kind::not_yet_acquired);
    EXPECT_EQ((*stream.frame(4).data)[2], 4);
    EXPECT_EQ((*stream.frame(3).data)[2], 3);
    // The oldest slot is about to be overwritten.
    EXPECT_EQ(error_of([&] { (void)stream.frame(2); }), Kind::overwritten);
    EXPECT_EQ(stream.latest_frame()->index, 4u);
    stream.reset(2);
    EXPECT_FALSE(stream.latest_frame());
    EXPECT_EQ(error_of([&] { (void)stream.frame(0); }), Kind::not_yet_acquired);
    EXPECT_EQ(stream.add_frame(jpeg(9)), 0u);
}

TEST(MjpegStream, CopiesAreTheSameStream) {
    const tw::MjpegStream stream;
    const auto camera = stream;
    (void)camera.add_frame(jpeg(1));
    EXPECT_EQ(stream.latest_frame()->index, 0u);
    camera.stop();
    EXPECT_FALSE(stream.is_streaming());
}

TEST(MjpegStream, WaitersGetTheNextFrameOrStopped) {
    const tw::MjpegStream stream;
    (void)stream.add_frame(jpeg(0));
    auto size = std::async(std::launch::async, [&] { return stream.next_frame_size(); });
    EXPECT_EQ(size.wait_for(50ms), std::future_status::timeout) << "it waits for the next frame";
    std::thread([camera = stream] { (void)camera.add_frame(jpeg(1)); }).join();
    EXPECT_EQ(size.get(), 5u);

    auto grabbed = std::async(std::launch::async, [&] { return stream.grab_frame(); });
    EXPECT_EQ(grabbed.wait_for(50ms), std::future_status::timeout);
    stream.stop();
    EXPECT_EQ(error_of([&] { (void)grabbed.get(); }), Kind::stopped);
    EXPECT_EQ(error_of([&] { (void)stream.next_frame(); }), Kind::stopped) << "stopped already";

    stream.reset();
    std::stop_source cancel;
    auto cancelled = std::async(std::launch::async, [&] { return stream.next_frame(cancel.get_token()); });
    EXPECT_EQ(cancelled.wait_for(50ms), std::future_status::timeout);
    cancel.request_stop();
    EXPECT_EQ(error_of([&] { (void)cancelled.get(); }), Kind::stopped);
}

TEST(MjpegStream, ReceiversGetTheLatestFrameAndEndWhenStopped) {
    const tw::MjpegStream stream;
    auto receiver = stream.frames();
    for (char n = 0; n < 3; ++n) {
        (void)stream.add_frame(jpeg(n));
    }
    // A slow receiver skips to the latest frame.
    EXPECT_EQ(receiver.next()->index, 2u);
    stream.stop();
    EXPECT_FALSE(receiver.next());
    auto late = stream.frames();
    EXPECT_FALSE(late.next()) << "a stopped stream has nothing";
    stream.reset();
    auto again = stream.frames();
    (void)stream.add_frame(jpeg(7));
    EXPECT_EQ((*again.next()->data)[2], 7);
}

TEST(MjpegStream, AReceiverWaitsForTheNextFrame) {
    const tw::MjpegStream stream;
    (void)stream.add_frame(jpeg(0));
    auto receiver = stream.frames();
    auto next = std::async(std::launch::async, [&] { return receiver.next(); });
    EXPECT_EQ(next.wait_for(50ms), std::future_status::timeout) << "frames from now on";
    (void)stream.add_frame(jpeg(1));
    EXPECT_EQ(next.get()->index, 1u);
}

TEST(MjpegStream, NotifiersAreCalledForEveryChangeAndTryNextNeverWaits) {
    const tw::MjpegStream stream;
    auto receiver = stream.frames();
    std::atomic<int> calls = 0;
    receiver.set_notifier([&] { ++calls; });
    EXPECT_FALSE(receiver.try_next());
    for (char n = 0; n < 3; ++n) {
        (void)stream.add_frame(jpeg(n));
        EXPECT_EQ(calls.load(), n + 1) << "every frame, not only the first";
        EXPECT_EQ(receiver.try_next()->index, static_cast<std::uint64_t>(n));
        EXPECT_FALSE(receiver.try_next()) << "nothing new";
    }
    (void)stream.add_frame(jpeg(3));
    (void)stream.add_frame(jpeg(4));
    EXPECT_EQ(receiver.try_next()->index, 4u) << "the latest";
    stream.stop();
    EXPECT_EQ(calls.load(), 6);
    EXPECT_TRUE(receiver.stopped());
    EXPECT_FALSE(receiver.try_next());
    {
        auto gone = stream.frames();
        gone.set_notifier([] { FAIL() << "a receiver's notifier goes with it"; });
    }
    stream.reset();
    EXPECT_EQ(calls.load(), 7);
}
