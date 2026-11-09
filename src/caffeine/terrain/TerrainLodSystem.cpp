#include "terrain/TerrainLodSystem.hpp"

#include "terrain/TerrainMeshBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

bool rangesOverlap(u32 aMin, u32 aMax, u32 bMin, u32 bMax) {
    return aMin <= bMax && bMin <= aMax;
}

}  // namespace

u32 TerrainLodSystem::chunkCountAlongAxis(u32 resolution, u32 chunkVertexCount) {
    const u32 span = std::max(2u, chunkVertexCount);
    if (resolution <= 1) return 1;
    const u32 cells = resolution - 1;
    return std::max(1u, (cells + span - 2) / (span - 1));
}

u32 TerrainLodSystem::selectLodLevel(f32 distance, const ECS::TerrainComponent& settings) {
    const u32 maxLod = std::max(1u, settings.maxLodLevels);
    const f32 terrainSpan = std::max(settings.worldSizeX, settings.worldSizeZ);
    const f32 scale = std::max(std::max(settings.lodDistanceScale, 80.0f), terrainSpan * 2.5f);
    const u32 lod = static_cast<u32>(distance / scale);
    return std::min(lod, maxLod - 1);
}

u32 TerrainLodSystem::selectLodLevelWithHysteresis(f32 distance, u32 currentLod,
                                                   const ECS::TerrainComponent& settings) {
    const u32 maxLod = std::max(1u, settings.maxLodLevels);
    currentLod = std::min(currentLod, maxLod - 1);

    const f32 scale = std::max(std::max(settings.lodDistanceScale, 80.0f),
                               std::max(settings.worldSizeX, settings.worldSizeZ) * 2.5f);
    const f32 hysteresis = std::clamp(settings.lodHysteresis, 0.0f, 0.9f) * scale;
    const u32 desired = selectLodLevel(distance, settings);

    if (desired > currentLod) {
        const f32 threshold = static_cast<f32>(currentLod + 1) * scale + hysteresis;
        return distance >= threshold ? desired : currentLod;
    }
    if (desired < currentLod) {
        const f32 threshold = static_cast<f32>(currentLod) * scale - hysteresis;
        return distance <= threshold ? desired : currentLod;
    }
    return currentLod;
}

u32 TerrainLodSystem::lodSampleStep(u32 lodLevel) {
    return 1u << lodLevel;
}

void TerrainLodSystem::rebuildChunk(TerrainChunk& chunk,
                                  const TerrainHeightmap& heightmap,
                                  const ECS::TerrainComponent& settings) {
    const u32 maxLod = std::max(1u, settings.maxLodLevels);
    chunk.lodMeshes.resize(maxLod);
    for (u32 lod = 0; lod < maxLod; ++lod) {
        const u32 step = lodSampleStep(lod);
        chunk.lodMeshes[lod] = std::make_unique<Assets::Mesh3D>(
            TerrainMeshBuilder::buildRegion(heightmap, settings,
                                            chunk.startVertexX, chunk.startVertexZ,
                                            chunk.endVertexX, chunk.endVertexZ,
                                            step));
    }

    if (!chunk.lodMeshes.empty() && chunk.lodMeshes[0]) {
        chunk.localBoundsMin = chunk.lodMeshes[0]->bounds.min;
        chunk.localBoundsMax = chunk.lodMeshes[0]->bounds.max;
    }
}

void TerrainLodSystem::rebuildChunks(std::vector<TerrainChunk>& chunks,
                                     const TerrainHeightmap& heightmap,
                                     const ECS::TerrainComponent& settings) {
    chunks.clear();
    if (heightmap.empty()) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const u32 span = std::max(2u, settings.chunkVertexCount);
    const u32 chunksX = chunkCountAlongAxis(resX, span);
    const u32 chunksZ = chunkCountAlongAxis(resZ, span);

    chunks.reserve(static_cast<size_t>(chunksX) * static_cast<size_t>(chunksZ));

    for (u32 cz = 0; cz < chunksZ; ++cz) {
        for (u32 cx = 0; cx < chunksX; ++cx) {
            TerrainChunk chunk;
            chunk.chunkX = cx;
            chunk.chunkZ = cz;
            chunk.startVertexX = cx * (span - 1);
            chunk.startVertexZ = cz * (span - 1);
            chunk.endVertexX = std::min(resX - 1, chunk.startVertexX + span - 1);
            chunk.endVertexZ = std::min(resZ - 1, chunk.startVertexZ + span - 1);
            rebuildChunk(chunk, heightmap, settings);
            chunks.push_back(std::move(chunk));
        }
    }
}

void TerrainLodSystem::rebuildChunksInRegion(std::vector<TerrainChunk>& chunks,
                                             const TerrainHeightmap& heightmap,
                                             const ECS::TerrainComponent& settings,
                                             u32 minVertexX, u32 minVertexZ,
                                             u32 maxVertexX, u32 maxVertexZ) {
    for (TerrainChunk& chunk : chunks) {
        if (rangesOverlap(chunk.startVertexX, chunk.endVertexX, minVertexX, maxVertexX) &&
            rangesOverlap(chunk.startVertexZ, chunk.endVertexZ, minVertexZ, maxVertexZ)) {
            rebuildChunk(chunk, heightmap, settings);
        }
    }
}

Spatial::AABB3D TerrainLodSystem::worldBoundsFromLocal(const Mat4& worldMatrix,
                                                       const Vec3& localMin,
                                                       const Vec3& localMax) {
    const Vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMin.z},
        {localMin.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMin.z},
        {localMin.x, localMin.y, localMax.z},
        {localMax.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMax.z},
        {localMax.x, localMax.y, localMax.z},
    };

    Spatial::AABB3D bounds;
    bounds.min = worldMatrix.transformPoint(corners[0]);
    bounds.max = bounds.min;
    for (int i = 1; i < 8; ++i) {
        const Vec3 p = worldMatrix.transformPoint(corners[i]);
        bounds.min.x = std::min(bounds.min.x, p.x);
        bounds.min.y = std::min(bounds.min.y, p.y);
        bounds.min.z = std::min(bounds.min.z, p.z);
        bounds.max.x = std::max(bounds.max.x, p.x);
        bounds.max.y = std::max(bounds.max.y, p.y);
        bounds.max.z = std::max(bounds.max.z, p.z);
    }
    return bounds;
}

void TerrainLodSystem::gatherDrawMeshes(std::vector<TerrainChunk>& chunks,
                                      std::vector<u32>& activeLods,
                                      const ECS::TerrainComponent& settings,
                                      const Mat4& worldMatrix,
                                      const Vec3& cameraPos,
                                      const Spatial::Frustum& frustum,
                                      std::vector<TerrainDrawChunk>& outDraws,
                                      TerrainCullStats* stats,
                                      f32 lodDistanceScale) {
    lodDistanceScale = std::max(lodDistanceScale, 1.0f);
    if (activeLods.size() != chunks.size()) {
        activeLods.assign(chunks.size(), 0);
    }

    if (stats) {
        stats->totalChunks = static_cast<u32>(chunks.size());
        stats->visibleChunks = 0;
        stats->culledChunks = 0;
        for (u32 i = 0; i < 8; ++i) stats->lodCounts[i] = 0;
    }

    for (size_t i = 0; i < chunks.size(); ++i) {
        TerrainChunk& chunk = chunks[i];
        if (chunk.lodMeshes.empty()) continue;

        const Vec3 centerLocal = (chunk.localBoundsMin + chunk.localBoundsMax) * 0.5f;
        const Vec3 centerWorld = worldMatrix.transformPoint(centerLocal);
        const f32 distance = (centerWorld - cameraPos).length() * lodDistanceScale;
        const Spatial::AABB3D worldBounds =
            worldBoundsFromLocal(worldMatrix, chunk.localBoundsMin, chunk.localBoundsMax);

        if (settings.frustumCull && !frustum.intersects(worldBounds)) {
            if (stats) ++stats->culledChunks;
            continue;
        }

        const u32 lod = selectLodLevelWithHysteresis(distance, activeLods[i], settings);
        activeLods[i] = lod;

        Assets::Mesh3D* mesh = nullptr;
        if (lod < chunk.lodMeshes.size()) {
            mesh = chunk.lodMeshes[lod].get();
        }
        if (!mesh || mesh->vertices.empty()) {
            mesh = chunk.lodMeshes[0].get();
        }
        if (!mesh || mesh->vertices.empty()) continue;

        TerrainDrawChunk draw;
        draw.mesh = mesh;
        draw.lodLevel = lod;
        draw.chunkX = chunk.chunkX;
        draw.chunkZ = chunk.chunkZ;
        draw.localBoundsMin = chunk.localBoundsMin;
        draw.localBoundsMax = chunk.localBoundsMax;
        outDraws.push_back(draw);

        if (stats) {
            ++stats->visibleChunks;
            if (lod < 8) ++stats->lodCounts[lod];
        }
    }
}

}  // namespace Caffeine::Terrain
