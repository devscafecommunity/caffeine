-- Template: override generation per chunk with Lua logic.
-- Duplicate this file and implement terrain() / structures().

local M = {
    chunkSize = 64,
    terrainProfile = "hills",
}

function M.terrain(cx, cz, api)
    -- Example: raise center of each chunk
    local h = caffeine.procedural.fbm2d(cx * 3.1, cz * 3.1, api.seed, 4, 2.0, 0.5)
    caffeine.procedural.generateTerrainChunk(api.terrainId, cx, cz, api.seed, M.terrainProfile)
end

function M.structures(cx, cz, api)
    local baseX = cx * M.chunkSize + M.chunkSize * 0.5
    local baseZ = cz * M.chunkSize + M.chunkSize * 0.5
    if caffeine.procedural.hash(cx, cz, api.seed, 99) > 0.6 then
        caffeine.procedural.spawnCube(api.directorId, cx, cz, baseX, 2, baseZ, 2, 4, 2)
    end
end

local state = { module = M }

function onCreate(directorId)
    state.id = directorId
    state.terrain = caffeine.procedural.findTerrain()
end

function onUpdate(directorId, dt)
    local focus = caffeine.procedural.getFocusChunk(state.id, state.module.chunkSize)
    caffeine.procedural.stream(state.id, state.terrain, focus.cx, focus.cz)
end
