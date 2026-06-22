#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::ECS {
using namespace Caffeine;

struct LightComponent {
    Vec4 color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 intensity = 1.0f;
};

struct DirectionalLightComponent {
    f32 shadowDistance = 100.0f;
    bool castShadows = true;
};

struct PointLightComponent {
    f32 radius = 10.0f;
    bool castShadows = false;
};

struct SpotLightComponent {
    f32 radius = 10.0f;
    f32 angle = 45.0f;
    bool castShadows = false;
};

}  // namespace Caffeine::ECS
