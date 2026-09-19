#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "terrain/TerrainSplatmap.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#endif

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

struct TerrainGpuTextures;

class TerrainCache {
public:
    static TerrainCache& instance();

    void initializeEntity(ECS::World& world, ECS::Entity entity);
    void syncEntity(ECS::World& world, ECS::Entity entity);
    void removeEntity(ECS::Entity entity);
    void clear();

    TerrainHeightmap* heightmapFor(ECS::Entity entity);
    const TerrainHeightmap* heightmapFor(ECS::Entity entity) const;
    TerrainSplatmap* splatmapFor(ECS::Entity entity);
    const TerrainSplatmap* splatmapFor(ECS::Entity entity) const;
    Assets::Mesh3D* meshFor(ECS::Entity entity);

    void rebuildRegion(ECS::Entity entity, const ECS::TerrainComponent& settings,
                       u32 minVertexX, u32 minVertexZ,
                       u32 maxVertexX, u32 maxVertexZ);

    void gatherDrawMeshes(ECS::Entity entity,
                          const ECS::TerrainComponent& settings,
                          const Mat4& worldMatrix,
                          const Vec3& cameraPos,
                          const Spatial::Frustum& frustum,
                          std::vector<TerrainDrawChunk>& outDraws,
                          TerrainCullStats* stats = nullptr);

    void syncTextureToFilter(ECS::World& world, ECS::Entity entity,
                             const ECS::TerrainComponent& terrain);
    void repairTexturePaths(ECS::TerrainComponent& terrain);
    void generateTerrain(ECS::World& world, ECS::Entity entity, ECS::TerrainComponent& terrain);

#ifdef CF_HAS_SDL3
    void syncGpuTextures(RHI::RenderDevice* device, ECS::Entity entity,
                           const ECS::TerrainComponent& terrain,
                           const std::string& projectRoot);
    const TerrainGpuTextures* gpuTexturesFor(ECS::Entity entity) const;
#endif

    bool saveTerrainFile(ECS::Entity entity, const std::filesystem::path& path,
                         bool includeSplat) const;
    bool loadTerrainFile(ECS::World& world, ECS::Entity entity,
                         ECS::TerrainComponent& terrain,
                         const std::filesystem::path& path);

#ifdef CF_HAS_SDL3
    void releaseGpuResources(RHI::RenderDevice* device);
#endif

private:
    struct TerrainEntry {
        TerrainHeightmap heightmap;
        TerrainSplatmap splatmap;
        std::unique_ptr<Assets::Mesh3D> mesh;
        std::vector<TerrainChunk> chunks;
        std::vector<u32> chunkActiveLods;
        bool useChunks = false;
        u32 builtRevision = 0;
    };

    TerrainEntry& ensureEntry(ECS::Entity entity);
    void rebuildMesh(ECS::Entity entity, TerrainEntry& entry, ECS::TerrainComponent& component);
#ifdef CF_HAS_SDL3
    void releaseMeshGpu(Assets::Mesh3D& mesh);
#endif

    std::unordered_map<u32, TerrainEntry> m_entries;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_gpuDevice = nullptr;
#endif
};

}  // namespace Caffeine::Terrain
