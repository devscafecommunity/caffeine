#include "catch.hpp"
#include "../src/events/EventBus.hpp"
#include "../src/events/Events.hpp"

#include <thread>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::Events;

struct TestEvent {
    int value;
};

struct OtherEvent {
    float x;
    float y;
};

TEST_CASE("EventBus - subscribe and publish immediate", "[events]") {
    EventBus bus;

    int received = 0;
    bus.subscribe<TestEvent>([&](const TestEvent& e) {
        received = e.value;
    });

    bus.publish(TestEvent{42});
    REQUIRE(received == 42);
}

TEST_CASE("EventBus - multiple listeners for same event", "[events]") {
    EventBus bus;

    int a = 0, b = 0;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { a = e.value; });
    bus.subscribe<TestEvent>([&](const TestEvent& e) { b = e.value * 2; });

    bus.publish(TestEvent{10});
    REQUIRE(a == 10);
    REQUIRE(b == 20);
}

TEST_CASE("EventBus - unsubscribe removes listener", "[events]") {
    EventBus bus;

    int count = 0;
    ListenerHandle h = bus.subscribe<TestEvent>([&](const TestEvent&) { ++count; });

    bus.publish(TestEvent{1});
    REQUIRE(count == 1);

    bus.unsubscribe(h);
    bus.publish(TestEvent{2});
    REQUIRE(count == 1);
}

TEST_CASE("EventBus - unsubscribe invalid handle is no-op", "[events]") {
    EventBus bus;
    REQUIRE_NOTHROW(bus.unsubscribe(9999));
}

TEST_CASE("EventBus - different event types are isolated", "[events]") {
    EventBus bus;

    int testCount = 0;
    int otherCount = 0;
    bus.subscribe<TestEvent>([&](const TestEvent&) { ++testCount; });
    bus.subscribe<OtherEvent>([&](const OtherEvent&) { ++otherCount; });

    bus.publish(TestEvent{1});
    REQUIRE(testCount == 1);
    REQUIRE(otherCount == 0);

    bus.publish(OtherEvent{1.f, 2.f});
    REQUIRE(testCount == 1);
    REQUIRE(otherCount == 1);
}

TEST_CASE("EventBus - publish with no listeners is no-op", "[events]") {
    EventBus bus;
    REQUIRE_NOTHROW(bus.publish(TestEvent{42}));
}

TEST_CASE("EventBus - deferred publish queues event", "[events]") {
    EventBus bus;

    int received = 0;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { received = e.value; });

    bus.publishDeferred(TestEvent{7});
    REQUIRE(received == 0);
    REQUIRE(bus.pendingCount() == 1);
}

TEST_CASE("EventBus - dispatch delivers deferred events", "[events]") {
    EventBus bus;

    int received = 0;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { received = e.value; });

    bus.publishDeferred(TestEvent{99});
    bus.dispatch();

    REQUIRE(received == 99);
    REQUIRE(bus.pendingCount() == 0);
}

TEST_CASE("EventBus - dispatch clears the queue", "[events]") {
    EventBus bus;

    bus.publishDeferred(TestEvent{1});
    bus.publishDeferred(TestEvent{2});
    REQUIRE(bus.pendingCount() == 2);

    bus.dispatch();
    REQUIRE(bus.pendingCount() == 0);
}

TEST_CASE("EventBus - multiple deferred events delivered in order", "[events]") {
    EventBus bus;

    std::vector<int> order;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { order.push_back(e.value); });

    bus.publishDeferred(TestEvent{1});
    bus.publishDeferred(TestEvent{2});
    bus.publishDeferred(TestEvent{3});
    bus.dispatch();

    REQUIRE(order.size() == 3);
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
    REQUIRE(order[2] == 3);
}

TEST_CASE("EventBus - clear removes all listeners and queue", "[events]") {
    EventBus bus;

    int count = 0;
    bus.subscribe<TestEvent>([&](const TestEvent&) { ++count; });
    bus.publishDeferred(TestEvent{1});

    bus.clear();

    bus.publish(TestEvent{2});
    bus.dispatch();

    REQUIRE(count == 0);
    REQUIRE(bus.pendingCount() == 0);
}

TEST_CASE("EventBus - listener handle is unique per subscription", "[events]") {
    EventBus bus;

    ListenerHandle h1 = bus.subscribe<TestEvent>([](const TestEvent&) {});
    ListenerHandle h2 = bus.subscribe<TestEvent>([](const TestEvent&) {});
    ListenerHandle h3 = bus.subscribe<OtherEvent>([](const OtherEvent&) {});

    REQUIRE(h1 != h2);
    REQUIRE(h1 != h3);
    REQUIRE(h2 != h3);
}

TEST_CASE("EventBus - unsubscribe one of many listeners", "[events]") {
    EventBus bus;

    int a = 0, b = 0, c = 0;
    bus.subscribe<TestEvent>([&](const TestEvent&) { ++a; });
    ListenerHandle hb = bus.subscribe<TestEvent>([&](const TestEvent&) { ++b; });
    bus.subscribe<TestEvent>([&](const TestEvent&) { ++c; });

    bus.publish(TestEvent{1});
    REQUIRE(a == 1);
    REQUIRE(b == 1);
    REQUIRE(c == 1);

    bus.unsubscribe(hb);
    bus.publish(TestEvent{1});
    REQUIRE(a == 2);
    REQUIRE(b == 1);
    REQUIRE(c == 2);
}

TEST_CASE("EventBus - deferred mixed event types", "[events]") {
    EventBus bus;

    int testVal = 0;
    float otherVal = 0.f;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { testVal = e.value; });
    bus.subscribe<OtherEvent>([&](const OtherEvent& e) { otherVal = e.x; });

    bus.publishDeferred(TestEvent{55});
    bus.publishDeferred(OtherEvent{3.14f, 0.f});
    bus.dispatch();

    REQUIRE(testVal == 55);
    REQUIRE(otherVal == Approx(3.14f));
}

TEST_CASE("EventBus - predefined OnEntityCreated event", "[events][predefined]") {
    EventBus bus;

    u32 capturedId = 0;
    bus.subscribe<OnEntityCreated>([&](const OnEntityCreated& e) {
        capturedId = e.entityId;
    });

    bus.publish(OnEntityCreated{42u});
    REQUIRE(capturedId == 42u);
}

TEST_CASE("EventBus - predefined OnDeath event", "[events][predefined]") {
    EventBus bus;

    u32 deadEntity = 0;
    u32 killer = 0;
    bus.subscribe<OnDeath>([&](const OnDeath& e) {
        deadEntity = e.entityId;
        killer = e.killerEntityId;
    });

    bus.publish(OnDeath{10u, 0u});
    REQUIRE(deadEntity == 10u);
    REQUIRE(killer == 0u);
}

TEST_CASE("EventBus - predefined OnScoreChanged event", "[events][predefined]") {
    EventBus bus;

    i32 delta = 0;
    bus.subscribe<OnScoreChanged>([&](const OnScoreChanged& e) {
        delta = e.delta;
    });

    bus.publishDeferred(OnScoreChanged{1u, 0, 100, 100});
    bus.dispatch();
    REQUIRE(delta == 100);
}

TEST_CASE("EventBus - thread safety: publishDeferred from multiple threads", "[events][threading]") {
    EventBus bus;

    constexpr int kThreads = 8;
    constexpr int kEventsPerThread = 125;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&bus, i]() {
            for (int j = 0; j < kEventsPerThread; ++j) {
                bus.publishDeferred(TestEvent{i * kEventsPerThread + j});
            }
        });
    }
    for (auto& t : threads) t.join();

    REQUIRE(bus.pendingCount() == static_cast<usize>(kThreads * kEventsPerThread));
}
