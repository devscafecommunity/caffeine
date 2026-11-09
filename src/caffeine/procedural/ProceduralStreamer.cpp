#include "procedural/ProceduralStreamer.hpp"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/TerrainComponents.hpp"
#include "procedural/ProceduralGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace Caffeine::Procedural {

ProceduralStreamer& ProceduralStreamer::instance() {
    static ProceduralStreamer streamer;
    return streamer;
}

void ProceduralStreamer::reset() {
    m_directorChunks.clear();
}

u64 ProceduralStreamer::chunkKey(i32 cx, i32 cz) const {
    return (static_cast<u64>(static_cast<u32>(cx)) << 32) |
           static_cast<u64>(static_cast<u32>(cz));
}

i32 ProceduralStreamer::worldToChunk(f32 worldCoord, f32 chunkSize) {
    chunkSize = std::max(chunkSize, 1.0f);
    return static_cast<i32>(std::floor(worldCoord / chunkSize));
}

bool ProceduralStreamer::isChunkLoaded(u32 directorId, i32 cx, i32 cz) const {
    const auto it = m_directorChunks.find(directorId);
    if (it == m_directorChunks.end()) return false;
    return it->second.find(chunkKey(cx, cz)) != it->second.end();
}

u32 ProceduralStreamer::findTerrainEntity(ECS::World& world) const {
    ECS::ComponentQuery q;
    q.with<ECS::TerrainComponent>();
    u32 found = 0;
    world.forEach<ECS::TerrainComponent>(q, [&](ECS::Entity e, ECS::TerrainComponent&) {
        if (found == 0) found = e.id();
    });
    return found;
}

void ProceduralStreamer::focusChunkFromEntity(ECS::World& world,
                                              const ECS::ProceduralWorldComponent& settings,
                                              i32& outCx, i32& outCz) {
    outCx = 0;
    outCz = 0;
    const f32 chunkSize = std::max(settings.chunkSize, 8.0f);

    ECS::Entity focus;
    if (settings.focusEntityId != 0) {
        focus = ECS::Entity(settings.focusEntityId, &world);
    } else {
        ECS::ComponentQuery camQ;
        camQ.with<ECS::Camera3DComponent>();
        world.forEach<ECS::Camera3DComponent>(camQ, [&](ECS::Entity e, ECS::Camera3DComponent&) {
            if (!focus.isValid()) focus = e;
        });
        if (!focus.isValid()) {
            ECS::ComponentQuery cam2Q;
            cam2Q.with<ECS::Camera2DComponent>();
            world.forEach<ECS::Camera2DComponent>(cam2Q, [&](ECS::Entity e, ECS::Camera2DComponent&) {
                if (!focus.isValid()) focus = e;
            });
        }
    }

    f32 x = 0.0f;
    f32 z = 0.0f;
    if (focus.isValid()) {
        if (auto* p3 = focus.get<ECS::Position3D>()) {
            x = p3->position.x;
            z = p3->position.z;
        } else if (auto* t = focus.get<ECS::Transform>()) {
            x = t->position.x;
            z = t->position.z;
        }
    }

    outCx = worldToChunk(x, chunkSize);
    outCz = worldToChunk(z, chunkSize);
}

void ProceduralStreamer::unloadChunk(ECS::World& world, u32 directorId, i32 cx, i32 cz) {
    auto dirIt = m_directorChunks.find(directorId);
    if (dirIt == m_directorChunks.end()) return;

    const u64 key = chunkKey(cx, cz);
    auto chunkIt = dirIt->second.find(key);
    if (chunkIt == dirIt->second.end()) return;

    for (u32 entityId : chunkIt->second.entityIds) {
        ECS::Entity e(entityId, &world);
        if (e.isValid()) {
            world.destroy(e);
        }
    }

    ECS::ComponentQuery q;
    q.with<ECS::ProceduralSpawnTag>();
    world.forEach<ECS::ProceduralSpawnTag>(q, [&](ECS::Entity e, ECS::ProceduralSpawnTag& tag) {
        if (tag.directorEntityId == directorId && tag.chunkX == cx && tag.chunkZ == cz) {
            world.destroy(e);
        }
    });

    dirIt->second.erase(chunkIt);
}

void ProceduralStreamer::loadChunk(ECS::World& world, ECS::Entity directorEntity,
                                   ECS::ProceduralWorldComponent& settings, i32 cx, i32 cz) {
    if (!settings.enabled || !directorEntity.isValid()) return;
    if (isChunkLoaded(directorEntity.id(), cx, cz)) return;

    u32 terrainId = settings.terrainEntityId;
    if (terrainId == 0) {
        terrainId = findTerrainEntity(world);
        settings.terrainEntityId = terrainId;
    }

    if (terrainId != 0) {
        ECS::Entity terrain(terrainId, &world);
        if (terrain.isValid()) {
            ProceduralGenerator::generateTerrainChunk(world, terrain, cx, cz, settings);
        }
    }

    LoadedChunk chunk;
    chunk.cx = cx;
    chunk.cz = cz;
    chunk.entityIds =
        ProceduralGenerator::generateStructureChunk(world, directorEntity, cx, cz, settings);

    m_directorChunks[directorEntity.id()][chunkKey(cx, cz)] = std::move(chunk);
}

void ProceduralStreamer::streamAround(ECS::World& world, ECS::Entity directorEntity,
                                      ECS::ProceduralWorldComponent& settings, i32 focusChunkX,
                                      i32 focusChunkZ) {
    if (!settings.enabled || !directorEntity.isValid()) return;

    const u32 radius = std::max(1u, settings.viewRadius);
    const u32 directorId = directorEntity.id();

    std::vector<std::pair<i32, i32>> needed;
    needed.reserve(static_cast<size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (i32 dz = -static_cast<i32>(radius); dz <= static_cast<i32>(radius); ++dz) {
        for (i32 dx = -static_cast<i32>(radius); dx <= static_cast<i32>(radius); ++dx) {
            needed.emplace_back(focusChunkX + dx, focusChunkZ + dz);
        }
    }

    std::vector<std::pair<i32, i32>> toUnload;
    auto& loaded = m_directorChunks[directorId];
    for (const auto& [key, chunk] : loaded) {
        bool keep = false;
        for (const auto& n : needed) {
            if (n.first == chunk.cx && n.second == chunk.cz) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            toUnload.emplace_back(chunk.cx, chunk.cz);
        }
    }

    for (const auto& [cx, cz] : toUnload) {
        unloadChunk(world, directorId, cx, cz);
    }

    for (const auto& [cx, cz] : needed) {
        loadChunk(world, directorEntity, settings, cx, cz);
    }
}

}  // namespace Caffeine::Procedural
