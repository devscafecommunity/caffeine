#pragma once

#include "core/Types.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Animation {

using namespace Caffeine;

/// Critically damped spring towards `target` (same model as Unity's SmoothDamp).
/// `velocity` is state the caller keeps between frames. `smoothTime` is roughly the time to reach the target.
/// Stable for any dt and never overshoots the target.
inline f32 smoothDamp(f32 current, f32 target, f32& velocity, f32 smoothTime, f32 dt) {
    if (dt <= 0.0f) return current;
    const f32 st       = std::max(smoothTime, 0.0001f);
    const f32 omega    = 2.0f / st;
    const f32 x        = omega * dt;
    const f32 expFactor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const f32 change   = current - target;
    const f32 temp     = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * expFactor;
    f32 output = target + (change + temp) * expFactor;

    // Stop at the target instead of passing it.
    if ((target - current > 0.0f) == (output > target)) {
        output   = target;
        velocity = 0.0f;
    }
    return output;
}

/// Frame-rate independent exponential approach. `rate` is in 1/seconds: higher reaches the target sooner.
inline f32 exponentialApproach(f32 current, f32 target, f32 rate, f32 dt) {
    if (dt <= 0.0f || rate <= 0.0f) return current;
    const f32 k = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * k;
}

}  // namespace Caffeine::Animation
