#pragma once

#include "ecs/World.hpp"

namespace Caffeine::Scene {

/// Applies SpriteLod2DComponent using active play camera zoom (PlayRenderContext).
void applySpriteLod2D(ECS::World& world, f32 cameraZoom);

}  // namespace Caffeine::Scene
