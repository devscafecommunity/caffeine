-- Exploration: terrain grows with the player; props scatter per chunk.
-- Attach to ProceduralDirector entity with ProceduralWorldComponent.

local state = {}

function onCreate(directorId)
    state.id = directorId
    state.terrain = caffeine.procedural.findTerrain()
    state.chunkSize = 64
    state.preset = "exploration"
end

function onUpdate(directorId, dt)
    local focus = caffeine.procedural.getFocusChunk(state.id, state.chunkSize)
    caffeine.procedural.stream(state.id, state.terrain, focus.cx, focus.cz, state.preset)
end
