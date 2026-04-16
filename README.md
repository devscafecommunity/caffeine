# ☕ Caffeine Engine

**A high-performance, custom-built game engine for the Codex Studio.**

Caffeine é uma engine de jogos desenvolvida em **C++** sobre a camada do **SDL3**. O foco do projeto é o controle total sobre o hardware, priorizando concorrência multithread, gerenciamento de memória customizado e otimização gráfica de baixo nível.

---

## 📚 Documentação

### Documentos Principais

| Documento | Descrição | Link |
|-----------|-----------|------|
| **MASTER.md** | Documentação unificada completa | [`docs/MASTER.md`](docs/MASTER.md) |
| **ROADMAP.md** | Roadmap das 6 fases de desenvolvimento | [`docs/ROADMAP.md`](docs/ROADMAP.md) |
| **SPECS.md** | Regras e padrões de desenvolvimento | [`docs/SPECS.md`](docs/SPECS.md) |
| **memory_model.md** | Especificações de memória | [`docs/memory_model.md`](docs/memory_model.md) |
| **architecture_specs.md** | Especificações técnicas | [`docs/architecture_specs.md`](docs/architecture_specs.md) |

### Arquitetura & Módulos

| Módulo | Arquitetura | API Reference |
|--------|-------------|---------------|
| **Core** | [`docs/architecture/core.md`](docs/architecture/core.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Memory** | [`docs/architecture/memory.md`](docs/architecture/memory.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Containers** | [`docs/containers/vector.md`](docs/containers/vector.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Math** | [`docs/math/vectors.md`](docs/math/vectors.md) | [`docs/api/README.md`](docs/api/README.md) |

### Documentação por Fase

| Fase | Descrição | Documentação | Status |
|------|-----------|--------------|--------|
| **Fase 0** | Setup & Docs | [`docs/SPECS.md`](docs/SPECS.md) | ✅ COMPLETO |
| **Fase 1** | Fundação Atômica | [`docs/architecture/`](docs/architecture/) | ✅ COMPLETO |
| **Fase 2** | Concorrência | [`docs/fase2/`](docs/fase2/) | 📅 Planejado |
| **Fase 3** | RHI & 2D | [`docs/fase3/`](docs/fase3/) | 📅 Planejado |
| **Fase 4** | ECS & Sistemas | [`docs/fase4/`](docs/fase4/) | 📅 Planejado |
| **Fase 5** | Transição 3D | [`docs/fase5/`](docs/fase5/) | 📅 Futuro Distante |
| **Fase 6** | Caffeine Studio IDE | [`docs/fase6/`](docs/fase6/) | 📅 Planejado |

### Fase 2 — Concorrência & Core Runtime

| Módulo | Arquivo |
|--------|---------|
| **Overview** | [`docs/fase2/README.md`](docs/fase2/README.md) |
| **Timer** | [`docs/fase2/timer.md`](docs/fase2/timer.md) |
| **Job System** | [`docs/fase2/job-system.md`](docs/fase2/job-system.md) |
| **Game Loop** | [`docs/fase2/game-loop.md`](docs/fase2/game-loop.md) |
| **Input** | [`docs/fase2/input.md`](docs/fase2/input.md) |
| **Debug Tools** | [`docs/fase2/debug.md`](docs/fase2/debug.md) |
| **Test System** | [`docs/fase2/testing.md`](docs/fase2/testing.md) |

### Fase 3 — RHI & Rendering 2D

| Módulo | Arquivo |
|--------|---------|
| **Overview** | [`docs/fase3/README.md`](docs/fase3/README.md) |
| **RHI** | [`docs/fase3/rhi.md`](docs/fase3/rhi.md) |
| **Batch Renderer** | [`docs/fase3/batch-renderer.md`](docs/fase3/batch-renderer.md) |
| **Camera 2D** | [`docs/fase3/camera.md`](docs/fase3/camera.md) |
| **Asset Manager** | [`docs/fase3/asset-manager.md`](docs/fase3/asset-manager.md) |
| **CAF Format** | [`docs/fase3/caf-format.md`](docs/fase3/caf-format.md) |

### Fase 4 — ECS & Sistemas de Jogo

| Módulo | Arquivo |
|--------|---------|
| **Overview** | [`docs/fase4/README.md`](docs/fase4/README.md) |
| **ECS** | [`docs/fase4/ecs.md`](docs/fase4/ecs.md) |
| **Scene** | [`docs/fase4/scene.md`](docs/fase4/scene.md) |
| **Events** | [`docs/fase4/events.md`](docs/fase4/events.md) |
| **Audio** | [`docs/fase4/audio.md`](docs/fase4/audio.md) |
| **Animation** | [`docs/fase4/animation.md`](docs/fase4/animation.md) |
| **Physics 2D** | [`docs/fase4/physics.md`](docs/fase4/physics.md) |
| **UI System** | [`docs/fase4/ui.md`](docs/fase4/ui.md) |

### Fase 5 — Transição Dimensional (3D)

| Módulo | Arquivo |
|--------|---------|
| **Overview** | [`docs/fase5/README.md`](docs/fase5/README.md) |
| **3D Math** | [`docs/fase5/3d-math.md`](docs/fase5/3d-math.md) |
| **Mesh Loading** | [`docs/fase5/mesh-loading.md`](docs/fase5/mesh-loading.md) |
| **Spatial Partitioning** | [`docs/fase5/spatial-partitioning.md`](docs/fase5/spatial-partitioning.md) |
| **Camera 3D** | [`docs/fase5/camera3d.md`](docs/fase5/camera3d.md) |
| **Skeletal Animation** | [`docs/fase5/skeletal-animation.md`](docs/fase5/skeletal-animation.md) |

### Fase 6 — Caffeine Studio IDE

| Módulo | Arquivo |
|--------|---------|
| **Overview** | [`docs/fase6/README.md`](docs/fase6/README.md) |
| **Embedded UI** | [`docs/fase6/embedded-ui.md`](docs/fase6/embedded-ui.md) |
| **Scene Editor** | [`docs/fase6/scene-editor.md`](docs/fase6/scene-editor.md) |
| **Asset Pipeline** | [`docs/fase6/asset-pipeline.md`](docs/fase6/asset-pipeline.md) |
| **Scripting** | [`docs/fase6/scripting.md`](docs/fase6/scripting.md) |

### Reviews

| Review | Arquivo |
|--------|---------|
| **Phase 1 Review** | [`docs/reviews/PHASE1_REVIEW.md`](docs/reviews/PHASE1_REVIEW.md) |

### Status das Fases

```
Fase 0: Setup & Docs       █████████████████ 100% ✅ COMPLETO
Fase 1: Fundação Atômica   █████████████████ 100% ✅ COMPLETO
Fase 2: Concorrência       █████████████████  100% ✅ COMPLETO
Fase 3: RHI & 2D            ████░░░░░░░░░░░░  10%
Fase 4: ECS & Sistemas      ░░░░░░░░░░░░░░░  0%
Fase 5: Transição 3D         ░░░░░░░░░░░░░░░  0%
Fase 6: Caffeine Studio IDE  ░░░░░░░░░░░░░░░  0%
```

---

## 🏛️ Filosofia de Desenvolvimento
Ao contrário de engines "bloated", a Caffeine é construída sob o princípio da **transparência**.
* **Zero Dependency:** Reduzir ao máximo a dependência da `std` padrão, criando uma `stdlib` própria focada em jogos.
* **Data-Oriented:** Foco em performance de cache e processamento paralelo.
* **Caffeine-Powered:** Feita por desenvolvedores que preferem entender cada byte que passa pela CPU.

---



## 🛠️ Roadmap Técnico

### 1. Sistema de Concorrência (Threading)
* Implementação de um **Job System** baseado em workers.
* Distribuição de tarefas pesadas (física, animação) entre núcleos da CPU.
* Primitivas de sincronização *lock-free* para evitar gargalos.

### 2. Otimização Gráfica
* Integração profunda com a nova **GPU API do SDL3**.
* Sistema de **Batch Rendering** para redução de Draw Calls.
* Gerenciamento eficiente de VRAM e Texture Streaming.

### 3. Core & Game Loop
* Loop de tempo fixo para lógica/física e variável para renderização.
* Interpolação de frames para garantir fluidez visual (60fps+).
* Sistema de eventos de alta precisão.

### 4. Caffeine Stdlib (Custom Lib)
* **Custom Allocators:** Linear, Pool e Stack allocators para evitar fragmentação de memória.
* **Contêineres Otimizados:** Implementação própria de vetores e strings focados em performance.

---

## 🚀 Como Compilar

*Compilação via CMake com testes GTest.*

**Requisitos:**
- Compilador C++20 ou superior
- CMake 3.20+
- SDL3 (Latest Build)

```bash
git clone https://github.com/codex-studio/caffeine
cd caffeine
mkdir build && cd build
cmake ..
make
```

### Executar Testes

```bash
# Na pasta build
ctest --output-on-failure
```

---

## 📂 Estrutura do Código

```
src/
├── Caffeine.hpp              # Header principal de inclusão
├── core/                     # Tipos fundamentais, platform, assertions
│   ├── Types.hpp            # u32, f64, etc.
│   ├── Platform.hpp         # Macros de plataforma
│   ├── Compiler.hpp         # Macros de compilador
│   └── Assertions.hpp       # CF_ASSERT
├── memory/                   # Allocators customizados
│   ├── Allocator.hpp       # Interface IAllocator
│   ├── LinearAllocator.hpp # O(1) reset
│   ├── PoolAllocator.hpp  # Slots fixos
│   └── StackAllocator.hpp  # Marcadores
├── containers/               # Estruturas de dados
│   ├── Vector.hpp          # Array dinâmico
│   ├── HashMap.hpp         # Tabela hash O(1)
│   ├── StringView.hpp      # String sem ownership
│   └── FixedString.hpp     # Buffer inline
└── math/                     # Tipos matemáticos
    ├── Vec2.hpp, Vec3.hpp, Vec4.hpp
    ├── Mat4.hpp            # Matriz 4×4 column-major
    └── Math.hpp            # Utility functions
```

> Para documentação completa, consulte [`docs/MASTER.md`](docs/MASTER.md)

---

> *“Caffeine: Because great games are built on strong code and a lot of coffee.”*

