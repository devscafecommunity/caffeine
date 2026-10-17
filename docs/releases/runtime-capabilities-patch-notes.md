# Runtime capabilities — patch notes

**Date:** 2026-10-14  
**Scope:** Caffeine engine core + scripting (Doppio consumes the same APIs)  
**RFC:** [planning/engine-runtime-capabilities-rfc.md](../../planning/engine-runtime-capabilities-rfc.md)

This document is intended for **all engine users** (internal teams and third-party projects). It summarizes what landed in two implementation phases and how to adopt it safely.

---

## Design principles (unchanged)

1. **Optional** — Projects that only use sprites + scripts pay no map/native/LOD cost unless they use those APIs.
2. **Neutral** — No real-world map providers, HTTP tile URLs, or game-specific names in core.
3. **Editor ≠ runtime** — Doppio plugins stay separate from play-mode native modules.
4. **Single Lua contract** — Documented in [runtime-apis.md](../scripting/runtime-apis.md).

---

## Phase 1 (foundation)

| Feature | Summary |
|---------|---------|
| **Picking** | `Scene::pickEntityAtWorld`, `raycastSelectEntity`, `PlayRenderContext`, `PickQueryRegistry` |
| **Overlays** | `Render::Overlay2D` + `caffeine.draw2d.*` |
| **UI** | Lua helpers on existing `UISystem` |
| **Scripts** | `require`, `caffeine.assets.loadText`, `PlayClock`, `GameStateStack` |
| **Map (data model)** | `MapSurface2DComponent` (bounds + chunk size only) |

**Tests:** `tests/test_pick_query.cpp` (`[pick]`).

---

## Phase 2 (this patch)

### 1. Native runtime modules

- **ABI:** `include/caffeine/runtime/NativeModuleAPI.h`
- **Host:** `Script::NativeModuleHost` (`dlopen` / `LoadLibrary`, `caffeine_native_module_info`, optional `caffeine_native_call`)
- **Lua:** `caffeine.native.load`, `caffeine.native.call(module, fn, json?)`
- **Docs:** [native-modules.md](../scripting/native-modules.md)

### 2. Overlay RHI path

- **API:** `Overlay2D::buildRhiMesh`, `Overlay2D::drawRhi` (SDL GPU / no ImGui entry point)
- **Shaders:** `overlay2d.vert` / `overlay2d.frag` in tree; wire into builtin shader registration to enable GPU draw
- **Docs:** [overlay-2d-rhi.md](../rendering/overlay-2d-rhi.md)

### 3. Map surface — editor + events

- **Component:** `MapSurface2DComponent` serialized in `.caf` (type id **55**)
- **Editor:** Add Component → **Map Surface 2D**
- **Runtime:** `Scene::tickMapSurfaceChunkRequests` emits `Events::OnMapSurfaceChunkRequest`
- **Lua:** `caffeine.map.onChunkRequest(fn)` or `caffeine.events.on("mapChunkRequest", fn)`
- **Note:** You supply all content (textures, meshes, data). The engine only reports which logical chunks are visible.

### 4. 2D sprite LOD

- **Component:** `SpriteLod2DComponent` (type id **56**), zoom thresholds + scale factor
- **System:** `Scene::applySpriteLod2D` driven by active play camera zoom
- **Editor:** Add Component → **Sprite LOD 2D** (captures current transform scale as base)

### 5. Custom Lua picking

- **API:** `caffeine.pick.register`, `unregister`, `query`
- Custom handlers run before entity AABB pick; return a numeric custom id or false

---

## Migration checklist for game teams

- [ ] Replace ad-hoc screen→world math with `caffeine.pick.screenToWorld` / `PlayRenderContext`
- [ ] Move vector debug draw to `caffeine.draw2d` instead of editor-only gizmos
- [ ] For large surfaces, add `MapSurface2D` + handle `mapChunkRequest` in Lua (or C++ via `EventBus`)
- [ ] For dense 2D scenes, add `SpriteLod2D` on marker entities
- [ ] For heavy sim outside Lua, add `native/my_module.so` using the documented ABI
- [ ] Read [runtime-apis.md](../scripting/runtime-apis.md) (English) before upgrading scripts

---

## Binary scene format

| Type ID | Component |
|--------:|-----------|
| 55 | `MapSurface2DComponent` |
| 56 | `SpriteLod2DComponent` |

Older `.caf` files load unchanged; new components are opt-in.

---

## Known follow-ups

- Register `overlay2d` shaders in `BuiltinShader` + CMake for full GPU overlay draw without ImGui
- Richer `caffeine.native.call` (typed Lua tables ↔ JSON)
- Inspector widgets for map bounds (today: component defaults + future inspector polish)
- Optional `SpatialGrid2D` helpers documented under [spatial-partitioning.md](../physics/spatial-partitioning.md) usage patterns

---

## Files touched (high level)

- `src/scene/PickQuery.*`, `PickRaycast.*`, `MapSurfaceSystem.*`, `SpriteLod2DSystem.*`
- `src/render/Overlay2D.*`, `src/render/shaders/overlay2d.*`
- `src/script/RuntimeScriptBindings.cpp`, `NativeModuleHost.*`, `AssetTextLoader.*`
- `src/runtime/PlayClock.*`, `GameStateStack.*`
- `src/ecs/MapComponents.hpp`, `LodComponents.hpp`
- `include/caffeine/runtime/NativeModuleAPI.h`
- `src/editor/SceneSerializer.*`, `ComponentRegistry.cpp`
- `docs/scripting/*`, `docs/rendering/overlay-2d-rhi.md`, this file

---

## Support

Questions or regressions: attach project `native/` layout, script snippets using new APIs, and whether play mode is 2D or 3D (affects `PlayRenderContext`).
