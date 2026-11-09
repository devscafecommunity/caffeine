#include "terrain/TerrainCollisionMeshBuilder.hpp"

#include "terrain/TerrainMeshBuilder.hpp"

#include <algorithm>

namespace Caffeine::Terrain {

Assets::Mesh3D TerrainCollisionMeshBuilder::build(const TerrainHeightmap& heightmap,
                                                  const ECS::TerrainComponent& settings) {
    if (heightmap.empty()) return {};

    const u32 step = std::max(1u, settings.collisionSampleStep);
    const u32 lastX = heightmap.resolutionX() - 1;
    const u32 lastZ = heightmap.resolutionZ() - 1;

    Assets::Mesh3D mesh =
        TerrainMeshBuilder::buildRegion(heightmap, settings, 0, 0, lastX, lastZ, step);

    // Collision meshes only need positions; strip extras to save memory.
    for (auto& vertex : mesh.vertices) {
        vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
        vertex.texcoord = Vec2(0.0f, 0.0f);
        vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    return mesh;
}

}  // namespace Caffeine::Terrain
