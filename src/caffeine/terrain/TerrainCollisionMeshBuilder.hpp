#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"

namespace Caffeine::Terrain {

/// Coarse triangle mesh for physics/collision queries (decimated heightfield).
class TerrainCollisionMeshBuilder {
public:
    static Assets::Mesh3D build(const TerrainHeightmap& heightmap,
                                const ECS::TerrainComponent& settings);
};

}  // namespace Caffeine::Terrain
