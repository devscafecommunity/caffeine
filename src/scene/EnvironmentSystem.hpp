#pragma once

#include "ecs/SkyboxComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/HierarchySystem.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Scene {

struct ActiveSkybox {
    ECS::Entity entity = ECS::Entity::INVALID;
    const ECS::SkyboxComponent* component = nullptr;
};

inline ActiveSkybox findActiveSkybox(ECS::World& world) {
    ActiveSkybox result;
    ECS::ComponentQuery q;
    q.with<ECS::SkyboxComponent>();
    world.forEach<ECS::SkyboxComponent>(q, [&](ECS::Entity e, ECS::SkyboxComponent& sky) {
        if (isEffectivelyDisabled(world, e)) return;
        if (!sky.enabled) return;
        if (!result.entity.isValid()) {
            result.entity = e;
        }
    });
    if (result.entity.isValid()) {
        result.component = world.get<ECS::SkyboxComponent>(result.entity);
    }
    return result;
}

inline bool hasSceneSkybox(ECS::World& world) {
    ECS::ComponentQuery q;
    q.with<ECS::SkyboxComponent>();
    bool found = false;
    world.forEach<ECS::SkyboxComponent>(q, [&](ECS::Entity e, ECS::SkyboxComponent&) {
        if (!isEffectivelyDisabled(world, e)) found = true;
    });
    return found;
}

std::filesystem::path findEngineAssetsRoot();
std::filesystem::path resolveBuiltinSkyboxPath(int presetIndex);
std::filesystem::path resolveSkyboxTexturePath(const ECS::SkyboxComponent& sky,
                                               const std::string& projectRoot = "");

}  // namespace Caffeine::Scene
