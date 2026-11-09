#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"

namespace Caffeine::Scene {

/// Per-frame input for Camera3DControllerComponent (editor or runtime).
struct Camera3DInput {
    f32 mouseDeltaX = 0.0f;
    f32 mouseDeltaY = 0.0f;
    f32 moveX       = 0.0f;
    f32 moveY       = 0.0f;
    f32 moveZ       = 0.0f;
    f32 zoomDelta   = 0.0f;
};

/// Updates entities with Camera3DControllerComponent + Position3D + Rotation3D.
/// Active camera (CameraActiveComponent, is2D=false) is updated when present.
void updateCamera3DControllers(ECS::World& world, f64 dt, const Camera3DInput& input = {});

}  // namespace Caffeine::Scene
