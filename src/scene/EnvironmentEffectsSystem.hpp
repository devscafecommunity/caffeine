#pragma once

#include "ecs/EnvironmentEffectsComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/SkyboxComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/World.hpp"
#include "math/Quat.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/LightingSystem.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Caffeine::Scene {

inline Vec3 environmentEntityPosition(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<WorldTransform>(entity)) {
        return Vec3(wt->matrix(0, 3), wt->matrix(1, 3), wt->matrix(2, 3));
    }
    if (auto* p = world.get<ECS::Position3D>(entity)) return p->position;
    if (auto* t = world.get<ECS::Transform>(entity)) return t->position;
    return {};
}

inline Vec3 worldLightSunDirection(f32 timeOfDay) {
    const f32 hour = std::clamp(timeOfDay, 0.0f, 24.0f);
    const f32 tau = (hour / 24.0f) * 6.28318530718f;
    const f32 elevation = std::sin((hour - 6.0f) / 12.0f * 3.14159265359f);
    if (elevation > 0.0f) {
        return Vec3(std::cos(tau), -std::max(0.12f, elevation), std::sin(tau)).normalized();
    }
    return Vec3(-std::cos(tau), -0.22f, -std::sin(tau)).normalized();
}

inline f32 worldLightDayFactor(f32 timeOfDay) {
    const f32 elevation = std::sin((std::clamp(timeOfDay, 0.0f, 24.0f) - 6.0f) / 12.0f * 3.14159265359f);
    return std::clamp(elevation, 0.0f, 1.0f);
}

inline f32 sampleSkyVisibility(ECS::World& world, const Vec3& origin);

inline Vec3 firstCamera3DPosition(ECS::World& world, const Vec3& fallback) {
    Vec3 pos = fallback;
    ECS::ComponentQuery query;
    query.with<ECS::Camera3DComponent>();
    world.forEach<ECS::Camera3DComponent>(query, [&](ECS::Entity entity, ECS::Camera3DComponent&) {
        if (isEffectivelyDisabled(world, entity)) return;
        if (auto* active = world.get<ECS::CameraActiveComponent>(entity)) {
            if (active->is2D) return;
        }
        pos = environmentEntityPosition(world, entity);
    });
    return pos;
}

inline f32 environmentZoneWeight(ECS::World& world, ECS::Entity entity,
                                 const ECS::EnvironmentEffectsComponent& fx, const Vec3& viewPos) {
    if (fx.mode != static_cast<u8>(ECS::WorldLightMode::Zone)) return 0.0f;
    const Vec3 origin = environmentEntityPosition(world, entity);
    const f32 radius = std::max(fx.zoneRadius, 0.01f);
    const f32 dist = (viewPos - origin).length();
    const f32 t = std::clamp(dist / radius, 0.0f, 1.0f);
    return 1.0f - std::pow(t, std::max(fx.zoneFalloff, 0.05f));
}

inline void evaluateWorldLightFill(const ECS::EnvironmentEffectsComponent& fx, f32 skyVis,
                                   f32 zoneWeight, Vec3& fill, f32& fill2D, f32& skyExposure) {
    const bool zone = fx.mode == static_cast<u8>(ECS::WorldLightMode::Zone);
    const f32 day = worldLightDayFactor(fx.timeOfDay);
    const f32 night = 1.0f - day;
    const f32 indoor = 1.0f - std::clamp(fx.indoorDarkness, 0.0f, 0.98f) * (1.0f - skyVis);
    fill = fx.sunColor * (fx.ambientDay * day) + fx.moonColor * (fx.ambientNight * night);
    fill2D = fx.ambientDay * day + fx.ambientNight * night;
    if (zone) {
        fill = fill * (1.0f - zoneWeight) + fx.zoneColor * (fx.zoneIntensity * zoneWeight);
        fill2D = fill2D * (1.0f - zoneWeight) + fx.zoneIntensity * zoneWeight;
    }
    if (fx.environmentLighting != 0) {
        fill = fill * indoor;
        fill2D = std::max(0.02f, fill2D * std::max(indoor, 0.08f));
    }
    skyExposure = (zone ? (0.45f + 0.7f * zoneWeight) : (0.28f + 0.85f * day)) * indoor;
}

inline Vec3 environmentAmbientAt(ECS::World& world, const Vec3& viewPos) {
    Vec3 ambient(0.03f, 0.03f, 0.035f);
    bool found = false;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (isEffectivelyDisabled(world, entity) || fx.enabled == 0) return;
            const f32 sky =
                fx.environmentLighting != 0 ? sampleSkyVisibility(world, viewPos) : 1.0f;
            const f32 zoneWeight = environmentZoneWeight(world, entity, fx, viewPos);
            Vec3 fill{};
            f32 fill2D = 1.0f;
            f32 skyExposure = 1.0f;
            evaluateWorldLightFill(fx, sky, zoneWeight, fill, fill2D, skyExposure);
            if (!found) {
                ambient = fill;
                found = true;
            } else {
                ambient = Vec3(ambient.x + fill.x, ambient.y + fill.y, ambient.z + fill.z) * 0.5f;
            }
        });
    return ambient;
}

inline Vec3 environmentAmbient3D(ECS::World& world) {
    Vec3 ambient(0.03f, 0.03f, 0.035f);
    bool found = false;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (isEffectivelyDisabled(world, entity) || fx.enabled == 0) return;
            if (!found) {
                ambient = fx.drivenAmbient;
                found = true;
            } else {
                ambient = Vec3(ambient.x + fx.drivenAmbient.x, ambient.y + fx.drivenAmbient.y,
                               ambient.z + fx.drivenAmbient.z) *
                          0.5f;
            }
        });
    return ambient;
}

inline ECS::Entity ensureSunDirectional(ECS::World& world, ECS::Entity envEntity) {
    ECS::Entity sun;
    ECS::ComponentQuery lights;
    lights.with<ECS::LightComponent>();
    lights.with<ECS::DirectionalLightComponent>();
    world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
        lights, [&](ECS::Entity lightEntity, ECS::LightComponent&, ECS::DirectionalLightComponent&) {
            if (isEffectivelyDisabled(world, lightEntity)) return;
            if (!sun.isValid()) sun = lightEntity;
        });
    if (sun.isValid()) return sun;
    if (!world.has<ECS::LightComponent>(envEntity)) {
        auto& light = world.add<ECS::LightComponent>(envEntity);
        light.intensity = 1.2f;
    }
    if (!world.has<ECS::DirectionalLightComponent>(envEntity)) {
        world.add<ECS::DirectionalLightComponent>(envEntity);
    }
    if (!world.has<ECS::Position3D>(envEntity)) world.add<ECS::Position3D>(envEntity);
    if (!world.has<ECS::Rotation3D>(envEntity)) world.add<ECS::Rotation3D>(envEntity);
    applyDefaultSunOrientation(world, envEntity);
    return envEntity;
}

inline f32 environmentAmbient2D(ECS::World& world) {
    f32 ambient = 1.0f;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (isEffectivelyDisabled(world, entity) || fx.enabled == 0) return;
            ambient = std::min(ambient, fx.drivenAmbient2D);
        });
    return ambient;
}

inline bool rayHitsAabb(const Vec3& origin, const Vec3& dir, const Vec3& bmin, const Vec3& bmax,
                        f32 maxDist) {
    f32 tmin = 0.0f;
    f32 tmax = maxDist;
    for (int axis = 0; axis < 3; ++axis) {
        const f32 o = axis == 0 ? origin.x : (axis == 1 ? origin.y : origin.z);
        const f32 d = axis == 0 ? dir.x : (axis == 1 ? dir.y : dir.z);
        const f32 mn = axis == 0 ? bmin.x : (axis == 1 ? bmin.y : bmin.z);
        const f32 mx = axis == 0 ? bmax.x : (axis == 1 ? bmax.y : bmax.z);
        if (std::abs(d) < 1.0e-8f) {
            if (o < mn || o > mx) return false;
            continue;
        }
        const f32 inv = 1.0f / d;
        f32 t0 = (mn - o) * inv;
        f32 t1 = (mx - o) * inv;
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmax < tmin) return false;
    }
    return tmax > 0.05f;
}

inline f32 sampleSkyVisibility(ECS::World& world, const Vec3& origin) {
    struct Box {
        Vec3 min;
        Vec3 max;
    };
    std::vector<Box> boxes;
    ECS::ComponentQuery meshes;
    meshes.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(
        meshes, [&](ECS::Entity entity, ECS::MeshFilterComponent&) {
            if (isEffectivelyDisabled(world, entity)) return;
            Vec3 center{};
            Vec3 half(0.5f, 0.5f, 0.5f);
            if (auto* wt = world.get<WorldTransform>(entity)) {
                center = Vec3(wt->matrix(0, 3), wt->matrix(1, 3), wt->matrix(2, 3));
                half = Vec3(Vec3(wt->matrix(0, 0), wt->matrix(1, 0), wt->matrix(2, 0)).length(),
                            Vec3(wt->matrix(0, 1), wt->matrix(1, 1), wt->matrix(2, 1)).length(),
                            Vec3(wt->matrix(0, 2), wt->matrix(1, 2), wt->matrix(2, 2)).length()) *
                       0.5f;
            } else if (auto* p = world.get<ECS::Position3D>(entity)) {
                center = p->position;
                if (auto* s = world.get<ECS::Scale3D>(entity)) {
                    half = Vec3(std::abs(s->scale.x), std::abs(s->scale.y), std::abs(s->scale.z)) * 0.5f;
                }
            } else {
                return;
            }
            half.x = std::max(half.x, 0.05f);
            half.y = std::max(half.y, 0.05f);
            half.z = std::max(half.z, 0.05f);
            boxes.push_back({center - half, center + half});
        });

    const Vec3 dirs[] = {
        {0.00f, 1.00f, 0.00f}, {0.35f, 0.92f, 0.00f}, {-0.35f, 0.92f, 0.00f},
        {0.00f, 0.92f, 0.35f}, {0.00f, 0.92f, -0.35f}, {0.50f, 0.75f, 0.40f},
        {-0.50f, 0.75f, 0.40f}, {0.50f, 0.75f, -0.40f}, {-0.50f, 0.75f, -0.40f},
    };
    int blocked = 0;
    const int count = static_cast<int>(sizeof(dirs) / sizeof(dirs[0]));
    const Vec3 start = origin + Vec3(0.0f, 0.35f, 0.0f);
    for (int i = 0; i < count; ++i) {
        const Vec3 dir = dirs[i].normalized();
        bool hit = false;
        for (const Box& box : boxes) {
            if (rayHitsAabb(start, dir, box.min, box.max, 36.0f)) {
                hit = true;
                break;
            }
        }
        if (hit) ++blocked;
    }
    return 1.0f - static_cast<f32>(blocked) / static_cast<f32>(count);
}

inline bool environmentWantsShadows(ECS::World& world) {
    bool wanted = false;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (!isEffectivelyDisabled(world, entity) && fx.enabled != 0 && fx.castShadows != 0) {
                wanted = true;
            }
        });
    return wanted;
}

inline bool environmentHasOverride(ECS::World& world) {
    bool found = false;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (!isEffectivelyDisabled(world, entity) && fx.enabled != 0) found = true;
        });
    return found;
}

inline void ensureEnvironmentRenderStack(ECS::World& world) {
    ECS::Entity host = ECS::Entity::INVALID;
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (host.isValid()) return;
            if (!isEffectivelyDisabled(world, entity) && fx.enabled != 0) host = entity;
        });
    if (!host.isValid()) return;
    if (!world.has<ECS::ForwardRenderFeaturesComponent>(host)) {
        auto& features = world.add<ECS::ForwardRenderFeaturesComponent>(host);
        features.ibl.enabled = false;
        features.occlusion.enabled = true;
        features.volumetrics.enabled = true;
        features.volumetrics.quality = Render::VolumetricQuality::High;
        features.volumetrics.sampleShadows = true;
        features.volumetrics.density = 0.045f;
        features.volumetrics.height = 80.0f;
        features.volumetrics.anisotropy = 0.62f;
    }
    if (!world.has<ECS::PostProcessComponent>(host)) {
        auto& post = world.add<ECS::PostProcessComponent>(host);
        post.ambientOcclusion.enabled = true;
        post.ambientOcclusion.intensity = 1.1f;
        post.ambientOcclusion.radius = 0.55f;
    }
}

inline void tickEnvironmentEffects(ECS::World& world, f32 dt, const Vec3& viewPos) {
    ensureEnvironmentRenderStack(world);
    ECS::ComponentQuery query;
    query.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        query, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            if (isEffectivelyDisabled(world, entity) || fx.enabled == 0) return;

            if (fx.kind == static_cast<u8>(ECS::EnvironmentEffectKind::WorldLight) &&
                fx.mode == static_cast<u8>(ECS::WorldLightMode::DayNight) &&
                fx.dayLengthSeconds > 0.05f && dt > 0.0f) {
                fx.timeOfDay = std::fmod(fx.timeOfDay + (dt * 24.0f) / fx.dayLengthSeconds, 24.0f);
                if (fx.timeOfDay < 0.0f) fx.timeOfDay += 24.0f;
            }

            const bool zone = fx.mode == static_cast<u8>(ECS::WorldLightMode::Zone);
            const f32 zoneWeight = environmentZoneWeight(world, entity, fx, viewPos);
            const f32 day = worldLightDayFactor(fx.timeOfDay);
            const f32 night = 1.0f - day;
            fx.skyVisibility = fx.environmentLighting != 0 ? sampleSkyVisibility(world, viewPos) : 1.0f;
            Vec3 fill{};
            f32 fill2D = 1.0f;
            evaluateWorldLightFill(fx, fx.skyVisibility, zoneWeight, fill, fill2D, fx.skyExposure);
            fx.drivenAmbient = fill;
            fx.drivenAmbient2D = std::clamp(fill2D, 0.02f, 2.0f);

            if (fx.driveExistingLights != 0 && !zone) {
                const Vec3 sunDir = worldLightSunDirection(fx.timeOfDay);
                const Vec3 lightColor = fx.sunColor * day + fx.moonColor * night;
                const f32 intensity = fx.sunIntensity * day + fx.moonIntensity * night;
                const ECS::Entity sun = ensureSunDirectional(world, entity);
                if (auto* light = world.get<ECS::LightComponent>(sun)) {
                    light->color = Vec4(lightColor.x, lightColor.y, lightColor.z, 1.0f);
                    light->intensity = intensity;
                }
                if (auto* dir = world.get<ECS::DirectionalLightComponent>(sun)) {
                    dir->castShadows = fx.castShadows != 0 && fx.shadowStrength > 0.05f;
                }
                applySunDirection(world, sun, sunDir);
            }

            ECS::ComponentQuery skies;
            skies.with<ECS::SkyboxComponent>();
            world.forEach<ECS::SkyboxComponent>(skies, [&](ECS::Entity skyEntity, ECS::SkyboxComponent& sky) {
                if (isEffectivelyDisabled(world, skyEntity) || !sky.enabled) return;
                sky.exposure = fx.skyExposure;
            });

            const f32 ao = zone ? (fx.aoDay * zoneWeight + fx.aoNight * (1.0f - zoneWeight))
                                : (fx.aoDay * day + fx.aoNight * night);
            ECS::ComponentQuery features;
            features.with<ECS::ForwardRenderFeaturesComponent>();
            world.forEach<ECS::ForwardRenderFeaturesComponent>(
                features, [&](ECS::Entity fxEntity, ECS::ForwardRenderFeaturesComponent& fwd) {
                    if (isEffectivelyDisabled(world, fxEntity) || !fwd.enabled) return;
                    fwd.occlusion.enabled = fx.ambientOcclusion != 0 && ao > 0.02f;
                    fwd.occlusion.minRadius = std::clamp(0.45f + ao, 0.35f, 2.0f);
                    fwd.volumetrics.enabled = fx.volumetricsEnabled != 0;
                    if (fwd.volumetrics.enabled) {
                        fwd.volumetrics.quality = Render::VolumetricQuality::High;
                        fwd.volumetrics.sampleShadows = fx.castShadows != 0;
                        fwd.volumetrics.density = std::max(fx.volumetricDensity, 0.002f);
                        fwd.volumetrics.height = 80.0f;
                        fwd.volumetrics.anisotropy = 0.62f;
                    }
                });

            ECS::ComponentQuery posts;
            posts.with<ECS::PostProcessComponent>();
            world.forEach<ECS::PostProcessComponent>(
                posts, [&](ECS::Entity postEntity, ECS::PostProcessComponent& post) {
                    if (isEffectivelyDisabled(world, postEntity) || !post.enabled) return;
                    post.ambientOcclusion.enabled = fx.ambientOcclusion != 0;
                    if (post.ambientOcclusion.enabled) {
                        post.ambientOcclusion.intensity = std::clamp(ao * (1.4f - 0.6f * fx.skyVisibility),
                                                                    0.25f, 3.0f);
                        post.ambientOcclusion.radius = 0.55f;
                    }
                });
        });
}

}  // namespace Caffeine::Scene
