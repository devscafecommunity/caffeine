#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include <vector>

namespace Caffeine::Scene {

struct DirectionalLightData {
    Vec3 direction;
    Vec4 color;
    f32 intensity;
    f32 shadowDistance;
    bool castShadows;
};

struct PointLightData {
    Vec3 position;
    Vec4 color;
    f32 intensity;
    f32 radius;
    bool castShadows;
};

struct SpotLightData {
    Vec3 position;
    Vec3 direction;
    Vec4 color;
    f32 intensity;
    f32 radius;
    f32 angle;
    bool castShadows;
};

struct LightingData {
    std::vector<DirectionalLightData> directionals;
    std::vector<PointLightData> points;
    std::vector<SpotLightData> spots;

    void clear() {
        directionals.clear();
        points.clear();
        spots.clear();
    }
};

inline void collectLights(ECS::World& world, LightingData& out) {
    out.clear();

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();
        q.with<ECS::Transform>();
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent, ECS::Transform>(
            q, [&](ECS::Entity, ECS::LightComponent& lc, ECS::DirectionalLightComponent& dl, ECS::Transform& t) {
                static constexpr float DEG2RAD = 3.14159265f / 180.f;
                float rx = t.rotation.x * DEG2RAD;
                float ry = t.rotation.y * DEG2RAD;
                Vec3 dir = {
                    -sinf(ry) * cosf(rx),
                    sinf(rx),
                    -cosf(ry) * cosf(rx)
                };
                out.directionals.push_back({dir, lc.color, lc.intensity, dl.shadowDistance, dl.castShadows});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();
        q.with<ECS::Transform>();
        world.forEach<ECS::LightComponent, ECS::PointLightComponent, ECS::Transform>(
            q, [&](ECS::Entity, ECS::LightComponent& lc, ECS::PointLightComponent& pl, ECS::Transform& t) {
                out.points.push_back({t.position, lc.color, lc.intensity, pl.radius, pl.castShadows});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::SpotLightComponent>();
        q.with<ECS::Transform>();
        world.forEach<ECS::LightComponent, ECS::SpotLightComponent, ECS::Transform>(
            q, [&](ECS::Entity, ECS::LightComponent& lc, ECS::SpotLightComponent& sl, ECS::Transform& t) {
                static constexpr float DEG2RAD = 3.14159265f / 180.f;
                float rx = t.rotation.x * DEG2RAD;
                float ry = t.rotation.y * DEG2RAD;
                Vec3 dir = {
                    -sinf(ry) * cosf(rx),
                    sinf(rx),
                    -cosf(ry) * cosf(rx)
                };
                out.spots.push_back({t.position, dir, lc.color, lc.intensity, sl.radius, sl.angle, sl.castShadows});
            });
    }
}

}  // namespace Caffeine::Scene
