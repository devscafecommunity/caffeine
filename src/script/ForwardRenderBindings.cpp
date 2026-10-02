#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/World.hpp"
#include "render/RenderFeatures.hpp"

#include <sol/sol.hpp>

#include <algorithm>

namespace Caffeine::Script {
namespace {

ECS::ForwardRenderFeaturesComponent* forwardFeaturesForEntity(ECS::World* world, u32 entityId) {
    if (!world) return nullptr;
    ECS::Entity entity(entityId, world);
    if (!entity.isValid()) return nullptr;
    return entity.get<ECS::ForwardRenderFeaturesComponent>();
}

void registerForwardRenderBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["forwardrender"] = lua.create_table();
    sol::table api = lua["caffeine"]["forwardrender"];

    api["ensure"] = [worldPtr](u32 entityId) -> bool {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return false;
        ECS::Entity entity(entityId, world);
        if (!entity.isValid()) return false;
        if (!entity.has<ECS::ForwardRenderFeaturesComponent>()) {
            world->add<ECS::ForwardRenderFeaturesComponent>(entity);
        }
        return true;
    };

    api["setInstancing"] = [worldPtr](u32 entityId, bool enabled, sol::optional<u32> maxBatch) {
        if (auto* fx = forwardFeaturesForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->instancing.enabled = enabled;
            if (maxBatch) fx->instancing.maxInstancesPerBatch = *maxBatch;
        }
    };

    api["setIbl"] = [worldPtr](u32 entityId, bool enabled, sol::optional<float> diffuse,
                                sol::optional<float> specular) {
        if (auto* fx = forwardFeaturesForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->ibl.enabled = enabled;
            if (diffuse) fx->ibl.diffuse = *diffuse;
            if (specular) fx->ibl.specular = *specular;
        }
    };

    api["setOcclusion"] = [worldPtr](u32 entityId, bool enabled) {
        if (auto* fx = forwardFeaturesForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->occlusion.enabled = enabled;
        }
    };

    api["setReflections"] = [worldPtr](u32 entityId, bool enabled, sol::optional<int> mode) {
        if (auto* fx = forwardFeaturesForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->reflections.enabled = enabled;
            if (mode) {
                fx->reflections.mode =
                    static_cast<Render::ReflectionMode>(std::clamp(*mode, 0, 3));
            }
        }
    };

    api["setVolumetrics"] = [worldPtr](u32 entityId, bool enabled, sol::optional<int> quality) {
        if (auto* fx = forwardFeaturesForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->volumetrics.enabled = enabled;
            if (quality) {
                fx->volumetrics.quality =
                    static_cast<Render::VolumetricQuality>(std::clamp(*quality, 0, 2));
            }
        }
    };
}

}  // namespace

void registerForwardRenderScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    registerForwardRenderBindings(lua, worldPtr);
}

}  // namespace Caffeine::Script
