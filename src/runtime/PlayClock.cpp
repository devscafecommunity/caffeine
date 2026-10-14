#include "runtime/PlayClock.hpp"

#include <algorithm>
#include <cstdlib>

namespace Caffeine::Runtime {

PlayClock& PlayClock::instance() {
    static PlayClock s;
    return s;
}

void PlayClock::reset() {
    m_rawDt = 0.0f;
    m_scaledDt = 0.0f;
    m_time = 0.0f;
    m_fixedAccumulator = 0.0f;
    m_fixedStepsPending = 0;
    m_frame = 0;
}

void PlayClock::advance(f32 variableDt) {
    m_rawDt = std::max(0.0f, variableDt);
    m_scaledDt = m_paused ? 0.0f : m_rawDt * m_timeScale;
    m_time += m_scaledDt;
    ++m_frame;

    m_fixedStepsPending = 0;
    if (!m_paused && m_timeScale > 0.0f) {
        m_fixedAccumulator += m_scaledDt;
        const f32 maxSteps = m_fixedDt > 0.0f ? 5.0f : 0.0f;
        int steps = 0;
        while (m_fixedAccumulator >= m_fixedDt && steps < static_cast<int>(maxSteps)) {
            m_fixedAccumulator -= m_fixedDt;
            ++m_fixedStepsPending;
            ++steps;
        }
    }
}

void PlayClock::setTimeScale(f32 scale) {
    m_timeScale = std::max(0.0f, scale);
}

void PlayClock::setPaused(bool paused) {
    m_paused = paused;
}

void PlayClock::setFixedDeltaTime(f32 dt) {
    m_fixedDt = std::max(1.0f / 240.0f, dt);
}

int PlayClock::consumeFixedSteps() {
    const int n = m_fixedStepsPending;
    m_fixedStepsPending = 0;
    return n;
}

void PlayClock::setRandomSeed(u32 seed) {
    m_seed = seed;
    std::srand(seed);
}

}  // namespace Caffeine::Runtime
