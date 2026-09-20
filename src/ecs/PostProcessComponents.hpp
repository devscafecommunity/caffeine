#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

struct PostProcessComponent {
    bool enabled = true;

    f32 exposure = 1.0f;
    f32 contrast = 1.0f;
    f32 saturation = 1.0f;
    f32 vignette = 0.0f;
    f32 bloom = 0.0f;
    f32 chromaticAberration = 0.0f;
    f32 filmGrain = 0.0f;
};

}  // namespace Caffeine::ECS
