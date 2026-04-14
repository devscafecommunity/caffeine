# ☕ Caffeine Engine

**A high-performance, custom-built game engine for the Codex Studio.**

Caffeine é uma engine de jogos desenvolvida em **C++** sobre a camada do **SDL3**. O foco do projeto é o controle total sobre o hardware, priorizando concorrência multithread, gerenciamento de memória customizado e otimização gráfica de baixo nível.

---

## 📚 Documentação

| Documento | Descrição | Link |
|-----------|-----------|------|
| **MASTER.md** | Documentação unificada completa | [`docs/MASTER.md`](docs/MASTER.md) |
| **ROADMAP.md** | Roadmap das 6 fases de desenvolvimento | [`docs/ROADMAP.md`](docs/ROADMAP.md) |
| **SPECS.md** | Regras e padrões de desenvolvimento | [`docs/SPECS.md`](docs/SPECS.md) |
| **memory_model.md** | Especificações de memória | [`docs/memory_model.md`](docs/memory_model.md) |
| **architecture_specs.md** | Especificações técnicas | [`docs/architecture_specs.md`](docs/architecture_specs.md) |

### Documentação por Módulo

| Módulo | Arquitetura | API Reference |
|--------|-------------|---------------|
| **Core** | [`docs/architecture/core.md`](docs/architecture/core.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Memory** | [`docs/architecture/memory.md`](docs/architecture/memory.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Containers** | [`docs/containers/vector.md`](docs/containers/vector.md) | [`docs/api/README.md`](docs/api/README.md) |
| **Math** | [`docs/math/vectors.md`](docs/math/vectors.md) | [`docs/api/README.md`](docs/api/README.md) |

### Status das Fases

```
Fase 0: Setup & Docs       █████████████████ 100% ✅ COMPLETO
Fase 1: Fundação Atômica   █████████████████ 100% ✅ COMPLETO
Fase 2: Concorrência        ░░░░░░░░░░░░░░░  0%
Fase 3: RHI & 2D            ░░░░░░░░░░░░░░░  0%
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

