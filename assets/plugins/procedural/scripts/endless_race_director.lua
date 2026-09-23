-- Endless racer: track strip + walls spawn ahead along +Z chunks.

local state = {}

function onCreate(directorId)
    state.id = directorId
    state.terrain = caffeine.procedural.findTerrain()
    state.chunkSize = 80
    state.preset = "endless_race"
end

function onUpdate(directorId, dt)
    local focus = caffeine.procedural.getFocusChunk(state.id, state.chunkSize)
    caffeine.procedural.stream(state.id, state.terrain, focus.cx, focus.cz, state.preset)
end
