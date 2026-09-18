#include "scene/TerrainSystem.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"

namespace Caffeine::Scene {

void syncTerrainMeshes(ECS::World& world) {
    ECS::ComponentQuery query;
    query.with<ECS::TerrainComponent>();
    world.forEach<ECS::TerrainComponent>(query, [&](ECS::Entity entity, ECS::TerrainComponent&) {
        Terrain::TerrainCache::instance().syncEntity(world, entity);
    });
}

}  // namespace Caffeine::Scene
