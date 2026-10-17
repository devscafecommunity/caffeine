#pragma once

#include "ecs/Components3D.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"

#include <cmath>

namespace Caffeine::Editor {

/// Tunable size of assets/benchmarks/dense_outdoor.caf.
/// Raise the terrain resolution toward 512 only when the frame budget allows it.
inline constexpr u32 kDenseOutdoorTerrainResolution = 129;
inline constexpr u32 kDenseOutdoorTreeCount = 240;
inline constexpr u32 kDenseOutdoorRockCount = 40;

inline void populateDenseOutdoorBenchmark(ECS::World& world) {
    {
        ECS::Entity env = world.create();
        setEntityName(world, env, "RenderEnvironment");
        auto& forward = world.add<ECS::ForwardRenderFeaturesComponent>(env);
        forward.instancing.enabled = true;
        forward.instancing.maxInstancesPerBatch = 256;
        forward.ibl.enabled = true;
        // Reflections / volumetrics / occlusion stay off unless you opt in here or via script.
    }
    {
        ECS::Entity sun = world.create();
        setEntityName(world, sun, "Sun");
        world.add<ECS::LightComponent>(sun).intensity = 1.1f;
        world.add<ECS::DirectionalLightComponent>(sun).shadowDistance = 80.0f;
    }
    {
        ECS::Entity point = world.create();
        setEntityName(world, point, "Point A");
        world.add<ECS::Position3D>(point).position = Vec3(-8.0f, 3.0f, 6.0f);
        auto& light = world.add<ECS::LightComponent>(point);
        light.color = Vec4(1.0f, 0.85f, 0.6f, 1.0f);
        light.intensity = 2.0f;
        world.add<ECS::PointLightComponent>(point).radius = 18.0f;
    }
    {
        ECS::Entity point = world.create();
        setEntityName(world, point, "Point B");
        world.add<ECS::Position3D>(point).position = Vec3(12.0f, 2.5f, -10.0f);
        auto& light = world.add<ECS::LightComponent>(point);
        light.color = Vec4(0.6f, 0.75f, 1.0f, 1.0f);
        light.intensity = 1.6f;
        world.add<ECS::PointLightComponent>(point).radius = 16.0f;
    }
    {
        ECS::Entity spot = world.create();
        setEntityName(world, spot, "Spot");
        world.add<ECS::Position3D>(spot).position = Vec3(0.0f, 6.0f, -8.0f);
        world.add<ECS::LightComponent>(spot).intensity = 3.0f;
        auto& cone = world.add<ECS::SpotLightComponent>(spot);
        cone.radius = 24.0f;
        cone.angle = 40.0f;
    }
    {
        ECS::Entity ground = world.create();
        setEntityName(world, ground, "Water");
        world.add<ECS::Position3D>(ground).position = Vec3(0.0f, 0.0f, 0.0f);
        world.add<ECS::Scale3D>(ground).scale = Vec3(80.0f, 1.0f, 80.0f);
        world.add<ECS::MeshFilterComponent>(ground).primitive = ECS::MeshPrimitive::Plane;
    }
    {
        ECS::Entity terrain = world.create();
        setEntityName(world, terrain, "Terrain");
        auto& component = world.add<ECS::TerrainComponent>(terrain);
        component.resolutionX = kDenseOutdoorTerrainResolution;
        component.resolutionZ = kDenseOutdoorTerrainResolution;
        component.worldSizeX = 128.0f;
        component.worldSizeZ = 128.0f;
        component.maxHeight = 12.0f;
        component.useSplatmap = false;
        component.buildCollisionMesh = false;
    }

    const int grid = 16;
    for (u32 i = 0; i < kDenseOutdoorTreeCount; ++i) {
        const int x = static_cast<int>(i % grid) - grid / 2;
        const int z = static_cast<int>(i / grid) - grid / 2;
        ECS::Entity tree = world.create();
        setEntityName(world, tree, "Tree");
        world.add<ECS::Position3D>(tree).position = Vec3(x * 3.2f, 0.5f, z * 3.2f);
        world.add<ECS::Scale3D>(tree).scale = Vec3(0.6f, 1.4f, 0.6f);
        world.add<ECS::MeshFilterComponent>(tree).primitive = ECS::MeshPrimitive::Sphere;
    }
    for (u32 i = 0; i < kDenseOutdoorRockCount; ++i) {
        ECS::Entity rock = world.create();
        setEntityName(world, rock, "Rock");
        const f32 angle = static_cast<f32>(i) * 0.7f;
        world.add<ECS::Position3D>(rock).position =
            Vec3(std::cos(angle) * 18.0f, 0.4f, std::sin(angle) * 18.0f);
        world.add<ECS::Scale3D>(rock).scale = Vec3(1.4f, 0.8f, 1.2f);
        world.add<ECS::MeshFilterComponent>(rock).primitive = ECS::MeshPrimitive::Cube;
    }
}

}  // namespace Caffeine::Editor
