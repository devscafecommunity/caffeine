# Terrain System — Roadmap

> **Namespace:** `Caffeine::Terrain`
> **Diretório:** `src/terrain/`, `src/terrain/generation/`
> **Status geral:** Fases 1–6 concluídas · Fases 7–13 planeadas

---

## Visão Geral

O sistema de terreno do Caffeine é baseado em **heightmap normalizado** + **splatmap de 4 camadas**, com mesh chunked LOD, persistência `.cterrain`, render GPU com blending de texturas e pipeline procedural modular.

```
TerrainComponent (ECS)
        │
        ▼
TerrainCache (singleton)
├── TerrainHeightmap
├── TerrainSplatmap
├── TerrainMeshBuilder / TerrainLodSystem
├── TerrainGpuTextureCache
└── TerrainGenerator (pipeline modular)
        │
        ▼
SceneViewport / GpuSceneRenderer
```

---

## Fases Concluídas

| Fase | Nome | Descrição | Status |
|:----:|------|-----------|:------:|
| 1 | Sculpt Editor | Brush raise/lower/smooth, raycast, overlay no viewport | ✅ |
| 2 | Chunks + LOD | Divisão em chunks, 4 níveis LOD, frustum culling, debug overlay | ✅ |
| 3 | Splatmaps | 4 layers Kenney, paint no viewport, blending CPU/GPU | ✅ |
| 4 | Serialização | `.cterrain` (heights + splat), integração `.caf` v7 | ✅ |
| 5 | GPU Path | `terrain_lit.frag`, splat blending, LOD draws no `GpuSceneRenderer` | ✅ |
| 6 | Geração Procedural | Pipeline modular: noise (Perlin/Simplex/Value/Worley), erosão, presets | ✅ |

### Ficheiros principais (estado atual)

| Área | Ficheiros |
|------|-----------|
| ECS | `src/ecs/TerrainComponents.hpp` |
| Dados | `src/terrain/TerrainHeightmap.*`, `TerrainSplatmap.*` |
| Cache | `src/terrain/TerrainCache.*` |
| Mesh/LOD | `src/terrain/TerrainMeshBuilder.*`, `TerrainLodSystem.*` |
| Edição | `src/terrain/TerrainSculptor.*`, `TerrainSplatPainter.*` |
| Persistência | `src/terrain/TerrainSerializer.*` |
| GPU | `src/terrain/TerrainGpuTextures.*`, `src/render/shaders/terrain_lit.frag` |
| Geração | `src/terrain/generation/TerrainNoise.*`, `TerrainGenerator.*` |
| Editor | `src/editor/InspectorPanel.cpp` (drawTerrain), `SceneViewport.cpp` |

---

## Próximas Fases

| Fase | Documento | Prioridade | Esforço |
|:----:|-----------|:----------:|:-------:|
| 7 | [7.1-modulos-adicionais.md](7.1-modulos-adicionais.md) | Alta | Médio |
| 8 | [7.2-combinadores-booleanos.md](7.2-combinadores-booleanos.md) | Alta | Médio |
| 9 | [7.3-brush-de-geracao.md](7.3-brush-de-geracao.md) | Média | Alto |
| 10 | [7.4-undo-de-geracao.md](7.4-undo-de-geracao.md) | Média | Baixo |
| 11 | [7.5-gpu-compute.md](7.5-gpu-compute.md) | Baixa | Alto |
| 12 | [7.6-import-png.md](7.6-import-png.md) | Média | Baixo |
| 13 | [7.7-colisao-fisica.md](7.7-colisao-fisica.md) | Alta | Alto |

### Ordem de implementação sugerida

```
7.4 Undo ──► 7.1 Módulos ──► 7.2 Combinadores ──► 7.3 Brush
                                                      │
7.6 Import PNG ◄──────────────────────────────────────┘
        │
7.7 Colisão física
        │
7.5 GPU Compute (quando resolução/iterações exigirem)
```

**Racional:** Undo é barato e desbloqueia experimentação segura. Módulos e combinadores expandem o pipeline antes do brush. Import PNG é entrada de dados externa. Colisão é requisito de gameplay. GPU compute só quando CPU for gargalo comprovado.

---

## Débito Técnico Conhecido

| Issue | Severidade | Ficheiro provável |
|-------|:----------:|-------------------|
| ~~heap-use-after-free~~ no shutdown (`m_whiteTexture` destruída via `layers[]` partilhado) | ✅ Corrigido | `TerrainGpuTextures.cpp` |
| Leak 51 B em `MeshFilterComponent` copy no ECS pool | 🟡 Baixo | `MeshComponents.hpp`, `Vector.hpp` |
| Leaks indirectos SDL3/ImGui (~1 MB) no shutdown | 🟢 Aceitável | backends ImGui/SDL |
| `RuntimeSceneRenderer` sem suporte a terreno | 🟡 Médio | `RuntimeSceneRenderer.cpp` |
| Undo system sem terrain no `SceneSerializer` runtime | 🟡 Médio | `SceneSerializer.cpp` |

---

## Referências

- Geração procedural: `src/terrain/generation/`
- Formato `.cterrain`: `src/terrain/TerrainSerializer.hpp`
- Shader terreno: `src/render/shaders/terrain_lit.frag`
- Inspector: secção **Procedural Generation** em `drawTerrain()`
