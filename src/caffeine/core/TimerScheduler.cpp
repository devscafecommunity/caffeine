#include "TimerScheduler.hpp"

#include <algorithm>

namespace Caffeine {

TimerScheduler& TimerScheduler::instance() {
    static TimerScheduler scheduler;
    return scheduler;
}

TimerHandle TimerScheduler::scheduleOnce(f64 delaySeconds, std::function<void()> callback) {
    if (!callback) return 0;
    TimerEntry entry;
    entry.handle = m_nextHandle++;
    entry.remaining = std::max(delaySeconds, 0.0);
    entry.repeating = false;
    entry.callback = std::move(callback);
    m_timers.push_back(std::move(entry));
    return entry.handle;
}

TimerHandle TimerScheduler::scheduleRepeating(f64 intervalSeconds, std::function<void()> callback) {
    if (!callback) return 0;
    TimerEntry entry;
    entry.handle = m_nextHandle++;
    entry.remaining = std::max(intervalSeconds, 0.0);
    entry.interval = std::max(intervalSeconds, 0.0);
    entry.repeating = true;
    entry.callback = std::move(callback);
    m_timers.push_back(std::move(entry));
    return entry.handle;
}

void TimerScheduler::cancel(TimerHandle handle) {
    if (handle == 0) return;
    for (auto& timer : m_timers) {
        if (timer.handle == handle) {
            timer.active = false;
            return;
        }
    }
}

void TimerScheduler::cancelAll() {
    m_timers.clear();
}

void TimerScheduler::tick(f64 fixedDeltaTime) {
    if (m_timers.empty()) return;

    for (auto& timer : m_timers) {
        if (!timer.active || !timer.callback) continue;
        timer.remaining -= fixedDeltaTime;
        if (timer.remaining > 0.0) continue;

        timer.callback();

        if (timer.repeating) {
            timer.remaining += timer.interval;
            if (timer.remaining <= 0.0) {
                timer.remaining = timer.interval;
            }
        } else {
            timer.active = false;
        }
    }

    m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(),
                                  [](const TimerEntry& t) { return !t.active; }),
                   m_timers.end());
}

}  // namespace Caffeine
