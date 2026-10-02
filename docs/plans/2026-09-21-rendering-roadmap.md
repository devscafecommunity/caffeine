# 🎨 Rendering Roadmap — 3D de Alto Volume

> **Objetivo:** renderizar ambientes 3D com alto volume de polígonos e texturas de forma **otimizada, consistente e estável** (sem spikes de FPS).  
> **Critério de “completo”:** cena densa (terreno + centenas de meshes texturizados + iluminação dinâmica + sombras) a 60 FPS estáveis no editor e no runtime.  
> **Estado atual (2026-10-02):** viewport GPU; cap editor 1280 px; mesh LOD; GGX + ACES; sombras no editor só com a câmara parada e **1 cascata**. Instancing e IBL estão no passe principal (ligados por defeito). Oclusão coarse, reflexos (planar / screen-space / probe) e volumétricos são opcionais e ficam desligados — reflexos e probes só correm com a câmara parada. Cena `assets/benchmarks/dense_outdoor.caf`. O gate p95 < 16.6 ms ainda não foi medido no editor.

---

## Diagnóstico (causa provável dos FPS spikes)

| Sintoma | Causa raiz | Ficheiros |
|---------|-----------|-----------|
| Spike ao mover câmara | Shadow passes + resolução HiDPI alta | `GpuSceneRenderer.cpp`, `SceneViewport.cpp` |
| Spike ao mover câmara (mitigado) | Skip shadows em movimento rápido; cap resolução 1920px | `SceneViewport.cpp` |
| Terreno pesado | Rebuild mesh / upload GPU síncrono | `TerrainCache.cpp`, `GpuSceneRenderer.cpp` |
| Muitos draw calls | 1 `drawIndexed` por mesh, sem batching | `GpuSceneRenderer.cpp` |
| Sombras inconsistentes | ~~Runtime CPU shadows~~ resolvido; tuning CSM/PCF | `GpuDirectionalShadowMap.*`, `scene_lit.frag` |
| Materiais “flat” | Phong ligado; PBR metallic/roughness pendente | `scene_lit.frag`, `terrain_lit.frag` |

---

## Fases e dependências

```mermaid
flowchart TD
    P0[P0 Fundação GPU única]
    P1[P1 Texturas e materiais]
    P2[P2 Geometria e LOD]
    P3[P3 Iluminação PBR]
    P4[P4 Shadow mapping GPU]
    P5[P5 Reflexos]
    P6[P6 Luz volumétrica]
    P0 --> P1 --> P2
    P1 --> P3 --> P4
    P4 --> P5
    P3 --> P6
```

Cada fase tem entregável testável. **Não avançar** para reflexos/volumétricos antes de P4 estável.

---

## P0 — Fundação: um único caminho GPU

**Meta:** eliminar dual-path (CPU raster + GPU) no viewport 3D.

| Tarefa | Descrição | Ficheiros |
|--------|-----------|-----------|
| P0.1 | GPU como default para **todos** os meshes texturizados | `SceneViewport.cpp`, `GpuSceneRenderer.cpp` |
| P0.2 | Remover fallback CPU exceto wireframe/debug | `SceneViewport.cpp` | ✅ (GPU texturizado + wireframe `FillMode::Line`) |
| P0.3 | Upload de buffers **antes** do render pass (já parcial) | `GpuSceneRenderer.cpp` |
| P0.4 | Runtime play mode usar `GpuSceneRenderer` (hoje CPU-only) | `RuntimeSceneRenderer.cpp` | ✅ |
| P0.5 | Profiler markers por pass (shadow, opaque, terrain, composite) | `Profiler.hpp`, viewport | ✅ |

**Métrica de sucesso:** viewport 3D sem `MeshCpuRasterizer` em modo shaded; FPS estável ±5% ao orbitar câmara.

**Doc detalhada:** [`rendering/rhi.md`](../rendering/rhi.md) (atualizar após P0)

---

## P1 — Texturas e materiais

**Meta:** cache GPU global, mips, sem reload por frame.

| Tarefa | Descrição | Ficheiros |
|--------|-----------|-----------|
| P1.1 | `GpuTextureCache` central (path → `GPUTexture`, refcount) | `src/render/GpuTextureCache.*` | ✅ |
| P1.2 | Geração de mipmaps no upload | `GpuTextureCache.cpp`, `RenderDevice.cpp` | ✅ |
| P1.3 | Bind albedo + normal + ORM no `scene_lit` | `scene_lit.frag`, `MaterialFile`, Material Editor | ✅ albedo, normal, ORM, emissão; o `.mat` alimenta o passe |
| P1.4 | Async decode (job system) + upload no frame N+1 | `JobSystem`, `AssetManager` |
| P1.5 | Atlas opcional para props repetidos (instancing) | `TextureAtlas.hpp` (estender) |
| P1.6 | LOD de texturas por distância aos visualizadores (tiers em cache) | `TextureQuality.hpp`, `GpuTextureCache.*` | ✅ |

**Métrica:** 0 `stbi_load` durante render loop; memória GPU estável após warm-up.

**Doc:** [`assets/asset-manager.md`](../assets/asset-manager.md), [`rendering/texture-quality-lod.md`](../rendering/texture-quality-lod.md), [`rendering/materials-pbr.md`](../rendering/materials-pbr.md)

---

## P2 — Polígonos, meshes e LOD

**Meta:** cenas com 500k–2M triângulos visíveis com culling agressivo.

| Tarefa | Descrição | Ficheiros |
|--------|-----------|-----------|
| P2.1 | Mesh LOD real (simplificação ou LOD chains no import) | `MeshLOD.cpp`, pipeline `.caf` | ✅ clustering; import chains ainda não |
| P2.2 | Seleção LOD por distância + hysteresis (como terreno) | `GpuSceneRenderer.cpp` | ✅ |
| P2.3 | Frustum + occlusion coarse (octree já existe) | `CoarseOcclusion.hpp` | ✅ off por defeito; modo Coarse |
| P2.4 | Instancing para meshes repetidos (árvores, props) | `drawIndexedInstanced` | ✅ até 256 por batch |
| P2.5 | Terreno: collision mesh separado (feito); GPU chunks estáveis | `TerrainLodSystem.*` |

**Métrica:** draw calls < 200 para cena de referência; triângulos visíveis escalam com LOD.

**Doc:** [`rendering/mesh-lod.md`](../rendering/mesh-lod.md)

---

## P3 — Iluminação

**Meta:** PBR forward+ (Cook-Torrance) com múltiplas luzes.

| Tarefa | Descrição | Ficheiros |
|--------|-----------|-----------|
| P3.1 | UBO de luzes unificado (dir/point/spot, até N luzes) | `LightingSystem.*`, shaders |
| P3.2 | PBR: metallic/roughness, Fresnel, GGX | `scene_lit.frag` | ✅ specular GGX; diffuse Lambert |
| P3.4 | Tone mapping (ACES) + exposure | shader + `PostProcess` GPU futuro | ✅ ACES no `scene_lit` |
| P3.3 | IBL básico (irradiance + probe opcional) | `scene_lit.frag` | ✅ cor de ambiente; probe se o modo estiver ativo |
| P3.5 | Spot lights no GPU (hoje só CPU) | `scene_lit.frag` | ✅ |

**Métrica:** esfera de referência + terreno com resposta física plausível; sem banding em gradientes.

**Doc:** atualizar [`rendering/camera-3d.md`](../rendering/camera-3d.md)

---

## P4 — Shadow mapping (GPU)

**Meta:** sombras direcionais + spot estáveis; point opcional (cubemap).

| Tarefa | Descrição | Ficheiros |
|--------|-----------|-----------|
| P4.1 | Ligar `renderDirectionalShadows` em `renderWithCamera` | `GpuSceneRenderer.cpp` | ✅ |
| P4.2 | CSM 2–4 cascatas para sol | `GpuDirectionalShadowMap.*` | ✅ |
| P4.3 | PCF / PCSS no `scene_lit.frag` | shaders | ✅ PCF 3×3 |
| P4.4 | Terreno `castShadows` no gather GPU | `GpuSceneRenderer.cpp` | ✅ |
| P4.5 | Spot shadow maps (1–2 luzes) | `GpuSpotShadowMap.*` | ✅ |
| P4.6 | Desligar CPU shadow path no editor quando GPU OK | `SceneViewport.cpp` | ✅ editor: 1 cascata só com câmara parada |
| P4.7 | Point light cubemap shadows (até 2) | `GpuPointShadowMap.*` | ✅ |

**Infra ligada:** `GpuDirectionalShadowMap`, `GpuPointShadowMap`, `shadow_depth.*` — ver [`rendering/shadow-mapping.md`](../rendering/shadow-mapping.md)

**Métrica:** sombras sem acne/ peter-panning visível; custo < 3 ms @ 1080p.

**Doc:** [`rendering/shadow-mapping.md`](../rendering/shadow-mapping.md), [`rendering/materials-phong.md`](../rendering/materials-phong.md)

---

## P5 — Reflexos em tempo real

**Meta:** reflexos convincentes sem custo proibitivo.

| Abordagem | Quando | Custo |
|-----------|--------|-------|
| **Planar** (espelhos, água plana) | P5.1 | Baixo |
| **SSR** (screen-space) | P5.2 | Médio |
| **Reflection probes** (cubemap local) | P5.3 | Médio |
| **RT reflections** | futuro | Alto |

| Tarefa | Descrição |
|--------|-----------|
| P5.1 | Pass de reflexão planar + clip plane | ✅ meia resolução, só com câmara parada |
| P5.2 | SSR sobre o alvo planar (passos configuráveis, sem G-buffer) | ✅ |
| P5.3 | Probe cubemap local, 16–128 px, recaptura quando a câmara assenta | ✅ |

**Pré-requisito:** P4 + depth prepass ou G-buffer mínimo.

**Doc:** nova `docs/rendering/reflections.md`

---

## P6 — Luz volumétrica

**Meta:** god rays, fog volumétrico, shafts de luz direcional.

| Tarefa | Descrição |
|--------|-----------|
| P6.1 | Ray march no shader forward (6 ou 12 passos) | ✅ não redesenha a cena |
| P6.2 | Nevoeiro por altura (sem textura 3D) | ✅ |
| P6.3 | Amostra opcional do shadow map no march | ✅ |

**Nota:** `PostProcessRenderer` atual é overlay ImGui — não conta como volumétrico GPU.

**Doc:** nova `docs/rendering/volumetric-lighting.md`

---

## Cena de referência (benchmark interno)

`assets/benchmarks/dense_outdoor.caf` (constantes em `DenseOutdoorBenchmark.hpp`):

- Terreno 129² (sobe para 512 em `kDenseOutdoorTerrainResolution` quando o orçamento aguentar)
- 240 esferas + 40 cubos, o mesmo mesh para instancing
- 1 direcional + 2 point + 1 spot
- Plano de água em Y = 0 para o modo planar
- Frame time no profiler do editor (p50 / p95 / p99 ainda manuais)

**Gate de qualidade:** p95 frame time < 16.6 ms @ 1080p antes de marcar fase como done.

---

## Ordem de execução recomendada

1. **P0** (esta semana) — maior impacto nos spikes
2. **P1** — texturas são o suspeito #1 dos spikes atuais
3. **P2** — polígonos sem LOD matam GPU e CPU
4. **P4** — sombras GPU (infra já existe)
5. **P3** — PBR em cima de base estável
6. **P5 / P6** — polish visual avançado

---

## Links

| Documento | Conteúdo |
|-----------|----------|
| [`rendering/rhi.md`](../rendering/rhi.md) | Abstração SDL_GPU |
| [`rendering/batch-renderer.md`](../rendering/batch-renderer.md) | Sprites 2D (não 3D) |
| [`terrain/terrain-system.md`](../terrain/terrain-system.md) | Terreno LOD/chunks |
| [`plans/2026-05-23-3d-mesh-enhancements.md`](2026-05-23-3d-mesh-enhancements.md) | Plano anterior meshes |
| [`plans/2026-09-22-viewport-rendering-quality-session.md`](2026-09-22-viewport-rendering-quality-session.md) | Sessão viewport: HiDPI, wireframe GPU, LOD texturas |
| [`rendering/texture-quality-lod.md`](../rendering/texture-quality-lod.md) | LOD de texturas por distância |
| [`editor/scene-viewport.md`](../editor/scene-viewport.md) | Pipeline do Scene Viewport 3D |
| [`rendering/mesh-lod.md`](../rendering/mesh-lod.md) | LOD de meshes por clustering |
| [`rendering/reflections.md`](../rendering/reflections.md) | Planar, screen-space e probe |
| [`rendering/volumetric-lighting.md`](../rendering/volumetric-lighting.md) | Nevoeiro analítico |
