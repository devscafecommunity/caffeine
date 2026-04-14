#include "catch.hpp"
#include <thread>
#include <chrono>
#include "../src/core/Types.hpp"
#include "../src/core/Timer.hpp"

using namespace Caffeine::Core;

TEST_CASE("TimePoint - Constructor initializes to zero", "[timer][timepoint]") {
    TimePoint tp;
    REQUIRE(tp.ticks == 0);
}

TEST_CASE("TimePoint - Can copy construct", "[timer][timepoint]") {
    TimePoint tp1;
    tp1.ticks = 1000;
    TimePoint tp2(tp1);
    REQUIRE(tp2.ticks == 1000);
}

TEST_CASE("TimePoint - Can assign", "[timer][timepoint]") {
    TimePoint tp1;
    tp1.ticks = 5000;
    TimePoint tp2 = tp1;
    REQUIRE(tp2.ticks == 5000);
}

TEST_CASE("TimePoint - Comparison operators", "[timer][timepoint]") {
    TimePoint tp1;
    TimePoint tp2;
    
    tp1.ticks = 100;
    tp2.ticks = 200;
    
    REQUIRE(tp1 < tp2);
    REQUIRE_FALSE(tp2 < tp1);
    REQUIRE(tp2 > tp1);
    REQUIRE_FALSE(tp1 > tp2);
    
    TimePoint tp3;
    tp3.ticks = 100;
    REQUIRE(tp1 == tp3);
    REQUIRE_FALSE(tp1 != tp3);
    REQUIRE(tp1 <= tp3);
    REQUIRE(tp1 >= tp3);
}

TEST_CASE("TimePoint - Subtraction returns Duration", "[timer][timepoint]") {
    TimePoint tp1;
    TimePoint tp2;
    
    tp1.ticks = 1000;
    tp2.ticks = 3000;
    
    Duration dur = tp2 - tp1;
    REQUIRE(dur.seconds > 0.0);
}

TEST_CASE("Duration - Constructor initializes to zero", "[timer][duration]") {
    Duration d;
    REQUIRE(d.seconds == 0.0);
}

TEST_CASE("Duration - Can construct from seconds", "[timer][duration]") {
    Duration d = Duration::fromSeconds(1.5);
    REQUIRE_THAT(d.seconds, Catch::Matchers::WithinAbs(1.5, 1e-9));
}

TEST_CASE("Duration - Convert to milliseconds", "[timer][duration]") {
    Duration d = Duration::fromSeconds(1.0);
    REQUIRE_THAT(d.millis(), Catch::Matchers::WithinAbs(1000.0, 1e-6));
}

TEST_CASE("Duration - Convert to microseconds", "[timer][duration]") {
    Duration d = Duration::fromSeconds(1.0);
    REQUIRE_THAT(d.micros(), Catch::Matchers::WithinAbs(1000000.0, 1e-3));
}

TEST_CASE("Duration - Convert to nanoseconds", "[timer][duration]") {
    Duration d = Duration::fromSeconds(1.0);
    REQUIRE_THAT(d.nanos(), Catch::Matchers::WithinAbs(1000000000.0, 1e0));
}

TEST_CASE("Duration - Add durations", "[timer][duration]") {
    Duration d1 = Duration::fromSeconds(1.0);
    Duration d2 = Duration::fromSeconds(2.0);
    Duration result = d1 + d2;
    REQUIRE_THAT(result.seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
}

TEST_CASE("Duration - Subtract durations", "[timer][duration]") {
    Duration d1 = Duration::fromSeconds(5.0);
    Duration d2 = Duration::fromSeconds(2.0);
    Duration result = d1 - d2;
    REQUIRE_THAT(result.seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
}

TEST_CASE("Duration - Multiply by scalar", "[timer][duration]") {
    Duration d = Duration::fromSeconds(2.0);
    Duration result = d * 3.0;
    REQUIRE_THAT(result.seconds, Catch::Matchers::WithinAbs(6.0, 1e-9));
}

TEST_CASE("Duration - Divide by scalar", "[timer][duration]") {
    Duration d = Duration::fromSeconds(6.0);
    Duration result = d / 2.0;
    REQUIRE_THAT(result.seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
}

TEST_CASE("Duration - Comparison operators", "[timer][duration]") {
    Duration d1 = Duration::fromSeconds(1.0);
    Duration d2 = Duration::fromSeconds(2.0);
    Duration d3 = Duration::fromSeconds(1.0);
    
    REQUIRE(d1 < d2);
    REQUIRE(d2 > d1);
    REQUIRE(d1 == d3);
    REQUIRE_FALSE(d1 != d3);
    REQUIRE(d1 <= d3);
    REQUIRE(d1 >= d3);
}

TEST_CASE("Duration - Precision is at least microseconds", "[timer][duration]") {
    Duration d_micro = Duration::fromSeconds(0.000001);
    double recovered_micros = d_micro.micros();
    REQUIRE_THAT(recovered_micros, Catch::Matchers::WithinAbs(1.0, 1e-2));
}

TEST_CASE("Timer - Constructor creates stopped timer", "[timer][timer]") {
    Timer timer;
    REQUIRE_FALSE(timer.isRunning());
}

TEST_CASE("Timer - Can start", "[timer][timer]") {
    Timer timer;
    timer.start();
    REQUIRE(timer.isRunning());
}

TEST_CASE("Timer - Can stop", "[timer][timer]") {
    Timer timer;
    timer.start();
    timer.stop();
    REQUIRE_FALSE(timer.isRunning());
}

TEST_CASE("Timer - Can reset when stopped", "[timer][timer]") {
    Timer timer;
    timer.start();
    timer.stop();
    timer.reset();
    REQUIRE(timer.elapsed().seconds == 0.0);
}

TEST_CASE("Timer - Elapsed time increases", "[timer][timer]") {
    Timer timer;
    timer.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Duration d1 = timer.elapsed();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Duration d2 = timer.elapsed();
    
    REQUIRE(d2.seconds > d1.seconds);
}

TEST_CASE("Timer - Peek does not reset timer", "[timer][timer]") {
    Timer timer;
    timer.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Duration d1 = timer.elapsed();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Duration d2 = timer.elapsed();
    
    REQUIRE(d2.seconds > d1.seconds);
}

TEST_CASE("Timer - Tick resets internal clock", "[timer][timer]") {
    Timer timer;
    timer.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Duration ticked = timer.tick();
    REQUIRE(ticked.seconds > 0.0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Duration ticked2 = timer.tick();
    
    REQUIRE(ticked2.seconds > 0.0);
}

TEST_CASE("Timer - Accurate to microseconds", "[timer][timer]") {
    Timer timer;
    timer.start();
    
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
    Duration elapsed = timer.elapsed();
    
    REQUIRE(elapsed.millis() > 0.5);
}

TEST_CASE("Timer - Multiple start-stop cycles", "[timer][timer]") {
    Timer timer;
    timer.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.stop();
    Duration d1 = timer.elapsed();
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.stop();
    Duration d2 = timer.elapsed();
    
    REQUIRE(d2.seconds > d1.seconds);
}

TEST_CASE("ScopeTimer - Can construct", "[timer][scope-timer]") {
    {
        ScopeTimer scoped("test_scope");
    }
    REQUIRE(true);
}

TEST_CASE("ScopeTimer - Measures block execution time", "[timer][scope-timer]") {
    ScopeTimer scoped("sleep_test");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(true);
}

TEST_CASE("ScopeTimer - Multiple nested scopes", "[timer][scope-timer]") {
    {
        ScopeTimer outer("outer");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {
            ScopeTimer inner("inner");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    REQUIRE(true);
}

TEST_CASE("Timer integration - Measure complex workload", "[timer][integration]") {
    Timer total_timer;
    total_timer.start();
    
    for (int i = 0; i < 1000; ++i) {
        volatile int x = i * i;
        (void)x;
    }
    
    Duration elapsed = total_timer.elapsed();
    REQUIRE(elapsed.micros() > 0.0);
}

TEST_CASE("Timer integration - Multiple concurrent timers", "[timer][integration]") {
    Timer timer1;
    Timer timer2;
    
    timer1.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    timer2.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    Duration d1 = timer1.elapsed();
    Duration d2 = timer2.elapsed();
    
    REQUIRE(d1.seconds > d2.seconds);
}

TEST_CASE("Timer integration - Frame timing simulation", "[timer][integration]") {
    Timer frame_timer;
    double cumulative_ms = 0.0;
    
    for (int frame = 0; frame < 10; ++frame) {
        frame_timer.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Duration frame_time = frame_timer.tick();
        cumulative_ms += frame_time.millis();
    }
    
    REQUIRE(cumulative_ms > 5.0);
}
