#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::ECS {
using namespace Caffeine;

struct Camera2DComponent {
    f32 zoom = 1.0f;
    f32 nearClip = 0.1f;
    f32 farClip = 1000.0f;
};

struct Camera3DComponent {
    f32 fov = 60.0f;
    f32 nearClip = 0.1f;
    f32 farClip = 1000.0f;
    f32 aspectRatio = 16.0f / 9.0f;
};

struct CameraActiveComponent {
    bool is2D = true;
};

}  // namespace Caffeine::ECS
