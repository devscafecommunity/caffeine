#pragma once

#include "core/Types.hpp"

namespace Caffeine::Runtime {

/// Simulation clock for play mode (time scale, pause, fixed step).
class PlayClock {
public:
    static PlayClock& instance();

    void reset();
    void advance(f32 variableDt);

    f32 deltaTime() const { return m_scaledDt; }
    f32 unscaledDeltaTime() const { return m_rawDt; }
    f32 time() const { return m_time; }
    f32 timeScale() const { return m_timeScale; }
    void setTimeScale(f32 scale);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);

    f32 fixedDeltaTime() const { return m_fixedDt; }
    void setFixedDeltaTime(f32 dt);
    int consumeFixedSteps();

    u64 frame() const { return m_frame; }
    u32 randomSeed() const { return m_seed; }
    void setRandomSeed(u32 seed);

private:
    f32 m_rawDt = 0.0f;
    f32 m_scaledDt = 0.0f;
    f32 m_time = 0.0f;
    f32 m_timeScale = 1.0f;
    f32 m_fixedDt = 1.0f / 60.0f;
    f32 m_fixedAccumulator = 0.0f;
    int m_fixedStepsPending = 0;
    bool m_paused = false;
    u64 m_frame = 0;
    u32 m_seed = 1;
};

}  // namespace Caffeine::Runtime
