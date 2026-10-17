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
| **Textured** | `GpuSceneRenderer` PBR forward + HDR + `PostProcessStack` (TAA/SSAO defaults) | Gizmos, frustums — **não** redesenha meshes nem céu CPU se GPU cobre o viewport |
| **Wireframe** | `GpuSceneRenderer` pipeline `FillMode::Line` | Idem |

Alternar: botão **Textured / Wireframe** na barra do viewport.

**Importante:** com GPU activo (`gpuOwnsSceneMeshes`), o CPU **não** rasteriza meshes nem terreno no mesmo frame. Ver [performance do viewport](../performance/editor-viewport-performance.md).

---

## Pipeline por frame (3D)

```
1. resizeCanvasIfNeeded(imguiFramebufferSize(viewportSize, 3840))
2. syncTerrainMeshes(world)
3. GpuSceneRenderer::renderWithCamera / render (HDR scene → PostProcessStack → RGBA8)
   ├── resolveForwardRenderFeatures + PostProcessComponent (câmara selecionada ou defaults)
   ├── planar reflection + reflection probes (top-level pass)
   ├── shadow passes (runtime / quando enableShadows)
   ├── opaque → sky GPU → grid GPU → translucent (copy opaco + blend)
   └── TAA jitter, SSR history, needsAnotherFrame até convergir
4. ImGui: AddImage(GPU) → overlays (sem fake post ImGui)
   ├── drawEmptyEntities, drawCameraFrustums, drawLightGizmos, TransformGizmo
   └── CPU sky/grid só se gpuImageCoversViewport == false
```

`renderScale` (Settings → Viewport → Rendering) multiplica a resolução interna da cena.

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
ImVec2 fb = imguiFramebufferSize(viewportSize, 3840);
resizeCanvasIfNeeded((u32)fb.x, (u32)fb.y);
```

- `DisplayFramebufferScale` do ImGui reflecte DPI do monitor
- Cap **3840** px no maior lado (viewport); camera preview até **1920**; gameplay preview até **2560**
- `ctx.renderScale` (0.25–2×) aplicado em `GpuSceneRenderOptions::renderScale`
- `cameraFarPlane()` dinâmico a partir da distância da câmara ao foco

---

## Opções GPU (`GpuSceneRenderOptions`)

Construídas em `SceneViewport::render()`:

| Campo | Comportamento no viewport |
|-------|---------------------------|
| `wireframeMeshes` | `true` em modo Wireframe |
| `enableShadows` | Conforme contexto (editor pode omitir passes; shader suporta PCF atlas) |
| `renderScale` | De `EditorContext` / preferências |
| `viewId` | `1` viewport, `2` camera preview (histórico TAA/SSR separado) |
| `postProcessCamera` | Entidade com `PostProcessComponent` (câmara selecionada no viewport) |
| `environmentPath` | Vazio se skybox desligado na toolbar |
| `grid` | Espaçamento derivado da distância da câmara; desenhado no GPU |
| `terrainLodDistanceScale` | **`2.0`** no editor |
| `textureQuality.enabled` | **`false`** no viewport (menos churn) |

> **Nota:** `enableShadows=false` desliga apenas os **passes** de sombra. Os samplers continuam ligados — ver [shadow-mapping.md](../rendering/shadow-mapping.md#editor-vs-runtime).

---

## Anti-aliasing de overlays

No início do draw do viewport:

```cpp
drawList->Flags |= ImDrawListFlags_AntiAliasedLines;
drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
```

Afecta gizmos e linhas ImGui. Silhuetas da cena usam **TAA** no `PostProcessStack` (não MSAA hardware).

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
- **Render scale** (resolução interna HDR)
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
| MSAA hardware | Não usado; TAA + render scale > 1 melhoram bordas |
| Gizmos ImGui | Sem depth test contra cena GPU (intencional — sempre visíveis) |
| Runtime play | `enableShadows` conforme contexto; ver `RuntimeSceneRenderer` |

---

## Ver também

- [**Performance — Editor Viewport**](../performance/editor-viewport-performance.md)
- [Sessão 2026-09-22 — Viewport & Quality](../plans/2026-09-22-viewport-rendering-quality-session.md)
- [Texture Quality LOD](../rendering/texture-quality-lod.md)
- [Materials PBR](../rendering/materials-pbr.md)
- [Post-processing](../rendering/post-processing.md)
- [Material Editor](material-editor.md)
- [Shadow Mapping](../rendering/shadow-mapping.md)
