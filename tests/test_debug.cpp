#include "catch.hpp"
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include "../src/debug/LogSystem.hpp"
#include "../src/debug/Profiler.hpp"

using namespace Caffeine;
using namespace Caffeine::Debug;

// ============================================================================
// LogSystem — LogLevel enum
// ============================================================================

TEST_CASE("LogLevel - Trace is lowest", "[debug][log]") {
    REQUIRE(static_cast<u8>(LogLevel::Trace) < static_cast<u8>(LogLevel::Info));
    REQUIRE(static_cast<u8>(LogLevel::Info) < static_cast<u8>(LogLevel::Warn));
    REQUIRE(static_cast<u8>(LogLevel::Warn) < static_cast<u8>(LogLevel::Error));
    REQUIRE(static_cast<u8>(LogLevel::Error) < static_cast<u8>(LogLevel::Fatal));
}

TEST_CASE("LogLevel - levelToString returns correct names", "[debug][log]") {
    REQUIRE(std::string(LogSystem::levelToString(LogLevel::Trace)) == "TRACE");
    REQUIRE(std::string(LogSystem::levelToString(LogLevel::Info))  == "INFO");
    REQUIRE(std::string(LogSystem::levelToString(LogLevel::Warn))  == "WARN");
    REQUIRE(std::string(LogSystem::levelToString(LogLevel::Error)) == "ERROR");
    REQUIRE(std::string(LogSystem::levelToString(LogLevel::Fatal)) == "FATAL");
}

// ============================================================================
// LogSystem — Singleton
// ============================================================================

TEST_CASE("LogSystem - instance returns same object", "[debug][log]") {
    auto& a = LogSystem::instance();
    auto& b = LogSystem::instance();
    REQUIRE(&a == &b);
}

// ============================================================================
// LogSystem — Level filtering
// ============================================================================

TEST_CASE("LogSystem - setLevel filters lower levels", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    int callCount = 0;
    log.addSink([&](LogLevel, const char*, const char*) { ++callCount; });

    log.setLevel(LogLevel::Warn);
    log.log(LogLevel::Trace, "Test", "should be filtered");
    log.log(LogLevel::Info,  "Test", "should be filtered");
    log.log(LogLevel::Warn,  "Test", "should pass");
    log.log(LogLevel::Error, "Test", "should pass");

    REQUIRE(callCount == 2);

    log.clearSinks();
    log.setLevel(LogLevel::Trace);
}

TEST_CASE("LogSystem - getLevel returns current minimum level", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.setLevel(LogLevel::Error);
    REQUIRE(log.getLevel() == LogLevel::Error);
    log.setLevel(LogLevel::Trace);
}

// ============================================================================
// LogSystem — Sinks
// ============================================================================

TEST_CASE("LogSystem - addSink receives formatted message", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    LogLevel receivedLevel = LogLevel::Trace;
    std::string receivedCategory;
    std::string receivedMessage;

    log.addSink([&](LogLevel level, const char* cat, const char* msg) {
        receivedLevel = level;
        receivedCategory = cat;
        receivedMessage = msg;
    });

    log.log(LogLevel::Info, "Physics", "velocity = %d", 42);

    REQUIRE(receivedLevel == LogLevel::Info);
    REQUIRE(receivedCategory == "Physics");
    REQUIRE(receivedMessage == "velocity = 42");

    log.clearSinks();
}

TEST_CASE("LogSystem - multiple sinks all receive messages", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    int sink1Count = 0;
    int sink2Count = 0;
    log.addSink([&](LogLevel, const char*, const char*) { ++sink1Count; });
    log.addSink([&](LogLevel, const char*, const char*) { ++sink2Count; });

    log.log(LogLevel::Info, "Test", "hello");

    REQUIRE(sink1Count == 1);
    REQUIRE(sink2Count == 1);

    log.clearSinks();
}

TEST_CASE("LogSystem - sinkCount tracks added sinks", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    REQUIRE(log.sinkCount() == 0);

    log.addSink([](LogLevel, const char*, const char*) {});
    REQUIRE(log.sinkCount() == 1);

    log.addSink([](LogLevel, const char*, const char*) {});
    REQUIRE(log.sinkCount() == 2);

    log.clearSinks();
    REQUIRE(log.sinkCount() == 0);
}

// ============================================================================
// LogSystem — Category filtering
// ============================================================================

TEST_CASE("LogSystem - category enabled by default", "[debug][log]") {
    auto& log = LogSystem::instance();
    REQUIRE(log.isCategoryEnabled("NewCategory") == true);
}

TEST_CASE("LogSystem - setCategoryEnabled disables category", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    int callCount = 0;
    log.addSink([&](LogLevel, const char*, const char*) { ++callCount; });

    log.setCategoryEnabled("Physics", false);
    log.log(LogLevel::Info, "Physics", "should be filtered");
    log.log(LogLevel::Info, "Audio", "should pass");

    REQUIRE(callCount == 1);

    log.setCategoryEnabled("Physics", true);
    log.clearSinks();
}

TEST_CASE("LogSystem - re-enable category works", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    int callCount = 0;
    log.addSink([&](LogLevel, const char*, const char*) { ++callCount; });

    log.setCategoryEnabled("ECS", false);
    log.log(LogLevel::Info, "ECS", "filtered");
    REQUIRE(callCount == 0);

    log.setCategoryEnabled("ECS", true);
    log.log(LogLevel::Info, "ECS", "passes now");
    REQUIRE(callCount == 1);

    log.clearSinks();
}

// ============================================================================
// LogSystem — Format strings
// ============================================================================

TEST_CASE("LogSystem - format string with multiple args", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    std::string msg;
    log.addSink([&](LogLevel, const char*, const char* m) { msg = m; });

    log.log(LogLevel::Info, "Test", "x=%d y=%.1f name=%s", 10, 3.14, "hello");

    REQUIRE(msg == "x=10 y=3.1 name=hello");

    log.clearSinks();
}

// ============================================================================
// LogSystem — Thread safety
// ============================================================================

TEST_CASE("LogSystem - concurrent logging does not crash", "[debug][log]") {
    auto& log = LogSystem::instance();
    log.clearSinks();
    log.setLevel(LogLevel::Trace);

    int callCount = 0;
    std::mutex countMutex;
    log.addSink([&](LogLevel, const char*, const char*) {
        std::lock_guard<std::mutex> lock(countMutex);
        ++callCount;
    });

    constexpr int THREADS = 4;
    constexpr int MESSAGES_PER_THREAD = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
                log.log(LogLevel::Info, "Thread", "msg %d from thread %d", i, t);
            }
        });
    }

    for (auto& th : threads) th.join();

    REQUIRE(callCount == THREADS * MESSAGES_PER_THREAD);

    log.clearSinks();
}

// ============================================================================
// Profiler — Singleton
// ============================================================================

TEST_CASE("Profiler - instance returns same object", "[debug][profiler]") {
    auto& a = Profiler::instance();
    auto& b = Profiler::instance();
    REQUIRE(&a == &b);
}

// ============================================================================
// Profiler — Scope measurement
// ============================================================================

TEST_CASE("Profiler - beginScope/endScope records a call", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    prof.beginScope("TestScope");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    prof.endScope("TestScope");

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 1);
    REQUIRE(std::string(stats[0].name) == "TestScope");
    REQUIRE(stats[0].callCount == 1);
    REQUIRE(stats[0].totalMs > 0.0);
}

TEST_CASE("Profiler - multiple calls accumulate stats", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    for (int i = 0; i < 3; ++i) {
        prof.beginScope("Loop");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        prof.endScope("Loop");
    }

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 1);
    REQUIRE(stats[0].callCount == 3);
    REQUIRE(stats[0].avgMs > 0.0);
    REQUIRE(stats[0].totalMs >= stats[0].avgMs);
}

TEST_CASE("Profiler - min/max tracked correctly", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    prof.beginScope("MinMax");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    prof.endScope("MinMax");

    prof.beginScope("MinMax");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    prof.endScope("MinMax");

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 1);
    REQUIRE(stats[0].minMs < stats[0].maxMs);
    REQUIRE(stats[0].minMs > 0.0);
}

TEST_CASE("Profiler - multiple named scopes are independent", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    prof.beginScope("ScopeA");
    prof.endScope("ScopeA");

    prof.beginScope("ScopeB");
    prof.endScope("ScopeB");

    prof.beginScope("ScopeA");
    prof.endScope("ScopeA");

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 2);

    bool foundA = false, foundB = false;
    for (usize i = 0; i < stats.size(); ++i) {
        if (std::string(stats[i].name) == "ScopeA") {
            REQUIRE(stats[i].callCount == 2);
            foundA = true;
        }
        if (std::string(stats[i].name) == "ScopeB") {
            REQUIRE(stats[i].callCount == 1);
            foundB = true;
        }
    }
    REQUIRE(foundA);
    REQUIRE(foundB);
}

// ============================================================================
// Profiler — Enable/disable
// ============================================================================

TEST_CASE("Profiler - disabled profiler does not record", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(false);

    prof.beginScope("Disabled");
    prof.endScope("Disabled");

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 0);

    prof.setEnabled(true);
}

TEST_CASE("Profiler - reset clears all stats", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.setEnabled(true);

    prof.beginScope("WillBeCleared");
    prof.endScope("WillBeCleared");
    REQUIRE(prof.scopeCount() > 0);

    prof.reset();
    REQUIRE(prof.scopeCount() == 0);

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);
    REQUIRE(stats.size() == 0);
}

// ============================================================================
// ProfileScope — RAII
// ============================================================================

TEST_CASE("ProfileScope - RAII records scope", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    {
        ProfileScope scope("RAIIScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Vector<Profiler::ScopeStats> stats;
    prof.report(stats);

    REQUIRE(stats.size() == 1);
    REQUIRE(std::string(stats[0].name) == "RAIIScope");
    REQUIRE(stats[0].callCount == 1);
    REQUIRE(stats[0].totalMs > 0.0);
}

// ============================================================================
// Profiler — scopeCount
// ============================================================================

TEST_CASE("Profiler - scopeCount tracks unique scopes", "[debug][profiler]") {
    auto& prof = Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    REQUIRE(prof.scopeCount() == 0);

    prof.beginScope("One");
    prof.endScope("One");
    REQUIRE(prof.scopeCount() == 1);

    prof.beginScope("Two");
    prof.endScope("Two");
    REQUIRE(prof.scopeCount() == 2);

    prof.beginScope("One");
    prof.endScope("One");
    REQUIRE(prof.scopeCount() == 2);
}
