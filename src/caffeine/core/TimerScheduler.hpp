#pragma once

#include "Types.hpp"
#include <functional>
#include <vector>

namespace Caffeine {

using TimerHandle = u64;

/// Engine timer scheduler — scheduleOnce / scheduleRepeating with fixed-timestep integration.
class TimerScheduler {
public:
    static TimerScheduler& instance();

    /// Fire once after delay seconds (wall time, decremented each tick).
    TimerHandle scheduleOnce(f64 delaySeconds, std::function<void()> callback);

    /// Fire every interval seconds. First fire after one interval.
    TimerHandle scheduleRepeating(f64 intervalSeconds, std::function<void()> callback);

    void cancel(TimerHandle handle);
    void cancelAll();

    /// Call from GameLoop fixed update with fixedDeltaTime.
    void tick(f64 fixedDeltaTime);

    usize activeCount() const { return m_timers.size(); }

private:
    struct TimerEntry {
        TimerHandle           handle   = 0;
        f64                   remaining = 0.0;
        f64                   interval  = 0.0;
        bool                  repeating = false;
        bool                  active    = true;
        std::function<void()> callback;
    };

    TimerHandle m_nextHandle = 1;
    std::vector<TimerEntry> m_timers;
};

}  // namespace Caffeine
