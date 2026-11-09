#include "caffeine/effects/EffectApi.hpp"
#include "effects/EffectSystem.hpp"

#include <algorithm>
#include <sol/sol.hpp>

namespace Caffeine::Script {

void registerEffectScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["effects"] = lua.create_table();
    sol::table effects = lua["caffeine"]["effects"];
    effects["set"] = [worldPtr](u32 entityId, u32 kind, u32 quality) {
        if (!worldPtr || !*worldPtr) return false;
        const auto effectKind = static_cast<Effects::EffectKind>(std::min(kind, 4u));
        const auto effectQuality = static_cast<Effects::EffectQuality>(std::min(quality, 2u));
        return Effects::setEffect(**worldPtr, ECS::Entity(entityId, *worldPtr), effectKind, effectQuality);
    };
    effects["particle"] = [worldPtr](u32 entityId, u32 domain) {
        if (!worldPtr || !*worldPtr) return false;
        const auto effectDomain = static_cast<Effects::EffectDomain>(std::min(domain, 2u));
        return Effects::setParticleEmitter(**worldPtr, ECS::Entity(entityId, *worldPtr), effectDomain);
    };
    effects["particleParams"] = [worldPtr](u32 entityId, f32 rate, f32 lifetime, i32 maxParticles,
                                           f32 startSize, f32 endSize, f32 gravityY) {
        if (!worldPtr || !*worldPtr) return false;
        return Effects::setParticleParams(**worldPtr, ECS::Entity(entityId, *worldPtr), rate, lifetime,
                                           maxParticles, startSize, endSize, gravityY);
    };
    effects["volumetric"] = [worldPtr](u32 entityId) {
        if (!worldPtr || !*worldPtr) return false;
        ECS::World& world = **worldPtr;
        const ECS::Entity entity(entityId, &world);
        if (!Effects::setVolumetricLight(world, entity)) return false;
        if (Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(entity)) {
            Effects::ensureVolumetricMesh(world, entity, *effect);
        }
        return true;
    };
    effects["volumetricShape"] = [worldPtr](u32 entityId, u32 shape, u32 columns, u32 rows) {
        if (!worldPtr || !*worldPtr) return false;
        ECS::World& world = **worldPtr;
        const ECS::Entity entity(entityId, &world);
        const auto volumeShape = static_cast<Effects::VolumetricShape>(std::min(shape, 4u));
        if (!Effects::setVolumetricShape(world, entity, volumeShape, columns, rows)) return false;
        if (Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(entity)) {
            Effects::adoptVolumetricMesh(world, entity, *effect);
        }
        return true;
    };
    effects["fog"] = [worldPtr](u32 entityId, u32 domain, u32 style) {
        if (!worldPtr || !*worldPtr) return false;
        const auto effectDomain = static_cast<Effects::EffectDomain>(std::min(domain, 2u));
        const auto fogStyle = static_cast<Effects::EffectFogStyle>(std::min(style, 3u));
        return Effects::setFog(**worldPtr, ECS::Entity(entityId, *worldPtr), effectDomain, fogStyle);
    };
}

}  // namespace Caffeine::Script
