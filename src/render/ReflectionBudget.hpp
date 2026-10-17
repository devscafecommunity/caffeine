#pragma once

#include "core/Types.hpp"

#include <algorithm>

namespace Caffeine::Render {

/// Optional reflection cost. Off leaves the scene SSR settings untouched.
struct ReflectionModules {
    bool performance = false;
    bool quality = false;
    bool planar = false;
    f32 resolution = 0.5f;
    f32 maxSteps = 32.0f;
    f32 temporalFrames = 4.0f;
    f32 distanceFade = 15.0f;
    f32 samples = 64.0f;
    f32 denoise = 0.7f;
    f32 probeBlend = 8.0f;
    f32 bounces = 1.0f;
};

/// Matches the step count in scene_lit.frag. Performance shortens the march; quality can raise it.
inline i32 reflectionMarchSteps(f32 sceneSteps, f32 roughness, const ReflectionModules& modules) {
    f32 steps = sceneSteps;
    if (modules.performance) {
        const f32 scale = std::clamp(modules.resolution, 0.25f, 1.0f);
        const f32 cap = std::clamp(modules.maxSteps, 4.0f, 64.0f);
        const f32 roughScale = 1.0f + (0.35f - 1.0f) * std::clamp(roughness, 0.0f, 1.0f);
        steps = std::min(steps, cap) * scale * roughScale;
    }
    if (modules.quality) {
        steps = std::max(steps, std::clamp(modules.samples, 4.0f, 64.0f));
    }
    return static_cast<i32>(std::clamp(steps, 4.0f, 64.0f));
}

}  // namespace Caffeine::Render
