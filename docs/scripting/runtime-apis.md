# Lua runtime APIs (core)

> **VM:** `Script::ScriptEngine` (sol2)  
> **Systems:** `ScriptSystem` (play mode)

Bindings are registered in `ScriptEngine::init()`. Doppio does not duplicate these APIs — editor plugins and game scripts share the same global `caffeine` table.

---

## Lifecycle (`ScriptComponent`)

| Function | When |
|----------|------|
| `onCreate(entityId)` | First frame after load |
| `onUpdate(entityId, dt)` | Each play-mode frame (scaled; see `caffeine.time`) |
| `onFixedUpdate(entityId, dt)` | Fixed simulation step (default 60 Hz) |
| `onDestroy(entityId)` | Entity destroyed |
| `onCollision(entityId, otherId)` | 2D physics collision |

`loadScript(path)` resolves paths from the project search root (`setSearchRoot`).  
`require("mod.name")` loads `scripts/mod/name.lua`.

---

## Time (`caffeine.time`)

`delta`, `unscaledDelta`, `time`, `timeScale`, `setTimeScale`, `paused`, `setPaused`, `fixedDelta`, `frame`, `seed`, `setSeed`.

---

## Picking (`caffeine.pick`)

| API | Description |
|-----|-------------|
| `world(x, y)` | Entity id at world position (2D AABB) |
| `screen(x, y)` | Entity id from screen pixels |
| `atMouse()` | Entity id under cursor |
| `screenToWorld(x, y)` | `{ x, y }` table |
| `raycast(origin, dir)` | Closest entity id along 3D ray |
| `register(fn)` | Custom picker; `fn(wx, wy)` returns custom id or false |
| `unregister(handle)` | Remove custom picker |
| `query(wx, wy)` | `{ kind = "entity"\|"custom"\|"none", ... }` |

Play mode sets `PlayRenderContext` from the active 2D camera / viewport.

---

## 2D drawing (`caffeine.draw2d`)

Record in `onUpdate` (buffer cleared each frame). Draw order: world → overlays → UI.

`line`, `circle`, `rect`, `poly` — color `{r,g,b,a}`, space `"world"` or `"screen"`.

GPU path without ImGui: `Render::Overlay2D::buildRhiMesh` / `drawRhi` (see [overlay-2d-rhi.md](../rendering/overlay-2d-rhi.md)).

---

## Runtime UI (`caffeine.ui`)

`setLabel`, `setProgress`, `createCanvas`, `createLabel`, `createButton`, `hitTest`.

---

## Text assets (`caffeine.assets`)

`loadText(path)` — reads project-relative files with cache.

---

## Game state helpers (`caffeine.gameState`)

`push(id)`, `pop()`, `top()`, `pause()`, `resume()` — optional, domain-agnostic.

---

## Map surfaces (`caffeine.map`)

Generic logical map layers (`MapSurface2DComponent`), not tied to real-world geo or HTTP.

```lua
caffeine.map.onChunkRequest(function(payload)
  -- payload.surfaceEntityId, chunkX, chunkY, worldMinX, worldMinY, chunkSize
end)
```

Enable `enableChunkRequests` on the component in the editor. The engine emits `mapChunkRequest` when the camera view intersects new chunks.

---

## Native modules (`caffeine.native`)

Play-mode libraries in `native/*.so` (see [native-modules.md](native-modules.md)).

`load(name)`, `call(module, function, argsJson?)` → result string or error.

---

## Post-processing (`caffeine.postprocess`)

See [post-processing.md](../rendering/post-processing.md).

---

## Forward render (`caffeine.forwardrender`)

See [forward-render-features.md](../rendering/forward-render-features.md).

---

## Materials

Runtime materials use `MaterialCache` and mesh components; `.mat` editing is in the editor. See [materials-pbr.md](../rendering/materials-pbr.md).
