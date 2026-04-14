#include "Timer.hpp"
#include <chrono>

namespace Caffeine::Core {

static TimePoint getCurrentTimePoint() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = now.time_since_epoch();
    u64 nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return TimePoint(nanos);
}

Timer::Timer()
    : m_is_running(false), m_accumulated_ticks(0) {
}

Timer::~Timer() {
}

void Timer::start() {
    if (!m_is_running) {
        m_start_time = getCurrentTimePoint();
        m_is_running = true;
    }
}

void Timer::stop() {
    if (m_is_running) {
        m_stop_time = getCurrentTimePoint();
        u64 ticks = m_stop_time.ticks - m_start_time.ticks;
        m_accumulated_ticks += ticks;
        m_is_running = false;
    }
}

void Timer::reset() {
    m_accumulated_ticks = 0;
    m_start_time.ticks = 0;
    m_stop_time.ticks = 0;
    m_is_running = false;
}

Duration Timer::elapsed() const {
    if (m_is_running) {
        TimePoint current = getCurrentTimePoint();
        u64 ticks = current.ticks - m_start_time.ticks + m_accumulated_ticks;
        return Duration::fromNanos(static_cast<double>(ticks));
    } else {
        return Duration::fromNanos(static_cast<double>(m_accumulated_ticks));
    }
}

Duration Timer::tick() {
    if (m_is_running) {
        TimePoint current = getCurrentTimePoint();
        u64 ticks = current.ticks - m_start_time.ticks;
        m_start_time = current;
        return Duration::fromNanos(static_cast<double>(ticks));
    }
    return Duration::fromSeconds(0.0);
}

bool Timer::isRunning() const {
    return m_is_running;
}

ScopeTimer::ScopeTimer(const char* name)
    : m_name(name) {
    m_timer.start();
}

ScopeTimer::~ScopeTimer() {
    m_timer.stop();
}

}
