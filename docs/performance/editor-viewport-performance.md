# Performance — Editor Viewport & GPU Scene

> **Data:** 2026-09-23  
> **Contexto:** Investigação de quedas de FPS no Doppio (~12 FPS, ~77 ms em `SceneEditor::render`)  
> **Status:** ✅ Otimizações aplicadas + instrumentação de profiler

---

## Resumo

O editor renderizava a mesma geometria em **CPU e GPU** no mesmo frame, executava **vários passes GPU** (viewport + previews) com sombras e binds redundantes, e rasterizava o **skybox em CPU** a resolução quase total do painel a cada micro-movimento de câmera.

As correções seguem duas regras:

1. **Meshes/terreno:** GPU **ou** CPU por frame — nunca os dois.
2. **Shadow passes** (redraw da cena) são independentes dos **shadow samplers** (texturas ligadas no shader).

---

## Resultado validado

| Métrica | Antes | Depois |
|---------|-------|--------|
| **FPS** | **~10** | **~100** |
| Frame time | ~77–213 ms | ~10 ms |

Ganho medido no Doppio com cena 3D + terreno chunked, após o pacote de correções abaixo.

---

## Sintomas observados (profiler, antes)

| Métrica | Valor | Interpretação |
|---------|-------|-----------------|
| Frame time | ~77–213 ms | CPU baixo (~5%), GPU ~30% → gargalo em draw calls / driver |
| `SceneEditor::render` | ~55 ms avg | Scope único engolia todos os painéis |
| `GpuSceneRenderer::draw` | ~54 ms avg | Pass principal da cena (terreno + meshes) |
| `SceneViewport::skybox` | 0.9 ms avg, **80 ms max** | Picos ao re-rasterizar equirect em CPU |
| `cameraPreview` / `gameplayPreview` | ~0 ms após throttle | Previews deixaram de competir com o viewport |

---

## Arquitetura: um caminho de render por frame

### Scene Viewport (3D)

| Camada | Responsável |
|--------|-------------|
| Meshes + terreno | **GPU** (`GpuSceneRenderer`) quando `m_useGpuScene` ativo |
| Skybox | CPU (`SkyboxRenderer`) como fundo; GPU limpa com alpha 0 |
| Grid, gizmos, frustums, marcadores | CPU (`ImDrawList`) — overlays de editor |

Quando `gpuOwnsSceneMeshes` está ativo, `drawEmptyEntities()` **não** rasteriza meshes na CPU (inclui terreno e seleção). Só marcadores de entidades vazias e debug de chunks.

### Camera / Gameplay Preview

- **GPU path:** `renderCameraPreviewGpu()` ou `GameplayPreviewPanel::m_renderer` — sem `drawSceneMeshesForCamera()` no mesmo frame.
- **CPU fallback:** só quando o pass GPU falha ou não está disponível (`#ifdef CF_HAS_SDL3`).
- Throttle: `editorPanelWorthGpuRender(..., frameInterval=2)` — re-render GPU a cada 2 frames; reutiliza textura anterior.

---

## Causa raiz em `GpuSceneRenderer::draw`

Para **cada chunk de terreno**, o loop fazia trabalho que deveria ser **uma vez por pass** ou **uma vez por entidade**:

| Operação | Problema |
|----------|----------|
| `pushUniformData(shadowUbo)` | Mesmo UBO repetido com sombras desligadas |
| `bindShadowTextures()` | 6+ samplers por chunk |
| `texCache.acquire()` | Lookup de splat/albedo/normal por chunk |

Com **50–200 chunks** visíveis → **milhares de operações redundantes por frame**.

---

## Correções aplicadas (10 → 100 FPS)

| # | Correção | Impacto |
|---|----------|---------|
| 1 | `shadowUbo` **uma vez por pass** (não por draw) | Menos uploads de uniform por frame |
| 2 | Shadow **passes** off no editor; **samplers** sempre ligados | Sem redraw × cascatas; sem SIGSEGV NVIDIA |
| 3 | **Cache de texturas de terreno** — bind só na 1ª vez por entidade | De O(chunks) → O(terrenos) binds de textura |
| 4 | `terrainLodDistanceScale = 2` no editor | Menos chunks visíveis / menos draws |
| 5 | Resolução GPU cap **1280 px** no viewport | Menos pixels no offscreen target |
| 6 | `textureQuality.enabled = false` no editor | Menos churn de mip/tier por draw |
| 7 | Skybox cap **512–1024 px** (sem throttle de frames) | Raster CPU mais barato sem desync céu/terreno |

Complementares (mesma sessão): separação GPU/CPU por frame, skip GPU em previews ocultos, profiler granular.

---

## Otimizações no `GpuSceneRenderer`

### 1. Shadow passes vs shadow samplers

| Conceito | Editor | Runtime / play |
|----------|--------|----------------|
| **Shadow passes** (`renderDirectionalShadows`, etc.) | ❌ Desligados | ✅ Quando `enableShadows=true` |
| **Shadow samplers** (`bindShadowTextures`) | ✅ **Sempre** ligados (white / dummy) | ✅ Sempre |

**⚠️ Regra crítica (SIGSEGV NVIDIA):**  
Desligar `enableShadows` **não** pode omitir `bindShadowTextures()`. Os shaders `scene_lit.frag` / `terrain_lit.frag` declaram samplers de sombra; slots não ligados → crash no driver.

```cpp
// Correto: passes condicionais, binds sempre
if (options.enableShadows) {
    renderDirectionalShadows(...);
}
bindShadowTextures(2, 4, 6);  // início do pass + ao trocar mesh ↔ terreno
```

### 2. Uniform de sombra uma vez por pass

`SceneShadowUBO` é enviado **uma vez** antes do loop de draws (não por mesh/chunk).

### 3. Cache de texturas de terreno

Texturas de splat/albedo/normal são ligadas **uma vez por entidade de terreno**, não por chunk LOD.

### 4. LOD de terreno no editor

`GpuSceneRenderOptions::terrainLodDistanceScale = 2.0f` no Scene Viewport — distância efectiva dobrada → LOD mais grosseiro, menos chunks visíveis.

---

## Otimizações no Scene Viewport

| Opção | Valor no editor | Motivo |
|-------|-----------------|--------|
| `enableShadows` | `false` | Evita 4+ redraws da cena por cascata |
| `terrainLodDistanceScale` | `2.0` | Menos chunks GPU |
| `textureQuality.enabled` | `false` | Menos churn de mips/tiers por draw |
| `imguiFramebufferSize` cap | **1280** px | Menos pixels no offscreen target |
| Skybox `maxRasterDim` | 512–1024 | Raster CPU mais barato; re-rasteriza a cada movimento de câmera |

---

## Otimizações nos previews

Helper: `editorPanelWorthGpuRender()` em `EditorPanelUtils.hpp`

- Ignora painéis colapsados, &lt; 32 px, ou fora de `IsRectVisible`
- Skybox desenhado **todo frame** nos previews (antes do `AddImage` GPU)
- GPU só é pulado quando o painel está colapsado / invisível / &lt; 32 px
- Resolução GPU cap **960** px nos previews
- Canvas sempre inicializado antes do primeiro draw (`resizeCanvas` se targets nulos)

---

## Instrumentação (`CF_PROFILE_SCOPE`)

O profiler mantém uma **árvore hierárquica** de scopes (stack trace de consumo). Cada nó regista:

| Métrica | Significado |
|---------|-------------|
| **self ms** | Tempo exclusivo (sem filhos) — onde procurar gargalos |
| **total ms** | Tempo inclusivo (scope + filhos) |
| **% frame** | `avg self` vs último frame time |
| **% parent** | `avg self` vs `avg total` do pai |

Scopes adicionados para diagnóstico no painel **Profiler**:

### SceneEditor

- `SceneEditor::render` (raiz do frame do editor)
- `SceneEditor::propagateTransforms`
- `SceneEditor::hierarchy`, `inspector`, `viewport`, `assetBrowser`, `console`, `profiler`
- `SceneEditor::cameraPreview`, `gameplayPreview`, `materialEditor`, `terrainEditor`, `plugins`, …

### SceneViewport

- `SceneViewport::render`
- `SceneViewport::gpuPass`
- `SceneViewport::skybox`
- `SceneViewport::sprites`, `SceneViewport::entities`

### GpuSceneRenderer

- `GpuSceneRenderer::renderWithCamera`
- `GpuSceneRenderer::gather`
- `GpuSceneRenderer::shadows` (quando passes activos)
- `GpuSceneRenderer::draw`

**Como usar:** abrir Profiler → **Reset Stats** → orbitar câmera / abrir previews → expandir a árvore em **Scope tree** → ordenar por **self ms** (activo por defeito). Passar o rato sobre um scope mostra o caminho completo (`SceneEditor::render > SceneEditor::viewport > …`).

---

## Pipeline actual (3D)

```
1. imguiFramebufferSize(viewportSize, 1280)
2. syncTerrainMeshes(world)                    // só se resolução/revisão mudou
3. GpuSceneRenderer::render(...)
   ├── gatherMeshDraws (frustum + LOD terreno ×2)
   ├── (sem shadow passes no editor)
   └── renderMeshes — bind samplers dummy, 1× shadow UBO
4. ImGui: skybox (CPU, cap resolução) → AddImage(GPU) → grid → overlays CPU
```

---

## Ficheiros principais

| Ficheiro | Alterações |
|----------|------------|
| `src/render/GpuSceneRenderer.cpp` | Shadow UBO 1×, cache terreno, binds obrigatórios |
| `src/render/GpuSceneRenderer.hpp` | `terrainLodDistanceScale` |
| `src/editor/SceneViewport.cpp` | GPU-only meshes, opções editor, scopes |
| `src/editor/CameraPreviewPanel.cpp` | GPU-only / CPU fallback, throttle |
| `src/editor/GameplayPreviewPanel.cpp` | Idem + resizeCanvas seguro |
| `src/editor/EditorPanelUtils.hpp` | `editorPanelWorthGpuRender()` |
| `src/render/SkyboxRenderer.cpp` | Cap resolução; raster sincronizado com câmera |
| `src/terrain/TerrainLodSystem.cpp` | `lodDistanceScale` no gather |

---

## Limitações e trabalho futuro

| Item | Notas |
|------|-------|
| `GpuSceneRenderer::draw` ainda domina frame time em cenas com muito terreno | Próximo passo: instancing/batching de chunks |
| Skybox continua CPU | Migrar para cubemap GPU ou shader fullscreen |
| MSAA no pass GPU | Pendente |
| Profiler próprio pode picar ~30 ms | `SceneEditor::profiler` ao redesenhar tabela |

---

## Ver também

- [Scene Viewport](../editor/scene-viewport.md)
- [Shadow Mapping](../rendering/shadow-mapping.md)
- [Texture Quality LOD](../rendering/texture-quality-lod.md)
- [Sessão 2026-09-22 — Viewport](../plans/2026-09-22-viewport-rendering-quality-session.md)
