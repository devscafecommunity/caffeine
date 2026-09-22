# Scene Viewport — Renderização 3D

> **Namespace:** `Caffeine::Editor::SceneViewport`  
> **Ficheiro:** `src/editor/SceneViewport.cpp`  
> **Status:** ✅ Implementado (GPU-first em 3D)

---

## Visão geral

O **Scene Viewport** renderiza a cena 3D num target offscreen GPU e compõe o resultado numa janela ImGui. Overlays (grid, gizmos, frustums de câmara, wireframe de seleção) usam `ImDrawList` com projeção 3D consistente.

---

## Modos de preview de mesh

| Modo | GPU | CPU overlay |
|------|-----|-------------|
| **Textured** | `GpuSceneRenderer` Phong + sombras (se estável) | Só entidade **selecionada** (contorno) |
| **Wireframe** | `GpuSceneRenderer` pipeline `FillMode::Line` | Só entidade **selecionada** |

Alternar: botão **Textured / Wireframe** na barra do viewport.

**Importante:** em Textured, o CPU **não** redesenha meshes não seleccionados — evita double render e queda de FPS.

---

## Pipeline por frame (3D)

```
1. resizeCanvasIfNeeded(imguiFramebufferSize(viewportSize))  // HiDPI, cap 1920px
2. syncTerrainMeshes(world)
3. GpuSceneRenderer::render(cmd, world, ctx, colorTarget, depthTarget, opts)
   ├── gatherMeshDraws (frustum cull, distância a visualizadores)
   ├── shadow passes (se enableShadows)
   └── renderMeshes (Phong ou wireframe)
4. ImGui: skybox → grid (atrás) → AddImage(GPU) → overlays
   ├── drawEmptyEntities (marcadores, seleção)
   ├── drawCameraFrustums
   ├── drawLightGizmos
   └── TransformGizmo
```

Ordem de desenho garante que o grid fica **atrás** do GPU (desenhado antes do `AddImage`), e gizmos **à frente**.

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
ImVec2 fb = imguiFramebufferSize(viewportSize, 1920);
resizeCanvasIfNeeded((u32)fb.x, (u32)fb.y);
```

- `DisplayFramebufferScale` do ImGui reflecte DPI do monitor
- Cap no maior lado evita render 4K+ no painel do editor
- `AddImage` estica o target GPU para o tamanho lógico do ImGui (downscale suave)

---

## Opções GPU (`GpuSceneRenderOptions`)

Construídas em `SceneViewport::render()`:

| Campo | Comportamento no viewport |
|-------|---------------------------|
| `wireframeMeshes` | `true` em modo Wireframe |
| `enableShadows` | `false` se câmara em movimento rápido (`m_editorCamMotion >= 1.25`) |
| `textureQuality` | Copiado de `EditorContext` |
| `textureQualityViewers` | Câmara editor + posições de todas `Camera3DComponent` |

### Detecção de movimento de câmara

```cpp
frameMotion = |Δyaw|*14 + |Δpitch|*14 + |Δpos|
m_editorCamMotion = m_editorCamMotion * 0.75 + frameMotion * 0.25
```

Sombras voltam quando a câmara estabiliza (~200 ms).

---

## Anti-aliasing de overlays

No início do draw do viewport:

```cpp
drawList->Flags |= ImDrawListFlags_AntiAliasedLines;
drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
```

Afecta grid, gizmos e linhas ImGui — **não** substitui MSAA no pass GPU (pendente).

---

## Camera Preview GPU

`renderCameraPreviewGpu()` usa o mesmo `GpuSceneRenderer` com:

- `textureQualityViewers = { cameraPos }` (só a câmara do painel)
- Targets `m_previewColorTarget` / `m_previewDepthTarget`

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
| Runtime / Gameplay Preview | Mesma API GPU; shadows sempre ligadas salvo lógica própria |

---

## Ver também

- [Sessão 2026-09-22 — Viewport & Quality](../plans/2026-09-22-viewport-rendering-quality-session.md)
- [Texture Quality LOD](../rendering/texture-quality-lod.md)
- [Materials Phong](../rendering/materials-phong.md)
- [Shadow Mapping](../rendering/shadow-mapping.md)
