#pragma once

#include "ecs/World.hpp"
#include "events/EventBus.hpp"
#include "render/Camera2D.hpp"

namespace Caffeine::Scene {

/// Emits chunk request events for MapSurface2DComponent (no provider logic in engine).
void tickMapSurfaceChunkRequests(ECS::World& world, Events::EventBus& eventBus,
                                 const Render::Camera2D* camera, bool cameraValid);

void resetMapSurfaceChunkRequestCache();

}  // namespace Caffeine::Scene
