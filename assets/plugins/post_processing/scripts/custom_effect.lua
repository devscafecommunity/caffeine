-- Custom post-process driver (template).
-- Set PostProcessComponent.customEffectScript on a camera to this file, or from any script:
--   caffeine.postprocess.setCustomScript(cameraId, "scripts/postprocessing/custom_effect.lua")
--
-- The engine calls onCreate(entityId) once and onPostProcess(entityId, dt, api) every play-mode
-- frame. `api` is bound to the camera entity:
--   api.get("bloom.intensity")            -> value of any "effect.field"
--   api.set("bloom.intensity", 0.3)       -> clamped write
--   api.enable("dof", true)               -> toggle an effect (aliases: ao, aa, fog, dof, ssr, chromatic)
--   api.snapshot() / api.apply(table)     -> whole stack as a nested table
--   api.preset("cinematic")               -> cinematic | horror | arcade | reset
-- caffeine.postprocess.effects() and caffeine.postprocess.fields(effect) list everything available.

local time = 0.0

function onCreate(entityId)
    time = 0.0
end

function onPostProcess(entityId, dt, api)
    time = time + dt
    api.enable("bloom", true)
    api.set("bloom.intensity", 0.15 + math.sin(time * 0.5) * 0.05)
    api.set("vignette.intensity", 0.25 + math.sin(time * 2.0) * 0.03)
    api.enable("vignette", true)
end
