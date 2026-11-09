#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"

namespace Caffeine::Terrain {

class TerrainMeshBuilder {
public:
    static Assets::Mesh3D build(const TerrainHeightmap& heightmap,
                                const ECS::TerrainComponent& settings);

    static Assets::Mesh3D buildRegion(const TerrainHeightmap& heightmap,
                                      const ECS::TerrainComponent& settings,
                                      u32 startX, u32 startZ,
                                      u32 endX, u32 endZ,
                                      u32 sampleStep = 1);
};

}  // namespace Caffeine::Terrain
