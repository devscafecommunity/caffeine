#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <string>

namespace Caffeine::Photon {

struct BakeSettings {
    u32 resolution = 512;
    u32 rays = 16;
    f32 maxDistance = 2.5f;
    f32 bias = 0.02f;
    /// Use the GPU compute path when `renderDevice` is provided.
    bool useGpu = true;
};

struct BakeResult {
    bool ok = false;
    bool usedGpu = false;
    std::string error;
    std::string ormPath;
    std::string materialPath;
    u32 coveredTexels = 0;
    u32 triangles = 0;
};

/// Bakes static ambient occlusion for `entity` into an ORM texture and assigns
/// it to the entity material. The scene's static meshes are used as occluders.
/// Pass a RenderDevice* (as void*) to enable the GPU compute path; when null or
/// unavailable the CPU BVH path is used.
BakeResult bakeAmbientOcclusion(ECS::World& world, ECS::Entity entity,
                                const BakeSettings& settings,
                                const std::string& projectRoot,
                                void* renderDevice = nullptr);

}  // namespace Caffeine::Photon
