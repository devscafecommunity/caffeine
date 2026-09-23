#pragma once

#include "assets/MeshTypes.hpp"
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "spatial/Octree.hpp"

#include <memory>
#include <vector>

namespace Caffeine::Terrain {

struct TerrainChunk {
    u32 chunkX = 0;
    u32 chunkZ = 0;
    u32 startVertexX = 0;
    u32 startVertexZ = 0;
    u32 endVertexX = 0;
    u32 endVertexZ = 0;
    Vec3 localBoundsMin;
    Vec3 localBoundsMax;
    std::vector<std::unique_ptr<Assets::Mesh3D>> lodMeshes;
};

struct TerrainCullStats {
    u32 totalChunks = 0;
    u32 visibleChunks = 0;
    u32 culledChunks = 0;
    u32 lodCounts[8] = {};
};

struct TerrainDrawChunk {
    Assets::Mesh3D* mesh = nullptr;
    u32 lodLevel = 0;
    u32 chunkX = 0;
    u32 chunkZ = 0;
    Vec3 localBoundsMin;
    Vec3 localBoundsMax;
};

class TerrainLodSystem {
public:
    static u32 chunkCountAlongAxis(u32 resolution, u32 chunkVertexCount);
    static u32 selectLodLevel(f32 distance, const ECS::TerrainComponent& settings);
    static u32 selectLodLevelWithHysteresis(f32 distance, u32 currentLod,
                                            const ECS::TerrainComponent& settings);
    static u32 lodSampleStep(u32 lodLevel);

    static void rebuildChunks(std::vector<TerrainChunk>& chunks,
                              const TerrainHeightmap& heightmap,
                              const ECS::TerrainComponent& settings);

    static void rebuildChunk(TerrainChunk& chunk,
                             const TerrainHeightmap& heightmap,
                             const ECS::TerrainComponent& settings);

    static void rebuildChunksInRegion(std::vector<TerrainChunk>& chunks,
                                      const TerrainHeightmap& heightmap,
                                      const ECS::TerrainComponent& settings,
                                      u32 minVertexX, u32 minVertexZ,
                                      u32 maxVertexX, u32 maxVertexZ);

    static Spatial::AABB3D worldBoundsFromLocal(const Mat4& worldMatrix,
                                                const Vec3& localMin,
                                                const Vec3& localMax);

    static void gatherDrawMeshes(std::vector<TerrainChunk>& chunks,
                                 std::vector<u32>& activeLods,
                                 const ECS::TerrainComponent& settings,
                                 const Mat4& worldMatrix,
                                 const Vec3& cameraPos,
                                 const Spatial::Frustum& frustum,
                                 std::vector<TerrainDrawChunk>& outDraws,
                                 TerrainCullStats* stats = nullptr,
                                 f32 lodDistanceScale = 1.0f);
};

}  // namespace Caffeine::Terrain
