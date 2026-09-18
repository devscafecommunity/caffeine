#pragma once

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"

namespace Caffeine::Terrain {

struct GeologicalSimulationResult {
    u32 iterationsRun = 0;
    f32 averageDelta = 0.0f;
    bool converged = false;
};

class GeologicalSimulator {
public:
    static GeologicalSimulationResult run(TerrainHeightmap& heightmap,
                                          const ECS::TerrainComponent& terrain,
                                          ECS::TerrainGenerationSettings& settings);
};

void applyEnvironmentPreset(ECS::TerrainGenerationSettings& settings,
                              ECS::TerrainEnvironment environment);

}  // namespace Caffeine::Terrain
