#include "ecs/PostProcessComponents.hpp"
#include "ecs/World.hpp"

#include <sol/sol.hpp>

#include <cstring>

namespace Caffeine::Script {
namespace {

ECS::PostProcessComponent* postProcessForEntity(ECS::World* world, u32 entityId) {
    if (!world) return nullptr;
    ECS::Entity entity(entityId, world);
    if (!entity.isValid()) return nullptr;
    return entity.get<ECS::PostProcessComponent>();
}

void registerPostProcessBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["postprocess"] = lua.create_table();
    sol::table pp = lua["caffeine"]["postprocess"];

    pp["get"] = [&lua, worldPtr](u32 entityId) -> sol::table {
        sol::table t = lua.create_table();
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        auto* fx = postProcessForEntity(world, entityId);
        if (!fx) return t;
        t["enabled"] = fx->enabled;
        t["exposure"] = fx->colorGrading.exposure;
        t["bloom"] = fx->bloom.intensity;
        t["vignette"] = fx->vignette.intensity;
        return t;
    };

    pp["setEnabled"] = [worldPtr](u32 entityId, bool enabled) {
        if (auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->enabled = enabled;
        }
    };

    pp["setExposure"] = [worldPtr](u32 entityId, float exposure) {
        if (auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->colorGrading.exposure = exposure;
            fx->colorGrading.enabled = true;
        }
    };

    pp["setBloom"] = [worldPtr](u32 entityId, float intensity) {
        if (auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->bloom.intensity = intensity;
            fx->bloom.enabled = intensity > 0.001f;
        }
    };

    pp["setVignette"] = [worldPtr](u32 entityId, float intensity) {
        if (auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            fx->vignette.intensity = intensity;
            fx->vignette.enabled = intensity > 0.001f;
        }
    };

    pp["enableEffect"] = [worldPtr](u32 entityId, const std::string& effectName, bool enabled) {
        auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId);
        if (!fx) return;
        if (effectName == "ambientOcclusion" || effectName == "ao") {
            fx->ambientOcclusion.enabled = enabled;
        } else if (effectName == "antiAliasing" || effectName == "aa") {
            fx->antiAliasing.enabled = enabled;
        } else if (effectName == "autoExposure") {
            fx->autoExposure.enabled = enabled;
        } else if (effectName == "bloom") {
            fx->bloom.enabled = enabled;
        } else if (effectName == "chromaticAberration" || effectName == "chromatic") {
            fx->chromaticAberration.enabled = enabled;
        } else if (effectName == "colorGrading") {
            fx->colorGrading.enabled = enabled;
        } else if (effectName == "deferredFog" || effectName == "fog") {
            fx->deferredFog.enabled = enabled;
        } else if (effectName == "depthOfField" || effectName == "dof") {
            fx->depthOfField.enabled = enabled;
        } else if (effectName == "grain") {
            fx->grain.enabled = enabled;
        } else if (effectName == "lensDistortion") {
            fx->lensDistortion.enabled = enabled;
        } else if (effectName == "motionBlur") {
            fx->motionBlur.enabled = enabled;
        } else if (effectName == "screenSpaceReflections" || effectName == "ssr") {
            fx->screenSpaceReflections.enabled = enabled;
        } else if (effectName == "vignette") {
            fx->vignette.enabled = enabled;
        }
    };

    pp["setCustomScript"] = [worldPtr](u32 entityId, const std::string& path) {
        if (auto* fx = postProcessForEntity(worldPtr ? *worldPtr : nullptr, entityId)) {
            std::strncpy(fx->customEffectScript, path.c_str(), sizeof(fx->customEffectScript) - 1);
            fx->customEffectScript[sizeof(fx->customEffectScript) - 1] = '\0';
        }
    };
}

}  // namespace

void registerPostProcessScriptBindings(sol::state& lua, ECS::World** worldPtr);

void registerPostProcessScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    registerPostProcessBindings(lua, worldPtr);
}

}  // namespace Caffeine::Script
