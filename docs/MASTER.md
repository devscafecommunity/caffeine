# ☕ Caffeine Engine — Documentação Mestre

**Versão:** 1.1.0  
**Status:** Alpha (Pré-produção)  
**Última Atualização:** 2026-04-14  
**Mantido por:** Codex Studio Guild

---

## Índice Geral

### 📚 Guias Principal

| Documento | Descrição | Link |
|-----------|-----------|------|
| **MASTER.md** (este) | Documentação unificada completa | — |
| **ROADMAP.md** | Roadmap das 6 fases de desenvolvimento | [`docs/ROADMAP.md`](docs/ROADMAP.md) |
| **SPECS.md** | Regras e padrões de desenvolvimento | [`docs/SPECS.md`](docs/SPECS.md) |
| **architecture_specs.md** | Especificações técnicas detalhadas | [`docs/architecture_specs.md`](docs/architecture_specs.md) |

---

## 🗂️ Estrutura de Documentação

```
docs/
├── README.md                 # Este índice (redireciona para MASTER.md)
├── MASTER.md                 # Documentação unificada (este arquivo)
├── ROADMAP.md                # Roadmap de desenvolvimento
├── SPECS.md                  # Regras e padrões
├── memory_model.md           # Especificações de memória
├── architecture_specs.md    # Especificações técnicas
├── architecture/
│   ├── core.md              # 🔧 Módulo Core
│   └── memory.md            # 🔧 Módulo Memory
├── containers/
│   └── vector.md            # 🔧 Módulo Containers
├── math/
│   └── vectors.md           # 🔧 Módulo Math
├── api/
│   └── README.md            # 📖 Referência da API
├── fase2/                    # 📐 Documentação Fase 2 — Concorrência
│   ├── README.md
│   ├── timer.md
│   ├── job-system.md
│   ├── game-loop.md
│   ├── input.md
│   └── debug.md
├── fase3/                    # 📐 Documentação Fase 3 — RHI & 2D
│   ├── README.md
│   ├── rhi.md
│   ├── batch-renderer.md
│   ├── camera.md
│   ├── asset-manager.md
│   └── caf-format.md
├── fase4/                    # 📐 Documentação Fase 4 — ECS & Sistemas
│   ├── README.md
│   ├── ecs.md
│   ├── scene.md
│   ├── events.md
│   ├── audio.md
│   ├── animation.md
│   ├── physics.md
│   └── ui.md
├── fase5/                    # 📐 Documentação Fase 5 — Transição 3D
│   ├── README.md
│   ├── 3d-math.md
│   ├── mesh-loading.md
│   ├── spatial-partitioning.md
│   ├── camera3d.md
│   └── skeletal-animation.md
├── fase6/                    # 📐 Documentação Fase 6 — Caffeine Studio IDE
│   ├── README.md
│   ├── embedded-ui.md
│   ├── scene-editor.md
│   ├── asset-pipeline.md
│   └── scripting.md
└── reviews/
    └── PHASE1_REVIEW.md     # 📝 Reviews
```

---

## 🎯 Status das Fases

```
Fase 0: Setup & Docs       █████████████████ 100% ✅ COMPLETO
Fase 1: Fundação Atômica   █████████████████ 100% ✅ COMPLETO
Fase 2: Concorrência        ░░░░░░░░░░░░░░░  0%
Fase 3: RHI & 2D            ░░░░░░░░░░░░░░░  0%
Fase 4: ECS & Sistemas      ░░░░░░░░░░░░░░░  0%
Fase 5: Transição 3D         ░░░░░░░░░░░░░░░  0%
Fase 6: Caffeine Studio IDE  ░░░░░░░░░░░░░░░  0%
```

> Para detalhes completos, consulte [`ROADMAP.md`](docs/ROADMAP.md).

---

## 📐 Documentação por Fase (Pré-Implementação)

### Fase 2 — Concorrência & Core Runtime

| Módulo | Arquivo | Namespace | RFs |
|--------|---------|-----------|-----|
| **Overview** | [`fase2/README.md`](fase2/README.md) | — | RF2.1–RF2.10 |
| **Timer** | [`fase2/timer.md`](fase2/timer.md) | `Caffeine::Core` | RF2.1 |
| **Job System** | [`fase2/job-system.md`](fase2/job-system.md) | `Caffeine::Threading` | RF2.2, RF2.3 |
| **Game Loop** | [`fase2/game-loop.md`](fase2/game-loop.md) | `Caffeine::Core` | RF2.4, RF2.5 |
| **Input** | [`fase2/input.md`](fase2/input.md) | `Caffeine::Input` | RF2.6, RF2.7 |
| **Debug Tools** | [`fase2/debug.md`](fase2/debug.md) | `Caffeine::Debug` | RF2.8–RF2.10 |

### Fase 3 — RHI & Rendering 2D

| Módulo | Arquivo | Namespace | RFs |
|--------|---------|-----------|-----|
| **Overview** | [`fase3/README.md`](fase3/README.md) | — | RF3.0–RF3.9 |
| **RHI** | [`fase3/rhi.md`](fase3/rhi.md) | `Caffeine::RHI` | RF3.1, RF3.2 |
| **Batch Renderer** | [`fase3/batch-renderer.md`](fase3/batch-renderer.md) | `Caffeine::Render` | RF3.3, RF3.4 |
| **Camera 2D** | [`fase3/camera.md`](fase3/camera.md) | `Caffeine::Render` | RF3.5 |
| **Asset Manager** | [`fase3/asset-manager.md`](fase3/asset-manager.md) | `Caffeine::Assets` | RF3.6, RF3.7 |
| **CAF Format** | [`fase3/caf-format.md`](fase3/caf-format.md) | `Caffeine::Assets` | RF3.8, RF3.9 |

### Fase 4 — ECS & Sistemas de Jogo

| Módulo | Arquivo | Namespace | RFs |
|--------|---------|-----------|-----|
| **Overview** | [`fase4/README.md`](fase4/README.md) | — | RF4.1–RF4.11 |
| **ECS** | [`fase4/ecs.md`](fase4/ecs.md) | `Caffeine::ECS` | RF4.1, RF4.2 |
| **Scene** | [`fase4/scene.md`](fase4/scene.md) | `Caffeine::Scene` | RF4.3, RF4.4 |
| **Events** | [`fase4/events.md`](fase4/events.md) | `Caffeine::Events` | RF4.5 |
| **Audio** | [`fase4/audio.md`](fase4/audio.md) | `Caffeine::Audio` | RF4.6 |
| **Animation** | [`fase4/animation.md`](fase4/animation.md) | `Caffeine::Animation` | RF4.7 |
| **Physics 2D** | [`fase4/physics.md`](fase4/physics.md) | `Caffeine::Physics2D` | RF4.8, RF4.9 |
| **UI System** | [`fase4/ui.md`](fase4/ui.md) | `Caffeine::UI` | RF4.10, RF4.11 |

### Fase 5 — Transição Dimensional (3D)

| Módulo | Arquivo | Namespace | RFs |
|--------|---------|-----------|-----|
| **Overview** | [`fase5/README.md`](fase5/README.md) | — | RF5.1–RF5.6 |
| **3D Math** | [`fase5/3d-math.md`](fase5/3d-math.md) | `Caffeine::Math` | RF5.1 |
| **Mesh Loading** | [`fase5/mesh-loading.md`](fase5/mesh-loading.md) | `Caffeine::Assets` | RF5.2, RF5.3 |
| **Spatial Partitioning** | [`fase5/spatial-partitioning.md`](fase5/spatial-partitioning.md) | `Caffeine::Spatial` | RF5.4, RF5.6 |
| **Camera 3D** | [`fase5/camera3d.md`](fase5/camera3d.md) | `Caffeine::Render` | RF5.5 |
| **Skeletal Animation** | [`fase5/skeletal-animation.md`](fase5/skeletal-animation.md) | `Caffeine::Animation` | RF5.7 |

### Fase 6 — Caffeine Studio IDE

| Módulo | Arquivo | Namespace | RFs |
|--------|---------|-----------|-----|
| **Overview** | [`fase6/README.md`](fase6/README.md) | — | RF6.1–RF6.6 |
| **Embedded UI** | [`fase6/embedded-ui.md`](fase6/embedded-ui.md) | `Caffeine::Editor` | RF6.1, RF6.2, RF6.6 |
| **Scene Editor** | [`fase6/scene-editor.md`](fase6/scene-editor.md) | `Caffeine::Editor` | RF6.3, RF6.4 |
| **Asset Pipeline** | [`fase6/asset-pipeline.md`](fase6/asset-pipeline.md) | `Caffeine::Tools` | RF6.5 |
| **Scripting** | [`fase6/scripting.md`](fase6/scripting.md) | `Caffeine::Script` | RF6.6 |

---

## 🏗️ Módulos Implementados (Fase 1)

### Estrutura de Arquivos vs Documentação

| Módulo | Arquivo Header | Documentação Arquitetura | Referência API |
|--------|---------------|-------------------------|----------------|
| **Core** | [`src/core/Types.hpp`](src/core/Types.hpp) | [`docs/architecture/core.md`](docs/architecture/core.md) | §1.1 |
| | [`src/core/Platform.hpp`](src/core/Platform.hpp) | | |
| | [`src/core/Compiler.hpp`](src/core/Compiler.hpp) | | |
| | [`src/core/Assertions.hpp`](src/core/Assertions.hpp) | | |
| **Memory** | [`src/memory/Allocator.hpp`](src/memory/Allocator.hpp) | [`docs/architecture/memory.md`](docs/architecture/memory.md) | §1.2 |
| | [`src/memory/LinearAllocator.hpp`](src/memory/LinearAllocator.hpp) | [`docs/memory_model.md`](docs/memory_model.md) | |
| | [`src/memory/PoolAllocator.hpp`](src/memory/PoolAllocator.hpp) | | |
| | [`src/memory/StackAllocator.hpp`](src/memory/StackAllocator.hpp) | | |
| **Containers** | [`src/containers/Vector.hpp`](src/containers/Vector.hpp) | [`docs/containers/vector.md`](docs/containers/vector.md) | §1.3 |
| | [`src/containers/HashMap.hpp`](src/containers/HashMap.hpp) | | |
| | [`src/containers/StringView.hpp`](src/containers/StringView.hpp) | | |
| | [`src/containers/FixedString.hpp`](src/containers/FixedString.hpp) | | |
| **Math** | [`src/math/Vec2.hpp`](src/math/Vec2.hpp) | [`docs/math/vectors.md`](docs/math/vectors.md) | §1.4 |
| | [`src/math/Vec3.hpp`](src/math/Vec3.hpp) | | |
| | [`src/math/Vec4.hpp`](src/math/Vec4.hpp) | | |
| | [`src/math/Mat4.hpp`](src/math/Mat4.hpp) | | |
| | [`src/math/Math.hpp`](src/math/Math.hpp) | | |

---

## 📖 Navegação por Seção

### 1. Visão Geral & Filosofia

- [**ROADMAP.md**](docs/ROADMAP.md) — Roadmap completo das 6 fases
- [**SPECS.md**](docs/SPECS.md) — Regras e padrões de desenvolvimento

### 2. Arquitetura & Especificações

- [**architecture_specs.md**](docs/architecture_specs.md) — Especificações técnicas detalhadas
- [**memory_model.md**](docs/memory_model.md) — Modelo de memória completo

### 3. Documentação por Módulo

| Módulo | Arquitetura | Containers | API |
|--------|-------------|------------|-----|
| **Core** | [`architecture/core.md`](docs/architecture/core.md) | — | [`api/README.md`](docs/api/README.md) |
| **Memory** | [`architecture/memory.md`](docs/architecture/memory.md) | — | [`api/README.md`](docs/api/README.md) |
| **Containers** | [`containers/vector.md`](docs/containers/vector.md) | [`containers/vector.md`](docs/containers/vector.md) | [`api/README.md`](docs/api/README.md) |
| **Math** | [`math/vectors.md`](docs/math/vectors.md) | [`math/vectors.md`](docs/math/vectors.md) | [`api/README.md`](docs/api/README.md) |

### 4. Testes

Os testes estão localizados em [`tests/`](../tests/) e cobrem:

| Arquivo | Cobertura |
|---------|-----------|
| [`tests/test_core.cpp`](../tests/test_core.cpp) | 8 testes (Types, Platform, Compiler, Assertions) |
| [`tests/test_allocators.cpp`](../tests/test_allocators.cpp) | 16 testes + 3 stress tests |
| [`tests/test_containers.cpp`](../tests/test_containers.cpp) | 14 testes + 1 stress test |
| [`tests/test_math.cpp`](../tests/test_math.cpp) | 18 testes |

---

## 🔗 Cross-Reference Index

### Dependências entre Módulos

```
Caffeine.hpp
    │
    ├── Core (types, platform, assertions)
    │       └── Dependency: NONE (base)
    │
    ├── Memory (allocators)
    │       └── Dependency: Core
    │
    ├── Containers (Vector, HashMap, String)
    │       └── Dependency: Core, Memory (optional)
    │
    ├── Math (Vec2, Vec3, Vec4, Mat4, Quat)
    │       └── Dependency: Core
    │
    │── [Fase 2] Threading, Core Runtime
    │       ├── Timer          → Core
    │       ├── Job System     → Core, Threading
    │       ├── Game Loop      → Timer, Job System
    │       ├── Input          → Core, Events (Fase 4)
    │       └── Debug Tools    → Core, Containers
    │
    ├── [Fase 3] RHI & Rendering
    │       ├── RHI            → Core, Math
    │       ├── Batch Renderer → RHI, Math, Containers
    │       ├── Camera 2D      → Math
    │       ├── Asset Manager  → Core, Containers, Threading
    │       └── CAF Format     → Asset Manager
    │
    ├── [Fase 4] ECS & Game Systems
    │       ├── ECS            → Core, Memory, Containers
    │       ├── Scene Manager  → ECS, Asset Manager
    │       ├── Events         → Core, Containers
    │       ├── Audio          → Asset Manager, ECS
    │       ├── Animation      → ECS, Asset Manager
    │       ├── Physics 2D     → ECS, Math
    │       └── UI System      → RHI, Input, Events
    │
    ├── [Fase 5] 3D Extension
    │       ├── 3D Math        → Math (Vec3, Mat4, Quat)
    │       ├── Mesh Loading   → Asset Manager, RHI
    │       ├── Spatial / Octree → Math, Physics 2D
    │       ├── Camera 3D      → 3D Math
    │       └── Skeletal Anim  → Animation, ECS, 3D Math
    │
    └── [Fase 6] Caffeine Studio IDE
            ├── Embedded UI    → RHI, Debug Tools, Asset Manager
            ├── Scene Editor   → ECS, Scene, Embedded UI, RHI
            ├── Asset Pipeline → Asset Manager, CAF Format (CLI tool)
            └── Scripting      → ECS, Input, Events, Debug Tools
```

### Links Rápidos por Tópico

| Tópico | Documentação Principal |
|--------|------------------------|
| **Tipos fundamentais** | [`src/core/Types.hpp`](src/core/Types.hpp) + [`docs/architecture/core.md`](docs/architecture/core.md) |
| **Allocators** | [`docs/memory_model.md`](docs/memory_model.md) + [`docs/architecture/memory.md`](docs/architecture/memory.md) |
| **Vector<T>** | [`src/containers/Vector.hpp`](src/containers/Vector.hpp) + [`docs/containers/vector.md`](docs/containers/vector.md) |
| **HashMap<K,V>** | [`src/containers/HashMap.hpp`](src/containers/HashMap.hpp) + [`docs/containers/vector.md`](docs/containers/vector.md) |
| **Vec2/Vec3/Vec4** | [`src/math/Vec2.hpp`](src/math/Vec2.hpp) + [`docs/math/vectors.md`](docs/math/vectors.md) |
| **Mat4** | [`src/math/Mat4.hpp`](src/math/Mat4.hpp) + [`docs/math/vectors.md`](docs/math/vectors.md) |
| **Timer** | [`docs/fase2/timer.md`](docs/fase2/timer.md) |
| **Game Loop** | [`docs/fase2/game-loop.md`](docs/fase2/game-loop.md) |
| **Job System** | [`docs/fase2/job-system.md`](docs/fase2/job-system.md) |
| **Input** | [`docs/fase2/input.md`](docs/fase2/input.md) |
| **Debug / Profiler** | [`docs/fase2/debug.md`](docs/fase2/debug.md) |
| **RHI** | [`docs/fase3/rhi.md`](docs/fase3/rhi.md) |
| **Batch Renderer** | [`docs/fase3/batch-renderer.md`](docs/fase3/batch-renderer.md) |
| **Asset Manager** | [`docs/fase3/asset-manager.md`](docs/fase3/asset-manager.md) |
| **CAF Format** | [`docs/fase3/caf-format.md`](docs/fase3/caf-format.md) |
| **ECS** | [`docs/fase4/ecs.md`](docs/fase4/ecs.md) |
| **Scene Manager** | [`docs/fase4/scene.md`](docs/fase4/scene.md) |
| **Events** | [`docs/fase4/events.md`](docs/fase4/events.md) |
| **Audio** | [`docs/fase4/audio.md`](docs/fase4/audio.md) |
| **Animation** | [`docs/fase4/animation.md`](docs/fase4/animation.md) |
| **Physics 2D** | [`docs/fase4/physics.md`](docs/fase4/physics.md) |
| **UI (game HUD)** | [`docs/fase4/ui.md`](docs/fase4/ui.md) |
| **Quaternions / 3D Math** | [`docs/fase5/3d-math.md`](docs/fase5/3d-math.md) |
| **Mesh Loading** | [`docs/fase5/mesh-loading.md`](docs/fase5/mesh-loading.md) |
| **Spatial / Octree** | [`docs/fase5/spatial-partitioning.md`](docs/fase5/spatial-partitioning.md) |
| **Camera 3D** | [`docs/fase5/camera3d.md`](docs/fase5/camera3d.md) |
| **Skeletal Animation** | [`docs/fase5/skeletal-animation.md`](docs/fase5/skeletal-animation.md) |
| **Embedded ImGui** | [`docs/fase6/embedded-ui.md`](docs/fase6/embedded-ui.md) |
| **Scene Editor** | [`docs/fase6/scene-editor.md`](docs/fase6/scene-editor.md) |
| **Asset Pipeline CLI** | [`docs/fase6/asset-pipeline.md`](docs/fase6/asset-pipeline.md) |
| **Scripting (Lua)** | [`docs/fase6/scripting.md`](docs/fase6/scripting.md) |

---

## 🧭 Guia de Contribuição

### Para Arquitetos (Implementação)

1. **Consultar ROADMAP:** Verificar статус da fase em [`ROADMAP.md`](docs/ROADMAP.md)
2. **Consultar SPECS:** Ler regras em [`SPECS.md`](docs/SPECS.md)
3. **Implementar:** Criar código em `src/`
4. **Documentar:** Atualizar documentação correspondente
5. **Testar:** Garantir que testes passem em CI
6. **Code Review:** Pull Request com aprovação

### Para Scribes (Documentação)

1. **Antes de codificar:** Spec deve existir em `docs/faseN/` (ex: `docs/fase2/job-system.md`)
2. **Sincronia:** Código e docs no mesmo commit
3. **Clareza:** Se explicação > código, simplificar
4. **AGNOSTICISM:** Toda spec deve prever 2D e 3D

> Checklist completo: [`SPECS.md`](docs/SPECS.md) §Checklist de Documentação

---

## 📋 Referências Externas

### Documentação
- [SDL3 Documentation](https://wiki.libsdl.org/)
- [SDL_GPU API](https://github.com/libsdl-org/SDL_gpu/)
- [C++20 Standard](https://en.cppreference.com/)

### Livros e Conceitos
- [Data-Oriented Design — Richard Fabian](https://www.dataorienteddesign.com/)
- [Game Programming Patterns — Robert Nystrom](https://gameprogrammingpatterns.com/)
- [Game Engine Architecture — Jason Gregory](https://www.gameenginebook.com/)

### Engines de Referência
- [flecs](https://github.com/SanderMertens/flecs) — ECS archetype-based
- [EnTT](https://github.com/skypjack/entt) — ECS patterns
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — Job System

---

## 📦 Header de Inclusão Principal

```cpp
#include <Caffeine.hpp>

// Agora disponíveis:
// - Tipos: Caffeine::u32, Caffeine::f32, etc.
// - Containers: Caffeine::Vector<T>, Caffeine::HashMap<K,V>, etc.
// - Math: Caffeine::Vec2, Caffeine::Mat4, etc.
// - Memory: Caffeine::LinearAllocator, etc.
```

---

> ☕ *Caffeine: Because great games are built on strong code and a lot of coffee.*