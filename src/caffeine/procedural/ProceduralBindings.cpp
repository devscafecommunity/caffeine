#include "procedural/ProceduralGenerator.hpp"
#include "procedural/ProceduralNoise.hpp"
#include "procedural/ProceduralStreamer.hpp"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainCache.hpp"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Caffeine::Script {
namespace {

void registerProceduralBindings(sol::state& lua, ECS::World** worldPtr) {
    lua["caffeine"]["procedural"] = lua.create_table();
    sol::table pt = lua["caffeine"]["procedural"];

    pt["hash"] = [](i32 x, i32 z, u32 seed, u32 salt) {
        return Procedural::hash01(x, z, seed, salt);
    };

    pt["noise2d"] = [](f32 x, f32 z, u32 seed) {
        return Procedural::valueNoise2D(x, z, seed);
    };

    pt["fbm2d"] = [](f32 x, f32 z, u32 seed, u32 octaves, f32 lacunarity, f32 persistence) {
        return Procedural::fbm2D(x, z, seed, octaves, lacunarity, persistence);
    };

    pt["worldToChunk"] = [](f32 worldCoord, f32 chunkSize) {
        return Procedural::ProceduralStreamer::worldToChunk(worldCoord, chunkSize);
    };

    pt["findTerrain"] = [worldPtr]() -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return 0;
        return Procedural::ProceduralStreamer::instance().findTerrainEntity(*world);
    };

    pt["getFocusChunk"] = [&lua, worldPtr](u32 directorId, f32 chunkSize, sol::optional<u32> focusId)
        -> sol::table {
        sol::table t = lua.create_table();
        t["cx"] = 0;
        t["cz"] = 0;
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return t;

        ECS::ProceduralWorldComponent settings;
        settings.chunkSize = chunkSize;
        if (focusId) settings.focusEntityId = *focusId;

        ECS::Entity director(directorId, world);
        if (director.isValid()) {
            if (auto* proc = director.get<ECS::ProceduralWorldComponent>()) {
                settings = *proc;
            }
        }

        i32 cx = 0;
        i32 cz = 0;
        Procedural::ProceduralStreamer::focusChunkFromEntity(*world, settings, cx, cz);
        t["cx"] = cx;
        t["cz"] = cz;
        return t;
    };

    pt["isChunkLoaded"] = [](u32 directorId, i32 cx, i32 cz) {
        return Procedural::ProceduralStreamer::instance().isChunkLoaded(directorId, cx, cz);
    };

    pt["unloadChunk"] = [worldPtr](u32 directorId, i32 cx, i32 cz) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return;
        Procedural::ProceduralStreamer::instance().unloadChunk(*world, directorId, cx, cz);
    };

    pt["stream"] = [worldPtr](u32 directorId, sol::optional<u32> terrainId, i32 cx, i32 cz,
                              sol::optional<std::string> preset) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return;

        ECS::Entity director(directorId, world);
        if (!director.isValid()) return;

        auto& settings = director.getOrAdd<ECS::ProceduralWorldComponent>();
        if (terrainId) settings.terrainEntityId = *terrainId;
        if (preset) {
            std::strncpy(settings.preset, preset->c_str(), sizeof(settings.preset) - 1);
            settings.preset[sizeof(settings.preset) - 1] = '\0';
        }

        Procedural::ProceduralStreamer::instance().streamAround(*world, director, settings, cx, cz);
    };

    pt["generateTerrainChunk"] = [worldPtr](u32 terrainId, i32 cx, i32 cz, u32 seed,
                                            sol::optional<std::string> preset) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return;

        ECS::Entity terrain(terrainId, world);
        if (!terrain.isValid()) return;

        ECS::ProceduralWorldComponent settings;
        settings.seed = seed;
        if (preset) {
            std::strncpy(settings.preset, preset->c_str(), sizeof(settings.preset) - 1);
        }
        settings.chunkSize = 64.0f;
        if (auto* tc = terrain.get<ECS::TerrainComponent>()) {
            settings.chunkSize = std::max(tc->worldSizeX / 8.0f, 32.0f);
        }

        Procedural::ProceduralGenerator::generateTerrainChunk(*world, terrain, cx, cz, settings);
    };

    pt["spawnCube"] = [worldPtr](u32 directorId, i32 cx, i32 cz, f32 x, f32 y, f32 z, f32 sx,
                                  f32 sy, f32 sz) -> u32 {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return 0;

        ECS::Entity e = world->create("ProcCube");
        ECS::Transform& t = e.getOrAdd<ECS::Transform>();
        t.position = Vec3(x, y, z);
        t.scale = Vec3(sx, sy, sz);

        ECS::MeshFilterComponent filter;
        filter.primitive = ECS::MeshPrimitive::Cube;
        e.add<ECS::MeshFilterComponent>(filter);
        e.add<ECS::MeshRendererComponent>();

        ECS::ProceduralSpawnTag tag;
        tag.directorEntityId = directorId;
        tag.chunkX = cx;
        tag.chunkZ = cz;
        e.add<ECS::ProceduralSpawnTag>(tag);
        return e.id();
    };

    pt["fillTerrainNoise"] = [worldPtr](u32 terrainId, u32 seed, f32 amplitude) {
        ECS::World* world = worldPtr ? *worldPtr : nullptr;
        if (!world) return;
        ECS::Entity terrain(terrainId, world);
        if (!terrain.isValid()) return;

        Terrain::TerrainHeightmap* hm = Terrain::TerrainCache::instance().heightmapFor(terrain);
        if (!hm) {
            Terrain::TerrainCache::instance().initializeEntity(*world, terrain);
            hm = Terrain::TerrainCache::instance().heightmapFor(terrain);
        }
        if (!hm) return;

        auto* tc = terrain.get<ECS::TerrainComponent>();
        if (!tc) return;

        const f32 halfX = tc->worldSizeX * 0.5f;
        const f32 halfZ = tc->worldSizeZ * 0.5f;
        const u32 resX = hm->resolutionX();
        const u32 resZ = hm->resolutionZ();

        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                const f32 u = static_cast<f32>(x) / static_cast<f32>(std::max(1u, resX - 1));
                const f32 v = static_cast<f32>(z) / static_cast<f32>(std::max(1u, resZ - 1));
                const f32 wx = -halfX + u * tc->worldSizeX;
                const f32 wz = -halfZ + v * tc->worldSizeZ;
                const f32 h = Procedural::fbm2D(wx * 0.02f, wz * 0.02f, seed, 5, 2.0f, 0.45f);
                hm->setNormalized(x, z, std::clamp(h * amplitude, 0.0f, 1.0f));
            }
        }

        tc->dataRevision++;
        Terrain::TerrainCache::instance().syncEntity(*world, terrain);
    };
}

}  // namespace

void registerProceduralScriptBindings(sol::state& lua, ECS::World** worldPtr);

void registerProceduralScriptBindings(sol::state& lua, ECS::World** worldPtr) {
    registerProceduralBindings(lua, worldPtr);
}

}  // namespace Caffeine::Script
