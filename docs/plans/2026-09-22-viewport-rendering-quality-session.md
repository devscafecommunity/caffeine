# Sessão 2026-09-22 — Viewport 3D, Qualidade Visual e Performance

> **Objetivo:** corrigir artefactos visuais no Scene Viewport (costuras de shading, aspecto 2.5D, serrilhamento), melhorar FPS em movimento rápido de câmara e introduzir LOD de texturas por distância aos visualizadores.  
> **Build verificado:** `doppio` compila após as alterações.

---

## Resumo executivo

| Área | Item | Estado |
|------|------|--------|
| Meshes procedurais | Remover `computeSmoothNormals()` em primitivas com normais analíticas | ✅ |
| Viewport HiDPI | Render offscreen na resolução do framebuffer (com cap 1920px) | ✅ |
| Linhas 3D | Clipping no near plane (`drawViewportWorldLine`) | ✅ |
| Wireframe GPU | Pipeline `SDL_GPU_FILLMODE_LINE` com depth test | ✅ |
| Anti-aliasing UI | `AntiAliasedLines` / `AntiAliasedFill` no draw list do viewport | ✅ |
| FPS câmara | Suspender shadow passes durante movimento rápido | ✅ |
| LOD texturas | Qualidade por distância a visualizadores (viewport + Camera 3D) | ✅ |
| Preferências | Sliders em Settings → Viewport | ✅ |

---

## 1. Costuras de shading em primitivas curvas

### Problema

Esfera, torus e cápsula mostravam uma **linha vertical** na iluminação — costura longitudinal.

### Causa

`finishCurvedPrimitive()` chamava `Assets::computeSmoothNormals()` **depois** de as meshes já terem normais analíticas corretas. Vértices duplicados na costura (`lon=0` vs `lon=segments`, mesma posição, índices diferentes) recebiam médias de faces diferentes.

### Correção

Removido `finishCurvedPrimitive()` e a dependência de `MeshNormals.hpp` em `GpuProceduralMeshes.cpp`. Primitivas curvas (esfera, cilindro, cápsula, cone, torus) mantêm apenas normais analíticas.

### Política de shading

| Tipo | Shading |
|------|---------|
| Esfera, cilindro, cápsula, cone, torus | Smooth (normais analíticas) |
| Cubo, plano, pirâmide | Facetado |
| Terreno | Facetado por chunk (LOD de geometria) |
| GLB sem normais | `computeSmoothNormals()` em `MeshLoader.cpp` (mantido) |

### Ficheiros

- `src/render/GpuProceduralMeshes.cpp`
- `src/assets/MeshNormals.hpp` (utilitário; não usado em procedurais GPU)
- `src/ecs/MeshComponents.hpp` — `meshPrimitiveUsesSmoothShading()`

---

## 2. Viewport HiDPI e resolução offscreen

### Problema

O viewport renderizava em pixels **lógicos** do ImGui enquanto a janela usa `SDL_WINDOW_HIGH_PIXEL_DENSITY` — metade da resolução real, upscale visível (serrilhamento).

### Correção

`imguiFramebufferSize()` em `ImGuiGpuTexture.hpp` multiplica por `ImGui::GetIO().DisplayFramebufferScale`.

**Cap:** maior lado limitado a **1920px** para evitar targets 4K+ no editor.

Aplicado em:

- `SceneViewport` (canvas principal)
- `GameplayPreviewPanel`
- `CameraPreviewPanel`

---

## 3. Linhas 3D estáveis (grid, frustums, wireframe CPU)

### Problema

Grid, frustum de câmara e wireframes CPU desenhavam segmentos com um endpoint atrás do near plane projetado para `(-10000, -10000)` — linhas “quebradas” e dependentes do ângulo da câmara.

### Correção

Novos helpers em `SceneViewport`:

- `projectWorldToViewport()` — projeta mundo → pixels; falha só atrás do near plane
- `drawViewportWorldLine()` — clip no near plane em clip space (mesma lógica que o grid)

Usados em: `drawGrid3D`, `drawCameraFrustums`, `drawSegment` (wireframe CPU em 3D).

---

## 4. Wireframe GPU com profundidade real

### Problema

Modo **Wireframe** desligava a GPU e desenhava apenas overlay ImGui sem Z-buffer — objetos atravessavam-se, aspecto 2.5D.

### Correção

- `RHI::FillMode::Line` → `SDL_GPU_FILLMODE_LINE` em `RenderDevice`
- Pipeline `m_wireframePipeline` em `GpuSceneRenderer`
- Scene Viewport activa GPU em **Textured e Wireframe**; overlay CPU só para seleção/destaque

`GpuSceneRenderOptions::wireframeMeshes` selecciona o pipeline.

---

## 5. Performance — movimento rápido de câmara

### Problema

Orbit/zoom rápido causava quedas de FPS (profiler: picos até ~850 ms).

### Causas

1. Shadow passes redesenham a cena inteira (4 cascatas direcionais + cubemaps point + spot)
2. Resolução offscreen alta em HiDPI

### Correções

| Medida | Implementação |
|--------|----------------|
| Skip shadows em movimento | `m_editorCamMotion` em `SceneViewport`; `enableShadows = false` se motion > 1.25 |
| Cap resolução | `imguiFramebufferSize(..., maxDim=1920)` |
| GPU só em Textured (anterior) | Wireframe também usa GPU desde esta sessão |

### API

```cpp
struct GpuSceneRenderOptions {
    bool wireframeMeshes = false;
    bool enableShadows   = true;
    TextureQualitySettings textureQuality{};
    std::vector<Vec3>      textureQualityViewers;
};
```

---

## 6. LOD de texturas por distância aos visualizadores

### Comportamento

- **Não remove** texturas — reduz resolução em cache GPU
- Distância = mínimo entre a posição do objeto e cada **visualizador**:
  - Câmara do Scene Viewport (orbit)
  - Todas as entidades com `Camera3DComponent`
  - Camera Preview usa só a câmara do painel
- Dentro de `fullRadius` → tier 0 (100%)
- Além do raio → decaimento linear até `minScale` ao longo de `falloffDistance`
- Tiers em cache: 100% → 50% → 25% → 12.5% (mín. 16px)

### Configuração (Settings → Viewport → Texture quality)

| Campo | Default | Descrição |
|-------|---------|-----------|
| `texture_quality_enabled` | true | Liga/desliga LOD |
| `texture_quality_radius` | 35 m | Raio de resolução total |
| `texture_quality_falloff` | 100 m | Distância de decaimento |
| `texture_quality_min_scale` | 0.25 | Escala mínima (1/4 res) |

Persistido em `~/.config/caffeine/editor_preferences.json`.

### Ficheiros

- `src/render/TextureQuality.hpp`
- `src/render/GpuTextureCache.{hpp,cpp}` — `acquire(..., qualityTier)`
- `src/render/GpuSceneRenderer.cpp` — `MeshDraw::viewerDistance`
- `src/editor/EditorContext.hpp`, `EditorPreferences.{hpp,cpp}`
- `src/editor/SettingsPanel.cpp`

### Documentação técnica

- [`rendering/texture-quality-lod.md`](../rendering/texture-quality-lod.md)
- [`editor/scene-viewport.md`](../editor/scene-viewport.md)

---

## 7. Primitivas e editor (sessão anterior relacionada)

Itens da mesma linha de trabalho já no código (referência):

| Item | Ficheiros |
|------|-----------|
| Cápsula ≠ cilindro | `GpuProceduralMeshes.cpp` — `buildCapsule()` |
| Cone, pirâmide, torus | `MeshComponents.hpp`, `HierarchyPanel`, `InspectorPanel` |
| Fix orbit câmara eixo Y | `EditorCameraMath.hpp`, `SceneViewport.cpp` |
| Menu Create no Hierarchy | `HierarchyPanel.cpp` |
| GPU shutdown / ASan | `apps/doppio/main.cpp`, cleanup ordem em `SceneEditor` |

---

## Links

- [Scene Viewport](../editor/scene-viewport.md)
- [Texture Quality LOD](../rendering/texture-quality-lod.md)
- [Engine Pillars Roadmap](2026-09-22-engine-pillars-roadmap.md)
- [Rendering Roadmap](2026-09-21-rendering-roadmap.md)
- [Sessão Phong GPU](2026-09-22-phong-gpu-handles-session.md)
