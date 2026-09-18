#pragma once

#include "ecs/TerrainComponents.hpp"
#include "math/Vec4.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/generation/TerrainGeneratorTypes.hpp"

#include <vector>

namespace Caffeine::Terrain {

struct RiverChannel {
    std::vector<std::pair<f32, f32>> points;
    f32 flowRate = 0.0f;
};

struct RiverNetwork {
    std::vector<RiverChannel> channels;
};

std::vector<f32> buildMoistureMap(const TerrainHeightmap& heightmap, const ECS::TerrainClimateSettings& climate,
                                  const ECS::TerrainHydrologySettings& hydrology, const RiverNetwork& rivers,
                                  u32 seed);

RiverNetwork traceRiverNetwork(TerrainHeightmap& heightmap, const ECS::TerrainHydrologySettings& hydrology,
                               const std::vector<f32>& moistureMap, u32 seed);

void carveRiverChannels(TerrainHeightmap& heightmap, const RiverNetwork& network,
                        const ECS::TerrainHydrologySettings& hydrology);

void depositSedimentInValleys(TerrainHeightmap& heightmap, const RiverNetwork& network, f32 strength);

Vec4 assignBiomeSplatWeights(f32 elevation, f32 moisture, f32 slope, f32 temperature, f32 blend,
                             f32 seaLevel);

}  // namespace Caffeine::Terrain
