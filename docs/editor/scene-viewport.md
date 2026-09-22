# Scene Viewport — Renderização 3D

> **Namespace:** `Caffeine::Editor::SceneViewport`  
> **Ficheiro:** `src/editor/SceneViewport.cpp`  
> **Status:** ✅ Implementado (GPU-first em 3D)  
> **Performance:** ~10 → ~100 FPS no editor (ver [performance](../performance/editor-viewport-performance.md))

---

## Visão geral

O **Scene Viewport** renderiza a cena 3D num target offscreen GPU e compõe o resultado numa janela ImGui. Overlays (grid, gizmos, frustums de câmara, wireframe de seleção) usam `ImDrawList` com projeção 3D consistente.

---

## Modos de preview de mesh

| Modo | GPU | CPU overlay |
|------|-----|-------------|
| **Textured** | `GpuSceneRenderer` Phong (sem shadow passes no editor) | Marcadores, gizmos, grid — **não** redesenha meshes |
| **Wireframe** | `GpuSceneRenderer` pipeline `FillMode::Line` | Idem |

Alternar: botão **Textured / Wireframe** na barra do viewport.

**Importante:** com GPU activo (`gpuOwnsSceneMeshes`), o CPU **não** rasteriza meshes nem terreno no mesmo frame. Ver [performance do viewport](../performance/editor-viewport-performance.md).

---

## Pipeline por frame (3D)

```
1. resizeCanvasIfNeeded(imguiFramebufferSize(viewportSize, 1280))
2. syncTerrainMeshes(world)
3. GpuSceneRenderer::render(cmd, world, ctx, colorTarget, depthTarget, opts)
   ├── gatherMeshDraws (frustum cull, LOD terreno ×2 no editor)
   ├── shadow passes — omitidos no editor (enableShadows=false)
   └── renderMeshes (samplers dummy sempre ligados; Phong ou wireframe)
4. ImGui: skybox (CPU, throttled) → AddImage(GPU) → grid → overlays
   ├── drawEmptyEntities (marcadores apenas; sem meshes se GPU activo)
   ├── drawCameraFrustums
   ├── drawLightGizmos
   └── TransformGizmo
```

Ordem: skybox CPU → composite GPU → grid por cima do GPU (se activo) → gizmos à frente.

---

## Câmara do editor

| Parâmetro | Fonte |
|-----------|--------|
| Posição | `editorCameraPosition(camYaw, camPitch, camDistance, camFocus)` |
| View | `Mat4::lookAt(camPos, camFocus, up)` |
| FOV | 60° (1.0472 rad) — alinhado com `GpuSceneRenderer` |
| Projeção overlay | `computeVP3D(viewportSize, ctx)` |

Ver [`EditorCameraMath.hpp`](../../src/editor/EditorCameraMath.hpp).

---

## Projeção de linhas 3D

Funções estáticas em `SceneViewport`:

```cpp
static bool projectWorldToViewport(const Mat4& vp, Vec3 worldPos,
                                   ImVec2 origin, ImVec2 viewportSize, ImVec2& screenOut);
static void drawViewportWorldLine(ImDrawList* dl, const Mat4& vp, ImVec2 origin,
                                  ImVec2 viewportSize, Vec3 a, Vec3 b,
                                  ImU32 color, float thickness);
```

- Endpoints fora do ecrã são permitidos (ImGui clipa)
- Se um endpoint está atrás do near plane (`w <= 0.1`), o segmento é **clipped** em clip space — evita linhas para coordenadas inválidas

Usado por: grid 3D, frustums `Camera3D`, wireframe CPU, anéis de primitivas.

---

## HiDPI e resolução

```cpp
// ImGuiGpuTexture.hpp
ImVec2 fb = imguiFramebufferSize(viewportSize, 1280);
resizeCanvasIfNeeded((u32)fb.x, (u32)fb.y);
```

- `DisplayFramebufferScale` do ImGui reflecte DPI do monitor
- Cap **1280** px no maior lado (editor); previews usam **960**
- `AddImage` estica o target GPU para o tamanho lógico do ImGui (downscale suave)

---

## Opções GPU (`GpuSceneRenderOptions`)

Construídas em `SceneViewport::render()`:

| Campo | Comportamento no viewport |
|-------|---------------------------|
| `wireframeMeshes` | `true` em modo Wireframe |
| `enableShadows` | **`false`** no editor (shadow passes omitidos; samplers dummy ligados) |
| `terrainLodDistanceScale` | **`2.0`** — LOD de terreno mais agressivo |
| `textureQuality.enabled` | **`false`** no editor (menos churn por draw) |

> **Nota:** `enableShadows=false` desliga apenas os **passes** de sombra. Os samplers continuam ligados — ver [shadow-mapping.md](../rendering/shadow-mapping.md#editor-vs-runtime).

---

## Anti-aliasing de overlays

No início do draw do viewport:

```cpp
drawList->Flags |= ImDrawListFlags_AntiAliasedLines;
drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
```

Afecta grid, gizmos e linhas ImGui — **não** substitui MSAA no pass GPU (pendente).

---

## Camera / Gameplay Preview GPU

- `renderCameraPreviewGpu()` — partilha `GpuSceneRenderer` do viewport; targets `m_previewColorTarget`
- `GameplayPreviewPanel` — renderer próprio, mesmo command buffer
- **Um caminho por frame:** GPU **ou** `drawSceneMeshesForCamera()` (CPU), nunca ambos
- Throttle: `editorPanelWorthGpuRender(origin, size, 2)` — ver `EditorPanelUtils.hpp`

---

## Preferências relacionadas

**Settings → Viewport:**

- Move speed / Orbit sensitivity
- Show grid
- **Texture quality** (raio, falloff, mínimo) — ver [`texture-quality-lod.md`](../rendering/texture-quality-lod.md)

Campos em `EditorContext` sincronizados via `SettingsPanel::applyPreferencesToContext()`.

---

## Ficheiros principais

| Ficheiro | Responsabilidade |
|----------|------------------|
| `src/editor/SceneViewport.cpp` | UI, input câmara, composição |
| `src/editor/SceneViewport.hpp` | API pública, raster CPU auxiliar |
| `src/editor/ImGuiGpuTexture.hpp` | `imguiFramebufferSize()` |
| `src/render/GpuSceneRenderer.cpp` | Pass GPU cena |
| `src/editor/EditorCameraMath.hpp` | Orbit / look direction |

---

## Limitações conhecidas

| Item | Notas |
|------|-------|
| MSAA no pass GPU | Não implementado; silhuetas podem serrilhar |
| Gizmos ImGui | Sem depth test contra cena GPU (intencional — sempre visíveis) |
| Runtime play | `enableShadows` conforme contexto; ver `RuntimeSceneRenderer` |

---

## Ver também

- [**Performance — Editor Viewport**](../performance/editor-viewport-performance.md)
- [Sessão 2026-09-22 — Viewport & Quality](../plans/2026-09-22-viewport-rendering-quality-session.md)
- [Texture Quality LOD](../rendering/texture-quality-lod.md)
- [Materials Phong](../rendering/materials-phong.md)
- [Shadow Mapping](../rendering/shadow-mapping.md)
