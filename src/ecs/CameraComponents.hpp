#pragma once
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
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
    f32 farClip = 8000.0f;
    f32 aspectRatio = 16.0f / 9.0f;
};

struct CameraActiveComponent {
    bool is2D = true;
};

enum class Camera3DControlMode : u8 {
    None   = 0,
    Orbit  = 1,
    FPS    = 2,
    Follow = 3,
};

/// Drives Position3D / Rotation3D for entities with Camera3DComponent.
struct Camera3DControllerComponent {
    Camera3DControlMode mode = Camera3DControlMode::None;
    Vec3 orbitTarget = {0.0f, 0.0f, 0.0f};
    f32  orbitDistance = 10.0f;
    f32  azimuth = 0.0f;
    f32  elevation = 30.0f;
    Entity followTarget;
    Vec3 followOffset = {0.0f, 2.0f, -5.0f};
    f32  followSmoothing = 0.1f;
    f32  mouseSensitivity = 0.15f;
    f32  moveSpeed = 5.0f;
};

}  // namespace Caffeine::ECS
