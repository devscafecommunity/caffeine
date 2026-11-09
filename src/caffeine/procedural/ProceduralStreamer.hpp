#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "ecs/World.hpp"

#include <unordered_map>
#include <vector>

namespace Caffeine::Procedural {

class ProceduralStreamer {
public:
    static ProceduralStreamer& instance();

    void reset();
    void streamAround(ECS::World& world, ECS::Entity directorEntity,
                      ECS::ProceduralWorldComponent& settings, i32 focusChunkX, i32 focusChunkZ);

    bool isChunkLoaded(u32 directorId, i32 cx, i32 cz) const;
    void unloadChunk(ECS::World& world, u32 directorId, i32 cx, i32 cz);

    static i32 worldToChunk(f32 worldCoord, f32 chunkSize);
    static void focusChunkFromEntity(ECS::World& world, const ECS::ProceduralWorldComponent& settings,
                                     i32& outCx, i32& outCz);

    u32 findTerrainEntity(ECS::World& world) const;

private:
    struct LoadedChunk {
        i32 cx = 0;
        i32 cz = 0;
        std::vector<u32> entityIds;
    };

    u64 chunkKey(i32 cx, i32 cz) const;
    void loadChunk(ECS::World& world, ECS::Entity directorEntity,
                   ECS::ProceduralWorldComponent& settings, i32 cx, i32 cz);

    std::unordered_map<u32, std::unordered_map<u64, LoadedChunk>> m_directorChunks;
};

}  // namespace Caffeine::Procedural
