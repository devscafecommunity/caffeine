#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"

namespace Caffeine::ECS {

/// Optional 2D LOD driven by active camera zoom (see SpriteLod2DSystem).
struct SpriteLod2DComponent {
    Vec2 baseScale{1.0f, 1.0f};
    /// Camera zoom at or above this keeps full baseScale.
    f32 zoomFullDetail = 1.0f;
    /// At or below this zoom, scale is multiplied by minimalScaleFactor.
    f32 zoomMinimal = 0.25f;
    f32 minimalScaleFactor = 0.35f;
    bool hideWhenMinimal = false;
};

}  // namespace Caffeine::ECS
