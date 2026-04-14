# 👁️ Fase 3: O Olho da Engine

> **Status:** 📅 Planejado  
> **Responsável:** Artisans / Architects  
> **Versão alvo:** `0.3.x`

Esta fase constrói a camada de renderização — o "olho" da engine. Com ela, a engine sai de dados puros e passa a produzir **pixels na tela** de forma eficiente.

---

## 📋 Índice

| Módulo | Arquivo | Namespace | Fase |
|--------|---------|-----------|------|
| **RHI** | [`rhi.md`](rhi.md) | `Caffeine::RHI` | 3 |
| **Batch Renderer** | [`batch-renderer.md`](batch-renderer.md) | `Caffeine::Render` | 3 |
| **Camera System** | [`camera.md`](camera.md) | `Caffeine::Render` | 3 |
| **Asset Manager** | [`asset-manager.md`](asset-manager.md) | `Caffeine::Assets` | 3 |
| **Formato .caf** | [`caf-format.md`](caf-format.md) | `Caffeine` | 3+4 |

---

## 🎯 Objetivo da Fase

> "Construir a camada de renderização agnóstica e sistema de assets."

Nesta fase a engine ganha:
- **RHI** — abstração sobre SDL_GPU para portabilidade
- **Batch Renderer** — 50.000 sprites em 1 draw call
- **Camera 2D** — projeção ortográfica com follow e shake
- **Asset Manager** — carregamento assíncrono com hot-reload
- **Formato .caf** — binary asset format zero-parsing

---

## 📊 Requisitos Funcionais

| ID | Requisito | Critério de Aceitação | Módulo |
|----|-----------|----------------------|--------|
| **RF3.0** | Caffeine Binary System (.caf) | Zero-parsing, zero-copy | [caf-format.md](caf-format.md) |
| **RF3.1** | RHI abstraction | Sem SDL_Draw direto no código do jogo | [rhi.md](rhi.md) |
| **RF3.2** | DrawCommand queue | Command buffer com flush automático | [rhi.md](rhi.md) |
| **RF3.3** | Batch Renderer | 50K sprites → 1 draw call | [batch-renderer.md](batch-renderer.md) |
| **RF3.4** | Persistent Mapped Buffers | CPU↔GPU zero memcpy por frame | [batch-renderer.md](batch-renderer.md) |
| **RF3.5** | Radix Sort Layer Sorting | Ordenação por layer + texture + depth | [batch-renderer.md](batch-renderer.md) |
| **RF3.6** | Texture Atlas | Bin-packing, UV mapping correto | [batch-renderer.md](batch-renderer.md) |
| **RF3.7** | Camera 2D/3D | Projeção orto e perspectiva | [camera.md](camera.md) |
| **RF3.8** | Asset Manager async | Loading em background job | [asset-manager.md](asset-manager.md) |
| **RF3.9** | Hot-reload | Textures/shaders recarregáveis em runtime | [asset-manager.md](asset-manager.md) |

---

## 🏗️ Pipeline de Renderização

```
ECS World (Fase 4)
    │
    ├── SpriteSystem.collect()
    │     └── todos os Sprite + Transform components
    │
    ▼
BatchRenderer
    ├── Group por: pipeline + texture + layer
    ├── Sort por: sortKey (Layer → Texture ID → Depth)  ← Radix Sort
    └── Flush para RHI Command Buffer
          │
          ▼
    RHI::CommandBuffer
          ├── bindPipeline()
          ├── bindVertexBuffer()  ← Persistent Mapped Buffer
          └── drawInstanced()     ← 1 call por batch

Camera2D
    └── viewProjectionMatrix()   ← passada ao shader
```

---

## 📁 Arquivos a Criar

```
src/
├── rhi/
│   ├── RenderDevice.hpp       # RF3.1-3.2 — abstração SDL_GPU
│   └── CommandBuffer.hpp      # DrawCommand queue
├── render/
│   ├── BatchRenderer.hpp      # RF3.3-3.5 — sprite batching
│   ├── TextureAtlas.hpp       # RF3.6 — bin-packing
│   └── Camera2D.hpp           # RF3.7 — ortho camera
├── assets/
│   ├── AssetManager.hpp       # RF3.8-3.9 — async loading
│   └── AssetHandle.hpp        # Handle tipado
└── core/io/
    └── CafTypes.hpp           # RF3.0 — header do formato .caf
```

---

## 🔗 Critério de Progresso

**Stress test:** 50K sprites na tela a **60fps estável**, 1 draw call verificado no GPU profiler (< 2ms GPU time).

---

## 🔗 Dependências

| Depende de | Fornece para |
|------------|-------------|
| [Fase 2 — Job System](../fase2/job-system.md) | [Fase 4 — ECS](../fase4/README.md) |
| [Fase 1 — Math](../math/vectors.md) | Sprites, Câmera, Física |
| `SDL3::SDL_GPU` | Toda renderização |

---

## 📚 Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §7 Asset Manager, §8 RHI, §9 Batch Renderer, §10 Camera
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [SDL_GPU API Reference](https://wiki.libsdl.org/SDL3/CategoryGPU)
