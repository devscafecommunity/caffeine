#include "terrain/TerrainMeshBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

void computeMeshTangents(Assets::Mesh3D& mesh) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3) return;

    std::vector<Vec3> tan1(mesh.vertices.size(), Vec3::zero());
    std::vector<Vec3> tan2(mesh.vertices.size(), Vec3::zero());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 i0 = mesh.indices[i];
        const u32 i1 = mesh.indices[i + 1];
        const u32 i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        const Vec3& p0 = mesh.vertices[i0].position;
        const Vec3& p1 = mesh.vertices[i1].position;
        const Vec3& p2 = mesh.vertices[i2].position;
        const Vec2& uv0 = mesh.vertices[i0].texcoord;
        const Vec2& uv1 = mesh.vertices[i1].texcoord;
        const Vec2& uv2 = mesh.vertices[i2].texcoord;

        const Vec3 edge1 = p1 - p0;
        const Vec3 edge2 = p2 - p0;
        const f32 du1 = uv1.x - uv0.x;
        const f32 dv1 = uv1.y - uv0.y;
        const f32 du2 = uv2.x - uv0.x;
        const f32 dv2 = uv2.y - uv0.y;
        const f32 denom = du1 * dv2 - du2 * dv1;
        if (std::fabs(denom) < 1e-8f) continue;

        const f32 r = 1.0f / denom;
        const Vec3 sdir((dv2 * edge1.x - dv1 * edge2.x) * r,
                        (dv2 * edge1.y - dv1 * edge2.y) * r,
                        (dv2 * edge1.z - dv1 * edge2.z) * r);
        const Vec3 tdir((du1 * edge2.x - du2 * edge1.x) * r,
                        (du1 * edge2.y - du2 * edge1.y) * r,
                        (du1 * edge2.z - du2 * edge1.z) * r);

        tan1[i0] = tan1[i0] + sdir;
        tan1[i1] = tan1[i1] + sdir;
        tan1[i2] = tan1[i2] + sdir;
        tan2[i0] = tan2[i0] + tdir;
        tan2[i1] = tan2[i1] + tdir;
        tan2[i2] = tan2[i2] + tdir;
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3& n = mesh.vertices[i].normal;
        Vec3 t = tan1[i];
        if (t.lengthSquared() < 1e-12f) {
            mesh.vertices[i].tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            continue;
        }
        t = t - n * n.dot(t);
        t = t.normalized();
        const f32 w = (n.cross(t).dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        mesh.vertices[i].tangent = Vec4(t.x, t.y, t.z, w);
    }
}

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
    computeMeshTangents(mesh);
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
