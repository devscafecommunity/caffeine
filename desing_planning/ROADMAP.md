# 🗺️ Roadmap de Desenvolvimento

Visão executiva das 6 fases do projeto. Para **detalhes técnicos completos** de cada fase (arquitetura, arquivos, critérios de progresso), consulte [`docs/MASTER.md`](../docs/MASTER.md) §4.

---

## 📊 Status Geral

```
Fase 0: Setup & Docs       ████████░░░░░░░░  60%  ← ATUAL
Fase 1: Fundação Atômica   ░░░░░░░░░░░░░░░░  0%
Fase 2: Concorrência        ░░░░░░░░░░░░░░░░  0%
Fase 3: RHI & 2D            ░░░░░░░░░░░░░░░░  0%
Fase 4: ECS & Serialização  ░░░░░░░░░░░░░░░░  0%
Fase 5: Transição 3D          ░░░░░░░░░░░░░░░░  0%
Fase 6: Caffeine Studio IDE  ░░░░░░░░░░░░░░░░  0%
```

---

## 🔧 Fase 0: Setup Inicial & Documentação

**Responsável:** Guilda Codex  
**Status:** 🕒 Em Progresso

| Tarefa | Status |
|---|:---:|
| README.md criado | ✅ |
| Manifesto de desenvolvimento criado | ✅ |
| Roadmap de 6 fases documentado | ✅ |
| Fluxo R.I.C.O. estabelecido | ✅ |
| `docs/MASTER.md` criado | ✅ |
| Estrutura de diretórios `/src` planejada | 🔄 |
| `.gitignore` verificado | ⏳ |
| CMakeLists.txt base criado | ⏳ |
| `Caffeine.h` com tipos básicos criado | ⏳ |

### Próximos Marcos
1. Criar `/src/core/Types.hpp` com tipos (`u32`, `f64`, etc.)
2. Criar `CMakeLists.txt` base
3. Criar `.gitignore` completo (atual está incompleto)

---

## 🧱 Fase 1: Fundação Atômica

**Responsável:** Architects  
**Status:** 📅 Planejado

> Criar independência total da `std` e garantir controle absoluto do hardware.

### Entregáveis

```
src/
├── Caffeine.hpp                  # Header principal de inclusão
├── core/
│   ├── Types.hpp                # u32, f64, etc. + static_assert
│   ├── Platform.hpp             # Macros de plataforma
│   ├── Assertions.hpp           # CF_ASSERT, CF_UNREACHABLE
│   └── Compiler.hpp             # Macros de compilador
├── memory/
│   ├── Allocator.hpp            # Interface base IAllocator
│   ├── LinearAllocator.hpp      # O(1), reset()
│   ├── PoolAllocator.hpp        # Slots fixos, O(1) amortizado
│   └── StackAllocator.hpp       # Marcadores, O(1)
└── containers/
    ├── Vector.hpp               # Array dinâmico cache-friendly
    ├── HashMap.hpp              # Tabela hash O(1)
    ├── StringView.hpp           # String sem ownership
    └── FixedString.hpp          # Buffer inline, zero heap
```

### Critério de Progresso
**Stress test:** 1M allocs → zero memory leaks, fragmentação < 0.1%.

---

## ⚡ Fase 2: O Pulso e a Concorrência

**Responsável:** Architects  
**Status:** 📅 Planejado

> Utilizar todos os núcleos da CPU e manter clock estável.

### Entregáveis

| Componente | Descrição |
|---|---|
| **High-Resolution Timer** | Precisão de microssegundos, `TimePoint`, `Duration` |
| **Job System** | Thread pool com workers, fila lock-free, `JobHandle` |
| **Master Game Loop** | Fixed timestep (lógica) + variable (render), interpolation |

### Critério de Progresso
**Physics demo:** 10K partículas, todos os núcleos a 80%+ carga, `tsan` clean.

---

## 👁️ Fase 3: O Olho da Engine

**Responsável:** Artisans / Architects  
**Status:** 📅 Planejado

> Construir a camada de renderização agnóstica.

### Entregáveis

| Componente | Descrição |
|---|---|
| **RHI** | Abstração sobre SDL_GPU, `DrawCommand` queue |
| **Batch Renderer** | 50K sprites → 1 draw call |
| **Camera System** | Matriz 4×4, ortográfica (2D) / perspectiva (3D) |

### Critério de Progresso
**Demo:** 50K sprites na tela a **60fps estável**.

---

## 🧠 Fase 4: O Cérebro

**Responsável:** Architects  
**Status:** 📅 Planejado

> ECS, serialização e comunicação desacoplada.

### Entregáveis

| Componente | Descrição |
|---|---|
| **ECS** | Entidades = IDs, componentes em arrays contíguos, archetype-based |
| **Scene Graph** | Hierarquia, `.caf` (binário), JSON para debug |
| **Event Bus** | Pub/sub sem acoplamento, `Event<T>` typed |

### Critério de Progresso
**Demo:** 100 entidades dinâmicas, 5+ sistemas, serialização end-to-end.

---

## 🌐 Fase 5: Transição Dimensional

**Responsável:** Artisans  
**Status:** 📅 Planejado

> Expansão para 3D sem quebrar o 2D existente.

### Entregáveis

| Componente | Descrição |
|---|---|
| **3D Math** | Quaternions, matrizes 3D, SIMD hints |
| **Mesh Loading** | `.obj`, `.gltf`, shaders HLSL/GLSL |
| **Spatial Partitioning** | Quadtree → Octree, frustum culling |

### Critério de Progresso
**Demo:** Mesh 3D carregada, shader customizado, **60fps**.

---

## 🏛️ Fase 6: O Olimpo

**Responsável:** Full Guild  
**Status:** 📅 Planejado

> Transformar a engine em ferramenta visual.

### Entregáveis

| Componente | Descrição |
|---|---|
| **Embedded UI** | Dear ImGui, profiler, console |
| **Scene Editor** | Drag-and-drop, inspector, hierarchy |
| **Asset Pipeline** | Processador de assets → `.caf` bundles |

### Critério de Progresso
**Primeiro game completo** feito 100% na Caffeine.

---

## 🚦 Gates Entre Fases

**Regra:** Não avançamos para a Fase N+1 enquanto a Fase N não passar no Stress Test.

```
Fase N ──[Stress Test]──▶ Passou? ──▶ SIM ──▶ Fase N+1
                          │
                          └──▶ NÃO ──▶ Corrigir ──▶ Repete
```

Stress tests por fase:

| Fase | Teste |
|---|---|
| **1** | 1M allocs, 0 leaks, <0.1% fragmentação |
| **2** | 10K partículas, tsan/asan clean, 80%+ CPU |
| **3** | 50K sprites, 60fps, 1 draw call |
| **4** | 100 entidades, save/load round-trip |
| **5** | Mesh 3D, 60fps, shader hot-reload |
| **6** | Game completo do início ao fim |

---

## 📈 Evolução de Versão

| Fase | Versão | Significado |
|---|---|---|
| 0 | `0.0.x` | Pré-alpha, documentação |
| 1-2 | `0.x.x` | Alpha, engine base usável para protótipos |
| 3-4 | `0.x.x` | Beta, games 2D funcionais possíveis |
| 5 | `0.x.x` → `1.0.0` | Feature-complete, API congelando |
| 6 | `1.0.0+` | Stable, primeiro game profissional |

---

> *"Grandes jogos são construídos com código forte e muito café."*
