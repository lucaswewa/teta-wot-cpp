// teta-wot-core's tests/cancel.rs, ported: resettable cancellation and
// cancellable waits. The async waits are threads here.

#include "teta/wot/cancel.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace {

namespace tw = teta::wot;
using namespace std::chrono_literals;

// Whether `future` is still waiting after a short while.
template <typename T>
bool pending(std::future<T>& future) {
    return future.wait_for(20ms) == std::future_status::timeout;
}

TEST(Cancel, NewTokensAreIndependentAndUncancelled) {
    tw::CancelToken first;
    tw::CancelToken second;
    EXPECT_FALSE(first.is_cancelled());
    EXPECT_FALSE(second.is_cancelled());
    EXPECT_NO_THROW(first.check());
    first.cancel();
    EXPECT_TRUE(first.is_cancelled());
    EXPECT_FALSE(second.is_cancelled());
    EXPECT_NO_THROW(second.check());
}

TEST(Cancel, CopiesShareCancellationAndChecksConsumeItOnce) {
    tw::CancelToken token;
    const tw::CancelToken copy = token;
    token.cancel();
    EXPECT_TRUE(copy.is_cancelled());
    EXPECT_TRUE(copy.is_cancelled());  // Inspecting the flag does not consume it.
    EXPECT_THROW(copy.check(), tw::Cancelled);
    EXPECT_FALSE(token.is_cancelled());
    EXPECT_NO_THROW(token.check());
    copy.cancel();
    EXPECT_THROW(token.check(), tw::Cancelled);
    EXPECT_NO_THROW(copy.check());
}

TEST(Cancel, RepeatedRequestsCoalesceUntilConsumed) {
    tw::CancelToken token;
    token.cancel();
    token.cancel();
    EXPECT_THROW(token.check(), tw::Cancelled);
    EXPECT_NO_THROW(token.check());
    token.cancel();
    EXPECT_THROW(token.check(), tw::Cancelled);
}

TEST(Cancel, ConcurrentChecksHaveExactlyOneConsumer) {
    tw::CancelToken token;
    token.cancel();
    std::barrier start(8);
    std::atomic<int> consumers{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            if (token.take()) {
                ++consumers;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(consumers.load(), 1);
    EXPECT_FALSE(token.is_cancelled());
}

TEST(Cancel, CancelledConsumesPendingRequestsAndCanWaitAgain) {
    tw::CancelToken token;
    token.cancel();
    token.cancelled();
    EXPECT_FALSE(token.is_cancelled());
    auto waiting = std::async(std::launch::async, [&] { token.cancelled(); });
    EXPECT_TRUE(pending(waiting));
    token.cancel();
    waiting.get();
    EXPECT_NO_THROW(token.check());
}

TEST(Cancel, CompetingWaitersConsumeSeparateRequests) {
    tw::CancelToken token;
    std::atomic<int> woken{0};
    auto first = std::async(std::launch::async, [&] {
        token.cancelled();
        ++woken;
    });
    auto second = std::async(std::launch::async, [&] {
        token.cancelled();
        ++woken;
    });
    EXPECT_TRUE(pending(first));
    token.cancel();
    // Both waiters were woken, but only one consumed the flag.
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(woken.load(), 1);
    token.cancel();
    first.get();
    second.get();
    EXPECT_EQ(woken.load(), 2);
    EXPECT_FALSE(token.is_cancelled());
}

TEST(Cancel, SleepCompletesAfterTheDurationWithoutCancellation) {
    tw::CancelToken token;
    const auto started = std::chrono::steady_clock::now();
    EXPECT_NO_THROW(token.sleep(30ms));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 30ms);
    EXPECT_FALSE(token.is_cancelled());
    token.cancel();
    EXPECT_THROW(token.check(), tw::Cancelled);
}

TEST(Cancel, SleepIsInterruptedAndTheTokenCanBeReused) {
    tw::CancelToken token;
    auto sleeping = std::async(std::launch::async, [&] { token.sleep(std::chrono::hours(1)); });
    EXPECT_TRUE(pending(sleeping));
    tw::CancelToken(token).cancel();
    EXPECT_THROW(sleeping.get(), tw::Cancelled);
    EXPECT_FALSE(token.is_cancelled());
    EXPECT_NO_THROW(token.sleep(0ms));
}

TEST(Cancel, PendingCancellationWinsOverAZeroSleep) {
    tw::CancelToken token;
    token.cancel();
    EXPECT_THROW(token.sleep(0ms), tw::Cancelled);
    EXPECT_NO_THROW(token.sleep(0ms));
}

TEST(Cancel, CancellationFromAnotherThreadWakesAWaiter) {
    tw::CancelToken token;
    auto waiting = std::async(std::launch::async, [&] { token.cancelled(); });
    EXPECT_TRUE(pending(waiting));
    std::thread other([token] { token.cancel(); });
    EXPECT_EQ(waiting.wait_for(2s), std::future_status::ready);
    other.join();
    EXPECT_FALSE(token.is_cancelled());
}

TEST(Cancel, CallbacksRunOnEachCancellationUntilUnregistered) {
    tw::CancelToken token;
    int calls = 0;
    {
        auto registration = token.on_cancel([&] { ++calls; });
        token.cancel();
        token.cancel();
    }
    token.cancel();
    EXPECT_EQ(calls, 2);
}

TEST(Cancel, CancelledHasThePublicMessage) {
    EXPECT_STREQ(tw::Cancelled().what(), "The action was cancelled.");
}

}  // namespace
