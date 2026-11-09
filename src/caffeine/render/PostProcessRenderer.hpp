#pragma once

#include "ecs/PostProcessComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"

namespace Caffeine::Render {

/// Post-process settings for a camera: its own component, otherwise the first one in the scene.
inline const ECS::PostProcessComponent* findPostProcessForCamera(ECS::World& world,
                                                               ECS::Entity cameraEntity) {
    if (cameraEntity.isValid() && world.isEntityAlive(cameraEntity)) {
        if (auto* fx = world.get<ECS::PostProcessComponent>(cameraEntity)) return fx;
    }
    ECS::ComponentQuery q;
    q.with<ECS::PostProcessComponent>();
    const ECS::PostProcessComponent* fallback = nullptr;
    world.forEach<ECS::PostProcessComponent>(q, [&](ECS::Entity, ECS::PostProcessComponent& fx) {
        if (!fallback) fallback = &fx;
    });
    return fallback;
}

}  // namespace Caffeine::Render
