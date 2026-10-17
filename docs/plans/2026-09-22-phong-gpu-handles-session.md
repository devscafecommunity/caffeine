# Sessão 2026-09-22 — Phong GPU, Handles e Sombras

> **Branch:** `feature/editor-plugin-sdk`  
> **Objetivo:** avançar pilares P0 (rendering Phong + handles) e tarefas P1 (sombras pontuais GPU, normal maps no terreno).  
> **Build verificado:** `caffeine-core`, `caffeine-runtime`, `doppio` compilam com sucesso.

---

## Resumo executivo

| Prioridade | Item | Estado |
|------------|------|--------|
| P0 | Remover fallback CPU shadows no editor (`SceneViewport`) | ✅ |
| P0 | Inspector UI para `customNormalPath` e `shininess` (mesh + terreno) | ✅ |
| P1 | Point light shadows GPU | ✅ |
| P1 | Terreno com normal maps em `terrain_lit.frag` | ✅ |
| P0 | Asset handles com generation + LRU + invalidation callbacks | ✅ |

---

## 1. Asset handles (Pilar 6)

### O que mudou

- `AssetHandle<T>` passou a guardar `{ id, generation }`. `get()` devolve `nullptr` se a geração não coincidir.
- `LoadStatus::Invalid` quando o slot foi invalidado (LRU ou GC).
- **Hot-reload** (`clearEntryData`): recarrega payload **sem** incrementar geração — handles activos continuam válidos.
- **Invalidação** (`invalidateEntry`): incrementa geração, limpa payload, notifica callbacks.
- **LRU eviction**: quando `m_cachedBytes > m_maxCacheBytes`, remove entradas não referenciadas pela ordem `lastAccessFrame`.
- `registerInvalidationCallback` / `unregisterInvalidationCallback` para libertar recursos GPU quando um asset é evicted.
- `CacheStats::evictedCount` para telemetria.

### Ficheiros

- `src/assets/AssetHandle.hpp`, `AssetTypes.hpp`, `AssetManager.{hpp,cpp}`
- `tests/test_assetmanager.cpp` — testes de geração, LRU, callbacks

### Documentação

- [`assets/asset-manager.md`](../assets/asset-manager.md)

---

## 2. Rendering Phong + sombras GPU (Pilar 1)

### Meshes (`scene_lit.*`)

- Shader Phong: ambient + diffuse + specular com `shininess`.
- Normal mapping via TBN (`scene_lit.vert` exporta tangent/bitangent).
- Tangentes calculadas em `MeshLoader::computeMeshTangents()` para OBJ e glTF.
- `MeshFilterComponent`: `customNormalPath`, `shininess` (default 32).
- Sombras direcionais: depth pass + PCF 3×3 no fragment shader.
- Sombras pontuais: até 2 luzes, cubemap depth (`GpuPointShadowMap`), sample no `scene_lit.frag`.

### Terreno (`terrain_lit.frag`)

- Mesmo modelo Phong que meshes.
- Normal map triplanar via `uNormalMap` (sampler slot 5).
- `TerrainComponent`: `normalMapPath`, `shininess` (default 16).
- `TerrainGpuTextureCache` carrega e invalida `normalMap` em sync com `texturePath`.
- Sombras: direcionais (slots 6–7) + pontuais (slots 8–9).
- `castShadows` / `receiveShadows` respeitados no gather GPU.

### Editor viewport

- Com GPU 3D texturizado activo, `gatherSceneLighting(..., buildCpuShadowMaps = false)`.
- `drawSceneMeshesForCamera` usa `collectSceneLights` apenas — sem CPU shadow maps.
- CPU raster permanece para wireframe/debug e entidades vazias quando GPU não está activo.

### Inspector + serialização

| Componente | Campos novos | Blob |
|------------|--------------|------|
| `MeshFilterComponent` | Normal Map, Shininess | append após `customMaterialPath` |
| `TerrainComponent` | Normal Map, Shininess | blob versão **10** |

### Ficheiros principais

- `src/render/GpuSceneRenderer.cpp`, `GpuPointShadowMap.{hpp,cpp}`
- `src/render/shaders/scene_lit.{vert,frag}`, `terrain_lit.frag`
- `src/scene/LightingSystem.{hpp,cpp}`
- `src/editor/SceneViewport.cpp`, `InspectorPanel.cpp`, `SceneSerializer.cpp`
- `src/terrain/TerrainGpuTextures.cpp`, `src/ecs/TerrainComponents.hpp`

### Documentação

- [`rendering/materials-phong.md`](../rendering/materials-phong.md)
- [`rendering/shadow-mapping.md`](../rendering/shadow-mapping.md)
- [`terrain/terrain-system.md`](../terrain/terrain-system.md)
- [`assets/mesh-loading.md`](../assets/mesh-loading.md)

---

## 3. Continuação (mesma branch)

| Item | Estado |
|------|--------|
| Runtime GPU shadows | ✅ `RuntimeSceneRenderer` integra `GpuSceneRenderer` |
| CSM 4 cascatas (luz dir 0) | ✅ atlas 1024², splits práticos |
| Spot shadows GPU (2 luzes) | ✅ `GpuSpotShadowMap` + loop spot nos shaders |
| `test_scenemanager.cpp` | ✅ usa `Transform`/`Acceleration2D`; compila |
| Componentes ECS em falta nos testes | ✅ `Position2D`, `Velocity2D`, `Scale2D`, `Rotation`, `Health` |

## 4. Próximos passos sugeridos

1. **PCSS / cascade blending** — qualidade visual das sombras.
2. **PBR** — metallic/roughness sobre a base Phong.
3. **Suite completa** — outros testes (`test_cap_loader`, integração editor) ainda com erros pré-existentes.

---

## Links

- [Engine Pillars Roadmap](2026-09-22-engine-pillars-roadmap.md)
- [Rendering Roadmap P0–P6](2026-09-21-rendering-roadmap.md)
