#pragma once

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include <string>
#include <vector>

namespace Caffeine::Scene {

struct DirectionalLightData;
struct SpotLightData;

struct CpuDirectionalShadowMap {
    bool valid = false;
    int resolution = 512;
    Mat4 lightVP;
    std::vector<f32> depth;

    void reset();
    void setup(const Vec3& lightDirection, const Vec3& focus, f32 shadowDistance, int res = 512);
    f32 sample(const Vec3& worldPos, f32 bias = 0.0015f) const;
    f32 samplePcf(const Vec3& worldPos, f32 bias = 0.0015f, int kernelRadius = 1) const;
};

struct CpuSpotShadowMap {
    bool valid = false;
    int resolution = 512;
    Mat4 lightVP;
    std::vector<f32> depth;

    void reset();
    void setup(const Vec3& position, const Vec3& direction, f32 radius, f32 angleDegrees,
               int res = 512);
    f32 sample(const Vec3& worldPos, f32 bias = 0.0015f) const;
    f32 samplePcf(const Vec3& worldPos, f32 bias = 0.0015f, int kernelRadius = 1) const;
};

bool meshCastsShadows(ECS::World& world, ECS::Entity entity);
bool meshReceivesShadows(ECS::World& world, ECS::Entity entity);

void buildDirectionalShadowMap(ECS::World& world, CpuDirectionalShadowMap& out,
                               const DirectionalLightData& light, const Vec3& focus,
                               const std::string& projectRoot,
                               ECS::Entity skipEntity = ECS::Entity::INVALID);

void buildSpotShadowMap(ECS::World& world, CpuSpotShadowMap& out, const SpotLightData& light,
                        const std::string& projectRoot,
                        ECS::Entity skipEntity = ECS::Entity::INVALID);

void rasterizeSceneShadowCasters(ECS::World& world, const Mat4& lightVP, int resolution,
                                 std::vector<f32>& depth, const std::string& projectRoot,
                                 ECS::Entity skipEntity);

}  // namespace Caffeine::Scene
