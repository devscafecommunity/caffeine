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

---

## 🏛️ Filosofia de Desenvolvimento
Ao contrário de engines "bloated", a Caffeine é construída sob o princípio da **transparência**.
* **Zero Dependency:** Reduzir ao máximo a dependência da `std` padrão, criando uma `stdlib` própria focada em jogos.
* **Data-Oriented:** Foco em performance de cache e processamento paralelo.
* **Caffeine-Powered:** Feita por desenvolvedores que preferem entender cada byte que passa pela CPU.
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

