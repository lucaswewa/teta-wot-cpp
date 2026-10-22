// teta-wot-core's broker unit tests, ported, and the global lock's.

#include "teta/wot/broker.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include "teta/wot/lock.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

tw::Message message(const char* affordance, Value payload) {
    return tw::Message::now("t", affordance, tw::MessageKind::property, std::move(payload));
}

TEST(Broker, OneSubscriptionReceivesSeveralAffordancesOnceEach) {
    tw::MessageBroker broker;
    auto subscription = broker.subscription();
    EXPECT_TRUE(broker.add(subscription, "t", "a"));
    EXPECT_TRUE(broker.add(subscription, "t", "b"));
    EXPECT_FALSE(broker.add(subscription, "t", "a"));  // already subscribed
    broker.publish(message("a", 1));
    broker.publish(message("b", 2));
    broker.publish(message("c", 3));
    EXPECT_EQ(subscription.recv()->payload, Value(1));
    EXPECT_EQ(subscription.recv()->payload, Value(2));
    EXPECT_FALSE(subscription.try_recv());
}

TEST(Broker, AFullBufferDropsAndCounts) {
    tw::MessageBroker broker;
    auto subscription = broker.subscribe("t", "a");
    for (int i = 0; i < 8; ++i) {
        broker.publish(message("a", i));
    }
    EXPECT_EQ(subscription.dropped(), 3u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(subscription.recv()->payload, Value(i));
    }
    EXPECT_FALSE(subscription.try_recv());
}

TEST(Broker, DestroyingASubscriptionUnsubscribes) {
    tw::MessageBroker broker;
    {
        auto subscription = broker.subscribe("t", "a");
        EXPECT_TRUE(broker.has_subscribers("t", "a"));
    }
    EXPECT_FALSE(broker.has_subscribers("t", "a"));
    broker.publish(message("a", 1));
}

TEST(Broker, ClosingEndsSubscriptionsAfterTheirBuffer) {
    tw::MessageBroker broker;
    auto subscription = broker.subscribe("t", "a");
    broker.publish(message("a", 1));
    broker.close();
    EXPECT_EQ(subscription.recv()->payload, Value(1));
    EXPECT_FALSE(subscription.recv());
    auto late = broker.subscribe("t", "a");
    EXPECT_FALSE(broker.has_subscribers("t", "a"));
    EXPECT_FALSE(late.recv());
}

TEST(Broker, ARecvWaitsForAMessageOrTheClose) {
    tw::MessageBroker broker;
    auto subscription = broker.subscribe("t", "a");
    auto received = std::async(std::launch::async, [&] { return subscription.recv(); });
    EXPECT_EQ(received.wait_for(20ms), std::future_status::timeout);
    broker.publish(message("a", 7));
    EXPECT_EQ(received.get()->payload, Value(7));
    EXPECT_FALSE(subscription.recv_for(10ms));
    int notified = 0;
    subscription.set_notifier([&] { ++notified; });
    auto ending = std::async(std::launch::async, [&] { return subscription.recv(); });
    broker.close();
    EXPECT_FALSE(ending.get());
    EXPECT_EQ(notified, 1);
    EXPECT_TRUE(subscription.finished());
}

TEST(GlobalLock, IsReentrantPerOwnerAndTimesOut) {
    const auto lock = tw::GlobalLock::create(10ms);
    EXPECT_EQ(lock->timeout(), 10ms);
    const auto owner = tw::Uuid::v4();
    {
        auto first = lock->try_acquire(owner);
        auto second = lock->acquire(owner);
        EXPECT_EQ(lock->owner(), owner);
        const auto started = std::chrono::steady_clock::now();
        EXPECT_THROW((void)lock->acquire(tw::Uuid::v4()), tw::GlobalLockBusy);
        EXPECT_GE(std::chrono::steady_clock::now() - started, 10ms);
        EXPECT_THROW((void)lock->try_acquire(tw::Uuid::v4()), tw::GlobalLockBusy);
        first.release();
        EXPECT_EQ(lock->owner(), owner);
    }
    EXPECT_EQ(lock->owner(), std::nullopt);
}

TEST(GlobalLock, AWaiterGetsTheLockWhenItIsReleased) {
    const auto lock = tw::GlobalLock::create(2s);
    auto held = lock->try_acquire(tw::Uuid::v4());
    const auto other = tw::Uuid::v4();
    auto waiting = std::async(std::launch::async, [&] { return lock->acquire(other); });
    EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
    held.release();
    auto guard = waiting.get();
    EXPECT_EQ(lock->owner(), other);
}

TEST(Uuid, ParsesAndWritesTheHyphenatedForm) {
    const auto id = tw::Uuid::v4();
    EXPECT_EQ(tw::Uuid::parse(id.to_string()), id);
    EXPECT_EQ(id.to_string()[14], '4');
    EXPECT_FALSE(tw::Uuid::parse("not-a-uuid"));
    EXPECT_EQ(tw::Uuid::parse("F46F31E7-A858-5FD5-91B0-829D229ED6CD")->to_string(),
              "f46f31e7-a858-5fd5-91b0-829d229ed6cd");
    EXPECT_TRUE(tw::Uuid().is_nil());
}

}  // namespace
