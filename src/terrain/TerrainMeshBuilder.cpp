#include "terrain/TerrainMeshBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

Vec2 tiledUv(f32 worldX, f32 worldZ, const ECS::TerrainComponent& settings) {
    const f32 halfX = settings.worldSizeX * 0.5f;
    const f32 halfZ = settings.worldSizeZ * 0.5f;
    const f32 tile =
        std::max(settings.useSplatmap ? settings.splatTileSize : settings.textureTileSize, 0.1f);
    const f32 u = ((worldX + halfX) / settings.worldSizeX) * (settings.worldSizeX / tile);
    const f32 v = ((worldZ + halfZ) / settings.worldSizeZ) * (settings.worldSizeZ / tile);
    return Vec2(u, v);
}

}  // namespace

Assets::Mesh3D TerrainMeshBuilder::buildRegion(const TerrainHeightmap& heightmap,
                                               const ECS::TerrainComponent& settings,
                                               u32 startX, u32 startZ,
                                               u32 endX, u32 endZ,
                                               u32 sampleStep) {
    Assets::Mesh3D mesh;
    if (heightmap.empty()) return mesh;

    sampleStep = std::max(1u, sampleStep);
    startX = std::min(startX, heightmap.resolutionX() - 1);
    startZ = std::min(startZ, heightmap.resolutionZ() - 1);
    endX = std::min(endX, heightmap.resolutionX() - 1);
    endZ = std::min(endZ, heightmap.resolutionZ() - 1);
    if (endX < startX || endZ < startZ) return mesh;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 halfX = settings.worldSizeX * 0.5f;
    const f32 halfZ = settings.worldSizeZ * 0.5f;
    const f32 stepX = settings.worldSizeX / static_cast<f32>(std::max(1u, resX - 1));
    const f32 stepZ = settings.worldSizeZ / static_cast<f32>(std::max(1u, resZ - 1));

    const u32 vertCountX = (endX - startX) / sampleStep + 1;
    const u32 vertCountZ = (endZ - startZ) / sampleStep + 1;
    mesh.vertices.reserve(static_cast<size_t>(vertCountX) * static_cast<size_t>(vertCountZ));

    f32 minY = 1e9f;
    f32 maxY = -1e9f;

    for (u32 zi = 0; zi < vertCountZ; ++zi) {
        const u32 z = startZ + zi * sampleStep;
        for (u32 xi = 0; xi < vertCountX; ++xi) {
            const u32 x = startX + xi * sampleStep;
            const f32 u = static_cast<f32>(x) / static_cast<f32>(resX - 1);
            const f32 v = static_cast<f32>(z) / static_cast<f32>(resZ - 1);
            const f32 height = heightmap.sampleNormalized(x, z) * settings.maxHeight;

            const f32 px = -halfX + static_cast<f32>(x) * stepX;
            const f32 pz = -halfZ + static_cast<f32>(z) * stepZ;

            Assets::Vertex3D vertex;
            vertex.position = Vec3(px, height, pz);
            vertex.normal = heightmap.sampleNormalBilinear(u, v, settings.worldSizeX,
                                                           settings.worldSizeZ, settings.maxHeight);
            vertex.texcoord = tiledUv(px, pz, settings);
            vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            mesh.vertices.push_back(vertex);

            minY = std::min(minY, height);
            maxY = std::max(maxY, height);
        }
    }

    mesh.indices.reserve(static_cast<size_t>(vertCountX - 1) * static_cast<size_t>(vertCountZ - 1) * 6);
    for (u32 zi = 0; zi + 1 < vertCountZ; ++zi) {
        for (u32 xi = 0; xi + 1 < vertCountX; ++xi) {
            const u32 i00 = zi * vertCountX + xi;
            const u32 i10 = i00 + 1;
            const u32 i01 = i00 + vertCountX;
            const u32 i11 = i01 + 1;

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i10);

            mesh.indices.push_back(i10);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i11);
        }
    }

    const f32 localMinX = -halfX + static_cast<f32>(startX) * stepX;
    const f32 localMaxX = -halfX + static_cast<f32>(endX) * stepX;
    const f32 localMinZ = -halfZ + static_cast<f32>(startZ) * stepZ;
    const f32 localMaxZ = -halfZ + static_cast<f32>(endZ) * stepZ;
    mesh.bounds.min = Vec3(localMinX, minY, localMinZ);
    mesh.bounds.max = Vec3(localMaxX, maxY, localMaxZ);
    mesh.flipTextureV = false;
    return mesh;
}

Assets::Mesh3D TerrainMeshBuilder::build(const TerrainHeightmap& heightmap,
                                         const ECS::TerrainComponent& settings) {
    if (heightmap.empty()) return {};
    return buildRegion(heightmap, settings,
                       0, 0,
                       heightmap.resolutionX() - 1,
                       heightmap.resolutionZ() - 1,
                       1);
}

}  // namespace Caffeine::Terrain
