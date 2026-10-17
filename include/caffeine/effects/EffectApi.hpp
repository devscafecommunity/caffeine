#pragma once

#include "effects/EffectTypes.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <algorithm>

namespace Caffeine::Effects {

inline bool setEffect(ECS::World& world, ECS::Entity entity, EffectKind kind, EffectQuality quality) {
    if (!entity.isValid()) return false;
    EffectComponent effect;
    if (EffectComponent* existing = world.get<EffectComponent>(entity)) effect = *existing;
    effect.enabled = 1;
    effect.kind = static_cast<u8>(kind);
    effect.quality = static_cast<u8>(quality);
    if (kind == EffectKind::ReflectiveSurface && effect.reflection <= 0.0f) effect.reflection = 0.85f;
    if (kind == EffectKind::VolumetricLight) effect.domain = static_cast<u8>(EffectDomain::ThreeD);
    if (kind == EffectKind::Fog && effect.radius <= 0.0f) {
        configureFog(effect, static_cast<EffectDomain>(effect.domain), EffectFogStyle::Fog);
    }
    if (world.has<EffectComponent>(entity)) *world.get<EffectComponent>(entity) = effect;
    else world.add<EffectComponent>(entity, effect);
    return true;
}

inline bool setParticleEmitter(ECS::World& world, ECS::Entity entity, EffectDomain domain) {
    if (!entity.isValid()) return false;
    EffectComponent effect;
    const bool fresh = world.get<EffectComponent>(entity) == nullptr ||
                       world.get<EffectComponent>(entity)->kind != static_cast<u8>(EffectKind::Particles);
    if (EffectComponent* existing = world.get<EffectComponent>(entity)) effect = *existing;
    if (fresh) configureParticle(effect, domain);
    else {
        effect.enabled = 1;
        effect.kind = static_cast<u8>(EffectKind::Particles);
        effect.domain = static_cast<u8>(domain);
    }
    if (world.has<EffectComponent>(entity)) *world.get<EffectComponent>(entity) = effect;
    else world.add<EffectComponent>(entity, effect);
    return true;
}

inline bool setParticleParams(ECS::World& world, ECS::Entity entity, f32 rate, f32 lifetime,
                              i32 maxParticles, f32 startSize, f32 endSize, f32 gravityY) {
    if (!entity.isValid()) return false;
    if (!world.get<EffectComponent>(entity)) {
        if (!setParticleEmitter(world, entity, EffectDomain::ThreeD)) return false;
    }
    EffectComponent* effect = world.get<EffectComponent>(entity);
    if (!effect) return false;
    if (effect->kind != static_cast<u8>(EffectKind::Particles)) {
        configureParticle(*effect, static_cast<EffectDomain>(effect->domain));
    }
    effect->rate = std::max(rate, 0.0f);
    effect->lifetime = std::max(lifetime, 0.05f);
    effect->maxParticles = std::max(maxParticles, 0);
    effect->startSize = std::max(startSize, 0.0f);
    effect->endSize = std::max(endSize, 0.0f);
    effect->gravity.y = gravityY;
    return true;
}

inline bool setVolumetricLight(ECS::World& world, ECS::Entity entity) {
    if (!entity.isValid()) return false;
    EffectComponent effect;
    if (EffectComponent* existing = world.get<EffectComponent>(entity)) effect = *existing;
    if (effect.kind != static_cast<u8>(EffectKind::VolumetricLight)) configureVolumetricLight(effect);
    else {
        effect.enabled = 1;
        effect.kind = static_cast<u8>(EffectKind::VolumetricLight);
        effect.domain = static_cast<u8>(EffectDomain::ThreeD);
    }
    if (world.has<EffectComponent>(entity)) *world.get<EffectComponent>(entity) = effect;
    else world.add<EffectComponent>(entity, effect);
    return true;
}

inline bool setVolumetricShape(ECS::World& world, ECS::Entity entity, VolumetricShape shape,
                               u32 columns = 0, u32 rows = 0) {
    if (!entity.isValid()) return false;
    EffectComponent effect;
    if (EffectComponent* existing = world.get<EffectComponent>(entity)) effect = *existing;
    const Vec3 keptColor = effect.lightColor;
    const bool keepColor = effect.kind == static_cast<u8>(EffectKind::VolumetricLight);
    configureVolumetricLight(effect, shape);
    if (keepColor) effect.lightColor = keptColor;
    if (shape == VolumetricShape::Window && columns > 0 && rows > 0) setVolumetricGrid(effect, columns, rows);
    if (world.has<EffectComponent>(entity)) *world.get<EffectComponent>(entity) = effect;
    else world.add<EffectComponent>(entity, effect);
    return true;
}

inline bool setFog(ECS::World& world, ECS::Entity entity, EffectDomain domain, EffectFogStyle style) {
    if (!entity.isValid()) return false;
    EffectComponent effect;
    if (EffectComponent* existing = world.get<EffectComponent>(entity)) effect = *existing;
    configureFog(effect, domain, style);
    if (world.has<EffectComponent>(entity)) *world.get<EffectComponent>(entity) = effect;
    else world.add<EffectComponent>(entity, effect);
    return true;
}

}  // namespace Caffeine::Effects
