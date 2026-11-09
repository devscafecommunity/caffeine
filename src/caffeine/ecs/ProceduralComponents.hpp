#pragma once

#include "core/Types.hpp"
#include <cstring>

namespace Caffeine::ECS {

/// Marks entities spawned by procedural streaming (removed on chunk unload).
struct ProceduralSpawnTag {
    u32 directorEntityId = 0;
    i32 chunkX = 0;
    i32 chunkZ = 0;
};

/// Drives infinite / streaming procedural worlds from Lua scripts.
struct ProceduralWorldComponent {
    u32 seed = 1337;
    f32 chunkSize = 64.0f;
    u32 viewRadius = 4;
    u32 terrainEntityId = 0;
    u32 focusEntityId = 0;

    /// Built-in terrain height: flat, hills, fbm
    char terrainProfile[64] = "hills";

    /// Built-in chunk structures: none, markers, rooms, track
    char structureProfile[64] = "none";

    /// Legacy combined preset (exploration, backrooms, …) — prefer terrain/structure profiles
    char preset[64] = {};

    /// Lua director script (streaming logic in play mode).
    char customScriptPath[256] = "scripts/procedural/custom_director.lua";

    bool enabled = true;
};

}  // namespace Caffeine::ECS
