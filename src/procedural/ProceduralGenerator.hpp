#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "ecs/World.hpp"

namespace Caffeine::Procedural {

class ProceduralGenerator {
public:
    static void generateTerrainChunk(ECS::World& world, ECS::Entity terrainEntity,
                                     i32 chunkX, i32 chunkZ,
                                     const ECS::ProceduralWorldComponent& settings);

    static std::vector<u32> generateStructureChunk(ECS::World& world, ECS::Entity directorEntity,
                                                     i32 chunkX, i32 chunkZ,
                                                     const ECS::ProceduralWorldComponent& settings);
};

}  // namespace Caffeine::Procedural
