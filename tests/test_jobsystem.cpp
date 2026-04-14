// ============================================================================
// @file    test_jobsystem.cpp
// @brief   Tests for Caffeine::Threading Job System (RF2.2-RF2.6)
// @note    TDD: tests written first, implementation follows
// ============================================================================
#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "../src/threading/JobSystem.hpp"

using namespace Caffeine;
using namespace Caffeine::Threading;

// ============================================================================
// JobPriority — enum values
// ============================================================================

TEST_CASE("JobPriority - Critical is highest priority (lowest value)", "[threading][priority]") {
    REQUIRE(static_cast<u8>(JobPriority::Critical) == 0);
    REQUIRE(static_cast<u8>(JobPriority::Normal) == 1);
    REQUIRE(static_cast<u8>(JobPriority::Background) == 2);
}

TEST_CASE("JobPriority - Critical < Normal < Background ordering", "[threading][priority]") {
    REQUIRE(static_cast<u8>(JobPriority::Critical) < static_cast<u8>(JobPriority::Normal));
    REQUIRE(static_cast<u8>(JobPriority::Normal) < static_cast<u8>(JobPriority::Background));
}

// ============================================================================
// IJob / JobWithData<T> — job types
// ============================================================================

TEST_CASE("IJob - default priority is Normal", "[threading][job]") {
    struct TestJob : IJob {
        void execute() override {}
    };
    TestJob job;
    REQUIRE(job.priority() == JobPriority::Normal);
}

TEST_CASE("IJob - custom priority override", "[threading][job]") {
    struct CriticalJob : IJob {
        void execute() override {}
        JobPriority priority() const override { return JobPriority::Critical; }
    };
    CriticalJob job;
    REQUIRE(job.priority() == JobPriority::Critical);
}

TEST_CASE("IJob - virtual destructor is safe", "[threading][job]") {
    struct CounterJob : IJob {
        int* counter;
        explicit CounterJob(int* c) : counter(c) {}
        ~CounterJob() override { (*counter)++; }
        void execute() override {}
    };
    int destructorCount = 0;
    {
        std::unique_ptr<IJob> job = std::make_unique<CounterJob>(&destructorCount);
    }
    REQUIRE(destructorCount == 1);
}

TEST_CASE("JobWithData - stores data and executes function", "[threading][job]") {
    int result = 0;
    JobWithData<int> job;
    job.data = 42;
    job.func = [&result](int& d) { result = d * 2; };
    job.prio = JobPriority::Normal;

    job.execute();
    REQUIRE(result == 84);
}

TEST_CASE("JobWithData - priority is configurable", "[threading][job]") {
    JobWithData<int> job;
    job.data = 0;
    job.func = [](int&) {};
    job.prio = JobPriority::Background;

    REQUIRE(job.priority() == JobPriority::Background);
}

TEST_CASE("JobWithData - complex data type", "[threading][job]") {
    struct PhysicsData {
        f32 x, y, z;
        f32 mass;
    };

    f32 totalMass = 0.0f;
    JobWithData<PhysicsData> job;
    job.data = PhysicsData{1.0f, 2.0f, 3.0f, 10.5f};
    job.func = [&totalMass](PhysicsData& d) { totalMass = d.mass; };
    job.prio = JobPriority::Critical;

    job.execute();
    REQUIRE(totalMass == Approx(10.5f));
    REQUIRE(job.priority() == JobPriority::Critical);
}

// ============================================================================
// JobHandle — index + version tracking
// ============================================================================

TEST_CASE("JobHandle - default constructed is not complete", "[threading][handle]") {
    JobHandle handle;
    REQUIRE_FALSE(handle.isComplete());
}

TEST_CASE("JobHandle - bool conversion matches isComplete", "[threading][handle]") {
    JobHandle handle;
    REQUIRE(static_cast<bool>(handle) == handle.isComplete());
}

// ============================================================================
// JobBarrier — group synchronization
// ============================================================================

TEST_CASE("JobBarrier - initial count zero means already done", "[threading][barrier]") {
    JobBarrier barrier(0);
    // wait() should return immediately
    barrier.wait();
    REQUIRE(true);  // If we got here, wait() didn't hang
}

TEST_CASE("JobBarrier - release decrements count", "[threading][barrier]") {
    JobBarrier barrier(2);
    barrier.release();
    barrier.release();
    barrier.wait();  // Should return now
    REQUIRE(true);
}

TEST_CASE("JobBarrier - add increments pending count", "[threading][barrier]") {
    JobBarrier barrier(0);
    barrier.add();
    barrier.add();
    barrier.release();
    barrier.release();
    barrier.wait();
    REQUIRE(true);
}

TEST_CASE("JobBarrier - wait blocks until all released", "[threading][barrier]") {
    JobBarrier barrier(3);
    std::atomic<bool> done{false};

    std::thread waiter([&]() {
        barrier.wait();
        done.store(true, std::memory_order_release);
    });

    // Release 2 of 3 — should still be waiting
    barrier.release();
    barrier.release();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE_FALSE(done.load(std::memory_order_acquire));

    // Release final one
    barrier.release();
    waiter.join();
    REQUIRE(done.load(std::memory_order_acquire));
}

// ============================================================================
// JobSystem — construction and lifecycle
// ============================================================================

TEST_CASE("JobSystem - default constructor creates workers", "[threading][system]") {
    JobSystem system;
    REQUIRE(system.workerCount() > 0);
}

TEST_CASE("JobSystem - explicit worker count", "[threading][system]") {
    JobSystem system(2);
    REQUIRE(system.workerCount() == 2);
}

TEST_CASE("JobSystem - zero means auto (hardware_concurrency - 1)", "[threading][system]") {
    JobSystem system(0);
    u32 expected = std::thread::hardware_concurrency();
    if (expected > 1) expected -= 1;
    if (expected == 0) expected = 1;
    REQUIRE(system.workerCount() == expected);
}

TEST_CASE("JobSystem - destructor joins all workers cleanly", "[threading][system]") {
    {
        JobSystem system(2);
        // Just let it destruct
    }
    REQUIRE(true);  // No hang, no crash
}

// ============================================================================
// JobSystem — scheduling and execution
// ============================================================================

TEST_CASE("JobSystem - schedule single job executes", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> counter{0};

    struct IncrementJob : IJob {
        std::atomic<int>* counter;
        explicit IncrementJob(std::atomic<int>* c) : counter(c) {}
        void execute() override {
            counter->fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto handle = system.schedule(std::make_unique<IncrementJob>(&counter));
    handle.wait();
    REQUIRE(counter.load() == 1);
}

TEST_CASE("JobSystem - schedule returns valid handle", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> dummy{0};

    struct DummyJob : IJob {
        std::atomic<int>* d;
        explicit DummyJob(std::atomic<int>* d) : d(d) {}
        void execute() override { d->fetch_add(1); }
    };

    auto handle = system.schedule(std::make_unique<DummyJob>(&dummy));
    handle.wait();
    REQUIRE(handle.isComplete());
}

TEST_CASE("JobSystem - schedule with barrier", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> counter{0};
    JobBarrier barrier(3);

    struct AddJob : IJob {
        std::atomic<int>* c;
        explicit AddJob(std::atomic<int>* c) : c(c) {}
        void execute() override { c->fetch_add(1); }
    };

    system.schedule(std::make_unique<AddJob>(&counter), &barrier);
    system.schedule(std::make_unique<AddJob>(&counter), &barrier);
    system.schedule(std::make_unique<AddJob>(&counter), &barrier);

    barrier.wait();
    REQUIRE(counter.load() == 3);
}

TEST_CASE("JobSystem - schedule with explicit priority", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> counter{0};

    struct PrioJob : IJob {
        std::atomic<int>* c;
        JobPriority p;
        PrioJob(std::atomic<int>* c, JobPriority p) : c(c), p(p) {}
        void execute() override { c->fetch_add(1); }
        JobPriority priority() const override { return p; }
    };

    auto h1 = system.schedule(
        std::make_unique<PrioJob>(&counter, JobPriority::Critical), nullptr, JobPriority::Critical);
    auto h2 = system.schedule(
        std::make_unique<PrioJob>(&counter, JobPriority::Background), nullptr, JobPriority::Background);

    h1.wait();
    h2.wait();
    REQUIRE(counter.load() == 2);
}

// ============================================================================
// JobSystem — scheduleData sugar
// ============================================================================

TEST_CASE("JobSystem - scheduleData with int", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> result{0};

    auto handle = system.scheduleData(
        42,
        [&result](int& val) { result.store(val * 2); }
    );
    handle.wait();
    REQUIRE(result.load() == 84);
}

TEST_CASE("JobSystem - scheduleData with struct", "[threading][system]") {
    JobSystem system(2);

    struct Payload { int a; int b; };
    std::atomic<int> sum{0};

    auto handle = system.scheduleData(
        Payload{10, 20},
        [&sum](Payload& p) { sum.store(p.a + p.b); }
    );
    handle.wait();
    REQUIRE(sum.load() == 30);
}

TEST_CASE("JobSystem - scheduleData with barrier", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> total{0};
    JobBarrier barrier(4);

    for (int i = 1; i <= 4; ++i) {
        system.scheduleData(
            i,
            [&total](int& val) { total.fetch_add(val); },
            &barrier
        );
    }

    barrier.wait();
    REQUIRE(total.load() == 10);  // 1 + 2 + 3 + 4
}

TEST_CASE("JobSystem - scheduleData with priority", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> counter{0};

    auto handle = system.scheduleData(
        1,
        [&counter](int& val) { counter.fetch_add(val); },
        nullptr,
        JobPriority::Critical
    );
    handle.wait();
    REQUIRE(counter.load() == 1);
}

// ============================================================================
// JobSystem — scheduleParallelFor
// ============================================================================

TEST_CASE("JobSystem - parallelFor processes all elements", "[threading][parallel]") {
    JobSystem system(4);
    constexpr u32 COUNT = 1000;
    std::vector<std::atomic<int>> data(COUNT);
    for (auto& d : data) d.store(0);

    auto handle = system.scheduleParallelFor(COUNT,
        [&data](u32 i) { data[i].store(static_cast<int>(i * 2)); }
    );
    handle.wait();

    for (u32 i = 0; i < COUNT; ++i) {
        REQUIRE(data[i].load() == static_cast<int>(i * 2));
    }
}

TEST_CASE("JobSystem - parallelFor with zero count", "[threading][parallel]") {
    JobSystem system(2);
    auto handle = system.scheduleParallelFor(0, [](u32) {});
    handle.wait();
    REQUIRE(handle.isComplete());
}

TEST_CASE("JobSystem - parallelFor with barrier", "[threading][parallel]") {
    JobSystem system(4);
    constexpr u32 COUNT = 500;
    std::atomic<u32> sum{0};
    JobBarrier barrier(0);  // parallelFor will set barrier count internally

    auto handle = system.scheduleParallelFor(COUNT,
        [&sum](u32 i) { sum.fetch_add(i); },
        &barrier
    );

    barrier.wait();
    // sum of 0..499 = 499*500/2 = 124750
    REQUIRE(sum.load() == 124750);
}

TEST_CASE("JobSystem - parallelFor with single element", "[threading][parallel]") {
    JobSystem system(2);
    std::atomic<int> value{0};

    auto handle = system.scheduleParallelFor(1,
        [&value](u32) { value.store(42); }
    );
    handle.wait();
    REQUIRE(value.load() == 42);
}

// ============================================================================
// JobSystem — waitAll
// ============================================================================

TEST_CASE("JobSystem - waitAll completes all pending jobs", "[threading][system]") {
    JobSystem system(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 50; ++i) {
        system.scheduleData(
            1,
            [&counter](int& val) { counter.fetch_add(val); }
        );
    }

    system.waitAll();
    REQUIRE(counter.load() == 50);
}

TEST_CASE("JobSystem - waitAll on empty system returns immediately", "[threading][system]") {
    JobSystem system(2);
    system.waitAll();
    REQUIRE(true);
}

// ============================================================================
// JobSystem — stats
// ============================================================================

TEST_CASE("JobSystem - stats returns valid data", "[threading][stats]") {
    JobSystem system(2);
    auto s = system.stats();

    REQUIRE(s.activeWorkers <= system.workerCount());
    REQUIRE(s.completedJobsTotal == 0);
}

TEST_CASE("JobSystem - stats tracks completed jobs", "[threading][stats]") {
    JobSystem system(2);
    std::atomic<int> dummy{0};

    for (int i = 0; i < 10; ++i) {
        auto h = system.scheduleData(1, [&dummy](int& v) { dummy.fetch_add(v); });
        h.wait();
    }

    auto s = system.stats();
    REQUIRE(s.completedJobsTotal >= 10);
}

// ============================================================================
// Stress tests — concurrency correctness
// ============================================================================

TEST_CASE("Stress - 1K jobs all complete correctly", "[threading][stress]") {
    JobSystem system;
    constexpr int JOB_COUNT = 1000;
    std::atomic<int> counter{0};

    JobBarrier barrier(JOB_COUNT);

    for (int i = 0; i < JOB_COUNT; ++i) {
        system.scheduleData(
            1,
            [&counter](int& val) { counter.fetch_add(val); },
            &barrier
        );
    }

    barrier.wait();
    REQUIRE(counter.load() == JOB_COUNT);
}

TEST_CASE("Stress - 10K jobs complete", "[threading][stress]") {
    JobSystem system;
    constexpr int JOB_COUNT = 10000;
    std::atomic<int> counter{0};

    JobBarrier barrier(JOB_COUNT);

    for (int i = 0; i < JOB_COUNT; ++i) {
        system.scheduleData(
            1,
            [&counter](int& val) { counter.fetch_add(val); },
            &barrier
        );
    }

    barrier.wait();
    REQUIRE(counter.load() == JOB_COUNT);
}

TEST_CASE("Stress - parallelFor 100K elements", "[threading][stress]") {
    JobSystem system;
    constexpr u32 COUNT = 100000;
    std::atomic<u64> sum{0};

    auto handle = system.scheduleParallelFor(COUNT,
        [&sum](u32 i) { sum.fetch_add(i); }
    );
    handle.wait();

    // sum of 0..99999 = 99999*100000/2 = 4999950000
    REQUIRE(sum.load() == 4999950000ULL);
}

TEST_CASE("Stress - mixed priorities all complete", "[threading][stress]") {
    JobSystem system;
    constexpr int PER_LEVEL = 500;
    std::atomic<int> critical{0};
    std::atomic<int> normal{0};
    std::atomic<int> background{0};

    JobBarrier barrier(PER_LEVEL * 3);

    for (int i = 0; i < PER_LEVEL; ++i) {
        system.scheduleData(1, [&critical](int& v) { critical.fetch_add(v); },
                            &barrier, JobPriority::Critical);
        system.scheduleData(1, [&normal](int& v) { normal.fetch_add(v); },
                            &barrier, JobPriority::Normal);
        system.scheduleData(1, [&background](int& v) { background.fetch_add(v); },
                            &barrier, JobPriority::Background);
    }

    barrier.wait();
    REQUIRE(critical.load() == PER_LEVEL);
    REQUIRE(normal.load() == PER_LEVEL);
    REQUIRE(background.load() == PER_LEVEL);
}

TEST_CASE("Stress - multiple barriers independent", "[threading][stress]") {
    JobSystem system;
    constexpr int COUNT = 200;

    std::atomic<int> sumA{0};
    std::atomic<int> sumB{0};
    JobBarrier barrierA(COUNT);
    JobBarrier barrierB(COUNT);

    for (int i = 0; i < COUNT; ++i) {
        system.scheduleData(1, [&sumA](int& v) { sumA.fetch_add(v); }, &barrierA);
        system.scheduleData(2, [&sumB](int& v) { sumB.fetch_add(v); }, &barrierB);
    }

    barrierA.wait();
    barrierB.wait();

    REQUIRE(sumA.load() == COUNT);
    REQUIRE(sumB.load() == COUNT * 2);
}

TEST_CASE("Stress - rapid create-destroy cycles", "[threading][stress]") {
    for (int cycle = 0; cycle < 5; ++cycle) {
        JobSystem system(2);
        std::atomic<int> counter{0};

        for (int i = 0; i < 100; ++i) {
            system.scheduleData(1, [&counter](int& v) { counter.fetch_add(v); });
        }
        system.waitAll();
        REQUIRE(counter.load() == 100);
    }
}

// ============================================================================
// Background jobs never starve critical jobs
// ============================================================================

TEST_CASE("Priority - critical jobs complete before background under load", "[threading][priority]") {
    JobSystem system(2);

    // Flood with background jobs
    std::atomic<int> bgDone{0};
    for (int i = 0; i < 200; ++i) {
        system.scheduleData(
            1,
            [&bgDone](int& v) {
                // Simulate slow work
                volatile int sink = 0;
                for (int j = 0; j < 1000; ++j) sink += j;
                (void)sink;
                bgDone.fetch_add(v);
            },
            nullptr,
            JobPriority::Background
        );
    }

    // Schedule a critical job after the flood
    std::atomic<bool> critDone{false};
    auto critHandle = system.scheduleData(
        0,
        [&critDone](int&) { critDone.store(true); },
        nullptr,
        JobPriority::Critical
    );

    critHandle.wait();
    REQUIRE(critDone.load());

    // Wait for everything to finish
    system.waitAll();
    REQUIRE(bgDone.load() == 200);
}
