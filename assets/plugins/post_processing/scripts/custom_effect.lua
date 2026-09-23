-- Custom post-process driver (template).
-- Attach path on PostProcessComponent.customEffectScript, or set via:
--   caffeine.postprocess.setCustomScript(cameraId, "scripts/postprocessing/custom_effect.lua")

local state = { cameraId = 0, pulse = 0.0 }

function onCreate(entityId)
    state.cameraId = entityId
end

function onUpdate(entityId, dt)
    state.pulse = state.pulse + dt
    local bloom = 0.1 + math.sin(state.pulse * 0.5) * 0.05
    caffeine.postprocess.setBloom(state.cameraId, bloom)
end
