#pragma once

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainSplatmap.hpp"
#include "terrain/generation/TerrainGeneratorTypes.hpp"
#include "terrain/generation/TerrainHydrology.hpp"

#include <vector>

namespace Caffeine::Terrain {

struct TerrainGenerationState {
    std::vector<f32> moistureMap;
    RiverNetwork riverNetwork;
};

struct TerrainGeneratorContext {
    TerrainHeightmap& heightmap;
    TerrainSplatmap& splatmap;
    const ECS::TerrainComponent& terrain;
    ECS::TerrainGenerationSettings& settings;
    TerrainGenerationState* state = nullptr;
};

class ITerrainGeneratorModule {
public:
    virtual ~ITerrainGeneratorModule() = default;
    virtual const char* name() const = 0;
    virtual void apply(TerrainGeneratorContext& ctx) = 0;
};

class TerrainGeneratorPipeline {
public:
    void addModule(ITerrainGeneratorModule& module);
    void clear();
    void run(TerrainGeneratorContext& ctx) const;

private:
    std::vector<ITerrainGeneratorModule*> m_modules;
};

void applyGenerationStylePreset(ECS::TerrainGenerationSettings& settings, ECS::TerrainGenStyle style);

class TerrainGenerator {
public:
    static void generate(TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                         const ECS::TerrainComponent& terrain,
                         ECS::TerrainGenerationSettings& settings);
};

}  // namespace Caffeine::Terrain
