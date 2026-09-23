#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Entity.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include <string>
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

    void clear();
};

struct SceneShadowMaps {
    std::vector<CpuDirectionalShadowMap> directionals;
    std::vector<CpuSpotShadowMap> spots;

    void clear();
};

struct SceneLighting {
    LightingData lights;
    SceneShadowMaps shadows;

    void clear();
};

void collectSceneLights(ECS::World& world, LightingData& out);

/// Default key-light direction used when a directional light has identity rotation
/// (local -Z). Identity sun lights the horizon, not the floor.
Vec3 defaultSunDirection();
bool directionalLightHasExplicitAim(ECS::World& world, ECS::Entity entity);
void applyDefaultSunOrientation(ECS::World& world, ECS::Entity entity);

void buildSceneShadowMaps(ECS::World& world, const LightingData& lights, const Vec3& focus,
                          const std::string& projectRoot, SceneShadowMaps& out,
                          ECS::Entity skipEntity = ECS::Entity::INVALID);

void gatherSceneLighting(ECS::World& world, const Vec3& focus, const std::string& projectRoot,
                         SceneLighting& out, ECS::Entity skipEntity = ECS::Entity::INVALID,
                         bool buildCpuShadowMaps = true);

Vec3 evaluateDiffuseLighting(const LightingData& lights, const SceneShadowMaps& shadows,
                             const Vec3& worldPos, const Vec3& worldNormal, bool receiveShadows,
                             const Vec3& ambient = Vec3(0.18f, 0.18f, 0.18f));

}  // namespace Caffeine::Scene
