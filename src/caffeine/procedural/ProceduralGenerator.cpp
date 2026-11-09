#include "procedural/ProceduralGenerator.hpp"

#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "procedural/ProceduralNoise.hpp"
#include "procedural/ProceduralProfiles.hpp"
#include "terrain/TerrainCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace Caffeine::Procedural {
namespace {

bool profileIs(const char* profile, const char* name) {
    return profile && name && std::strcmp(profile, name) == 0;
}

f32 heightForTerrainProfile(const char* profile, f32 worldX, f32 worldZ, u32 seed) {
    if (profileIs(profile, "flat")) {
        return 0.0f;
    }
    if (profileIs(profile, "hills")) {
        const f32 n = fbm2D(worldX * 0.02f, worldZ * 0.02f, seed, 5, 2.0f, 0.45f);
        const f32 hills = fbm2D(worldX * 0.005f, worldZ * 0.005f, seed + 17, 3, 2.0f, 0.5f);
        return n * 0.55f + hills * 0.25f;
    }
    return fbm2D(worldX * 0.015f, worldZ * 0.015f, seed, 4, 2.0f, 0.5f) * 0.4f;
}

ECS::Entity spawnCube(ECS::World& world, const char* name, f32 x, f32 y, f32 z,
                      f32 sx, f32 sy, f32 sz, u32 directorId, i32 cx, i32 cz) {
    ECS::Entity e = world.create(name);
    ECS::Transform& t = e.getOrAdd<ECS::Transform>();
    t.position = Vec3(x, y, z);
    t.scale = Vec3(sx, sy, sz);

    ECS::MeshFilterComponent filter;
    filter.primitive = ECS::MeshPrimitive::Cube;
    e.add<ECS::MeshFilterComponent>(filter);

    ECS::MeshRendererComponent renderer;
    e.add<ECS::MeshRendererComponent>(renderer);

    ECS::ProceduralSpawnTag tag;
    tag.directorEntityId = directorId;
    tag.chunkX = cx;
    tag.chunkZ = cz;
    e.add<ECS::ProceduralSpawnTag>(tag);
    return e;
}

void spawnExplorationProps(ECS::World& world, u32 directorId, i32 cx, i32 cz, f32 chunkSize,
                           u32 seed) {
    const f32 baseX = static_cast<f32>(cx) * chunkSize;
    const f32 baseZ = static_cast<f32>(cz) * chunkSize;
    const int count = 4 + static_cast<int>(hash01(cx, cz, seed, 7) * 6.0f);
    for (int i = 0; i < count; ++i) {
        const f32 rx = hash01(cx + i, cz, seed, 11 + i);
        const f32 rz = hash01(cx, cz + i, seed, 23 + i);
        if (rx < 0.25f) continue;
        const f32 x = baseX + rx * chunkSize;
        const f32 z = baseZ + rz * chunkSize;
        const f32 h = 2.0f + hash01(i, cx, seed, 31) * 5.0f;
        spawnCube(world, "ExplorationMarker", x, h * 0.5f, z, 1.2f, h, 1.2f, directorId, cx, cz);
    }
}

void spawnBackroomsChunk(ECS::World& world, u32 directorId, i32 cx, i32 cz, f32 chunkSize,
                         u32 seed) {
    const f32 baseX = static_cast<f32>(cx) * chunkSize;
    const f32 baseZ = static_cast<f32>(cz) * chunkSize;
    const f32 roomW = 12.0f;
    const f32 wallH = 3.0f;
    const f32 floorY = 0.05f;

    spawnCube(world, "BackroomsFloor", baseX + chunkSize * 0.5f, floorY, baseZ + chunkSize * 0.5f,
              chunkSize, 0.1f, chunkSize, directorId, cx, cz);

    const int roomsX = std::max(1, static_cast<int>(chunkSize / roomW));
    const int roomsZ = std::max(1, static_cast<int>(chunkSize / roomW));
    for (int rz = 0; rz < roomsZ; ++rz) {
        for (int rx = 0; rx < roomsX; ++rx) {
            const f32 cx0 = baseX + static_cast<f32>(rx) * roomW;
            const f32 cz0 = baseZ + static_cast<f32>(rz) * roomW;
            const f32 roll = hash01(cx + rx, cz + rz, seed, 101);
            if (roll < 0.12f) continue;

            spawnCube(world, "BackroomsWallN", cx0 + roomW * 0.5f, wallH * 0.5f, cz0 + 0.1f,
                      roomW, wallH, 0.2f, directorId, cx, cz);
            spawnCube(world, "BackroomsWallS", cx0 + roomW * 0.5f, wallH * 0.5f,
                      cz0 + roomW - 0.1f, roomW, wallH, 0.2f, directorId, cx, cz);
            spawnCube(world, "BackroomsWallW", cx0 + 0.1f, wallH * 0.5f, cz0 + roomW * 0.5f, 0.2f,
                      wallH, roomW, directorId, cx, cz);
            spawnCube(world, "BackroomsWallE", cx0 + roomW - 0.1f, wallH * 0.5f,
                      cz0 + roomW * 0.5f, 0.2f, wallH, roomW, directorId, cx, cz);

            if (hash01(rx, rz, seed, 202) > 0.7f) {
                spawnCube(world, "BackroomsPillar", cx0 + roomW * 0.5f, wallH * 0.5f,
                          cz0 + roomW * 0.5f, 0.8f, wallH, 0.8f, directorId, cx, cz);
            }
        }
    }
}

void spawnEndlessRaceChunk(ECS::World& world, u32 directorId, i32 cx, i32 cz, f32 chunkSize,
                           u32 seed) {
    const f32 baseX = static_cast<f32>(cx) * chunkSize;
    const f32 baseZ = static_cast<f32>(cz) * chunkSize;
    const f32 trackW = chunkSize * 0.45f;
    const f32 centerX = baseX + chunkSize * 0.5f;

    spawnCube(world, "RaceTrack", centerX, 0.05f, baseZ + chunkSize * 0.5f, trackW, 0.1f, chunkSize,
              directorId, cx, cz);

    const f32 wallOffset = trackW * 0.5f + 0.5f;
    const f32 curve = (hash01(cx, cz, seed, 303) - 0.5f) * chunkSize * 0.15f;
    spawnCube(world, "RaceWallL", centerX - wallOffset + curve, 1.5f, baseZ + chunkSize * 0.5f,
              0.4f, 3.0f, chunkSize, directorId, cx, cz);
    spawnCube(world, "RaceWallR", centerX + wallOffset + curve, 1.5f, baseZ + chunkSize * 0.5f,
              0.4f, 3.0f, chunkSize, directorId, cx, cz);

    if (hash01(cx, cz, seed, 404) > 0.65f) {
        spawnCube(world, "RaceObstacle", centerX + curve, 0.75f,
                  baseZ + chunkSize * (0.3f + hash01(cx, cz, seed, 505) * 0.4f), 2.0f, 1.5f, 2.0f,
                  directorId, cx, cz);
    }
}

}  // namespace

void ProceduralGenerator::generateTerrainChunk(ECS::World& world, ECS::Entity terrainEntity,
                                               i32 chunkX, i32 chunkZ,
                                               const ECS::ProceduralWorldComponent& settings) {
    auto* terrain = world.get<ECS::TerrainComponent>(terrainEntity);
    if (!terrain) return;

    Terrain::TerrainHeightmap* hm = Terrain::TerrainCache::instance().heightmapFor(terrainEntity);
    if (!hm || hm->empty()) {
        Terrain::TerrainCache::instance().initializeEntity(world, terrainEntity);
        hm = Terrain::TerrainCache::instance().heightmapFor(terrainEntity);
    }
    if (!hm) return;

    const f32 chunkSize = std::max(settings.chunkSize, 8.0f);
    const f32 halfX = terrain->worldSizeX * 0.5f;
    const f32 halfZ = terrain->worldSizeZ * 0.5f;
    const u32 resX = hm->resolutionX();
    const u32 resZ = hm->resolutionZ();
    if (resX < 2 || resZ < 2) return;

    const f32 baseX = static_cast<f32>(chunkX) * chunkSize;
    const f32 baseZ = static_cast<f32>(chunkZ) * chunkSize;

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(resX - 1);
            const f32 v = static_cast<f32>(z) / static_cast<f32>(resZ - 1);
            const f32 worldX = -halfX + u * terrain->worldSizeX;
            const f32 worldZ = -halfZ + v * terrain->worldSizeZ;

            if (worldX < baseX || worldX >= baseX + chunkSize || worldZ < baseZ ||
                worldZ >= baseZ + chunkSize) {
                continue;
            }

            const f32 h =
                heightForTerrainProfile(effectiveTerrainProfile(settings), worldX, worldZ, settings.seed);
            hm->setNormalized(x, z, std::clamp(h, 0.0f, 1.0f));
        }
    }

    terrain->dataRevision++;
    Terrain::TerrainCache::instance().syncEntity(world, terrainEntity);
}

std::vector<u32> ProceduralGenerator::generateStructureChunk(ECS::World& world,
                                                               ECS::Entity directorEntity,
                                                               i32 chunkX, i32 chunkZ,
                                                               const ECS::ProceduralWorldComponent& settings) {
    std::vector<u32> spawned;
    if (!directorEntity.isValid()) return spawned;

    const f32 chunkSize = std::max(settings.chunkSize, 8.0f);
    const u32 directorId = directorEntity.id();

    const char* structure = effectiveStructureProfile(settings);
    if (profileIs(structure, "rooms")) {
        spawnBackroomsChunk(world, directorId, chunkX, chunkZ, chunkSize, settings.seed);
    } else if (profileIs(structure, "track")) {
        spawnEndlessRaceChunk(world, directorId, chunkX, chunkZ, chunkSize, settings.seed);
    } else if (profileIs(structure, "markers")) {
        spawnExplorationProps(world, directorId, chunkX, chunkZ, chunkSize, settings.seed);
    }

    ECS::ComponentQuery q;
    q.with<ECS::ProceduralSpawnTag>();
    world.forEach<ECS::ProceduralSpawnTag>(q, [&](ECS::Entity e, ECS::ProceduralSpawnTag& tag) {
        if (tag.directorEntityId == directorId && tag.chunkX == chunkX && tag.chunkZ == chunkZ) {
            spawned.push_back(e.id());
        }
    });
    return spawned;
}

}  // namespace Caffeine::Procedural
