# Lua — APIs de runtime (core)

> **VM:** `Script::ScriptEngine` (sol2)  
> **Sistemas:** `ScriptSystem` (play mode)

Bindings registados em `ScriptEngine::registerBindings()`. O Doppio não duplica estas APIs — plugins e scripts de jogo usam o mesmo `caffeine` global.

---

## Ciclo de vida (`ScriptComponent`)

| Função | Quando |
|--------|--------|
| `onCreate(entityId)` | Primeiro frame após load |
| `onUpdate(entityId, dt)` | Cada frame em play mode |
| `onDestroy(entityId)` | Entidade destruída |
| `onCollision(entityId, otherId)` | Colisão 2D |

`loadScript(path)` resolve caminhos relativos à raiz do projeto (`setSearchRoot`).

---

## Pós-processamento (`caffeine.postprocess`)

Documentação completa: [`../rendering/post-processing.md`](../rendering/post-processing.md).

```lua
caffeine.postprocess.add(cameraId)
local fx = caffeine.postprocess.get(cameraId)           -- tabela aninhada por efeito
caffeine.postprocess.setValue(cameraId, "bloom.intensity", 0.2)
caffeine.postprocess.enableEffect(cameraId, "dof", true)  -- aliases: ao, aa, fog, ssr, chromatic
caffeine.postprocess.applyPreset(cameraId, "cinematic") -- cinematic | horror | arcade | reset
local api = caffeine.postprocess.handle(cameraId)
caffeine.postprocess.setCustomScript(cameraId, "scripts/my_stack.lua")
```

### `customEffectScript` (`PostProcessComponent`)

Em play mode, `ScriptSystem` carrega o ficheiro com chave `postprocess:<path>` e chama:

```lua
function onPostProcess(entityId, dt, api)
  api.set("bloom.intensity", 0.15)
end
```

Se não existir `onPostProcess`, usa `onUpdate(entityId, dt)`.

Campos disponíveis: `caffeine.postprocess.effects()` e `caffeine.postprocess.fields("bloom")` (ver `PostProcessFields.hpp`).

---

## Forward render (`caffeine.forwardrender`)

Ver [`../rendering/forward-render-features.md`](../rendering/forward-render-features.md).

```lua
caffeine.forwardrender.ensure(entityId)
caffeine.forwardrender.setReflections(entityId, true, 3) -- 0 off, 1 planar, 2 SSR, 3 probe
caffeine.forwardrender.setVolumetrics(entityId, true, 2)
caffeine.forwardrender.setOcclusion(entityId, true)
```

---

## Materiais

Materiais em runtime passam por `MaterialCache` e componentes de mesh; edição de `.mat` é no editor. Ver [`../rendering/materials-pbr.md`](../rendering/materials-pbr.md).
