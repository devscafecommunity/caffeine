#pragma once

#include "../core/Types.hpp"

namespace Caffeine::Core {

struct TimePoint {
    u64 ticks = 0;

    TimePoint() = default;
    explicit TimePoint(u64 t) : ticks(t) {}

    bool operator<(const TimePoint& other) const {
        return ticks < other.ticks;
    }

    bool operator<=(const TimePoint& other) const {
        return ticks <= other.ticks;
    }

    bool operator>(const TimePoint& other) const {
        return ticks > other.ticks;
    }

    bool operator>=(const TimePoint& other) const {
        return ticks >= other.ticks;
    }

    bool operator==(const TimePoint& other) const {
        return ticks == other.ticks;
    }

    bool operator!=(const TimePoint& other) const {
        return ticks != other.ticks;
    }
};

struct Duration {
    f64 seconds = 0.0;

    Duration() = default;
    explicit Duration(f64 s) : seconds(s) {}

    static Duration fromSeconds(f64 s) {
        return Duration(s);
    }

    static Duration fromMillis(f64 ms) {
        return Duration(ms / 1000.0);
    }

    static Duration fromMicros(f64 us) {
        return Duration(us / 1000000.0);
    }

    static Duration fromNanos(f64 ns) {
        return Duration(ns / 1000000000.0);
    }

    f64 millis() const {
        return seconds * 1000.0;
    }

    f64 micros() const {
        return seconds * 1000000.0;
    }

    f64 nanos() const {
        return seconds * 1000000000.0;
    }

    Duration operator+(const Duration& other) const {
        return Duration(seconds + other.seconds);
    }

    Duration operator-(const Duration& other) const {
        return Duration(seconds - other.seconds);
    }

    Duration operator*(f64 scalar) const {
        return Duration(seconds * scalar);
    }

    friend Duration operator*(f64 scalar, const Duration& d) {
        return Duration(scalar * d.seconds);
    }

    Duration operator/(f64 scalar) const {
        return Duration(seconds / scalar);
    }

    bool operator<(const Duration& other) const {
        return seconds < other.seconds;
    }

    bool operator<=(const Duration& other) const {
        return seconds <= other.seconds;
    }

    bool operator>(const Duration& other) const {
        return seconds > other.seconds;
    }

    bool operator>=(const Duration& other) const {
        return seconds >= other.seconds;
    }

    bool operator==(const Duration& other) const {
        const f64 epsilon = 1e-15;
        return (seconds - other.seconds) < epsilon && (seconds - other.seconds) > -epsilon;
    }

    bool operator!=(const Duration& other) const {
        return !(*this == other);
    }
};

inline Duration operator-(const TimePoint& a, const TimePoint& b) {
    u64 tick_diff = (a.ticks > b.ticks) ? (a.ticks - b.ticks) : (b.ticks - a.ticks);
    return Duration::fromSeconds(static_cast<f64>(tick_diff) / 1000000.0);
}

class Timer {
public:
    Timer();
    ~Timer();

    void start();
    void stop();
    void reset();

    Duration elapsed() const;
    Duration tick();

    bool isRunning() const;

private:
    TimePoint m_start_time;
    TimePoint m_stop_time;
    u64 m_accumulated_ticks;
    bool m_is_running;
};

class ScopeTimer {
public:
    explicit ScopeTimer(const char* name);
    ~ScopeTimer();

private:
    const char* m_name;
    Timer m_timer;
};

}
