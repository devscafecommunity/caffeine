# ☕ Caffeine Engine — Documentação Mestre

**Versão:** 1.1.0  
**Status:** Alpha (Pré-produção)  
**Última Atualização:** 2026-04-06  
**Mantido por:** Codex Studio Guild

---

## Índice

1. [Visão Geral](#1-visão-geral)
2. [Filosofia & Princípios](#2-filosofia--princípios)
3. [Arquitetura do Sistema](#3-arquitetura-do-sistema)
4. [Fases de Desenvolvimento](#4-fases-de-desenvolvimento)
5. [Convenções de Código](#5-convenções-de-código)
6. [Fluxo de Trabalho (R.I.C.O.)](#6-fluxo-de-trabalho-rico)
7. [Estrutura de Diretórios](#7-estrutura-de-diretórios)
8. [Gestão de Memória](#8-gestão-de-memória)
9. [Módulos da Engine](#9-módulos-da-engine)
10. [Estratégia de Crescimento](#10-estratégia-de-crescimento)
11. [Documentação Técnica](#11-documentação-técnica)
12. [Como Contribuir](#12-como-contribuir)

---

## 11. Documentação Técnica

### Fase 1: Fundação Atômica (IMPLEMENTADA)

A documentação técnica da Fase 1 está organizada em subpastas com referências cruzadas:

| Módulo | Arquitetura | API |
|--------|-------------|-----|
| **Core** | [`architecture/core.md`](architecture/core.md) | Tipos, Platform, Assertions |
| **Memory** | [`architecture/memory.md`](architecture/memory.md) | Linear, Pool, Stack allocators |
| **Containers** | [`containers/vector.md`](containers/vector.md) | Vector, HashMap, String |
| **Math** | [`math/vectors.md`](math/vectors.md) | Vec2, Vec3, Vec4, Mat4 |

**Referências Cruzadas:**

```
Core ──────► Memory ──────► Containers ──────► Math
  │             │                 │              │
  └─────────────┴─────────────────┴────────────┘
                          │
                     [API Reference](api/README.md)
```

### Navegação Rápida

- [API Reference](api/README.md) - Referência completa da API
- [Core Module](architecture/core.md) - Tipos fundamentais
- [Memory Module](architecture/memory.md) - Allocators customizados
- [Containers Module](containers/vector.md) - Estruturas de dados
- [Math Module](math/vectors.md) - Vetores e matrizes

### Testes

Os testes estão localizados em [`tests/`](../tests/) e cobrem:

- `test_core.cpp` - 8 testes (Types, Platform, Compiler, Assertions)
- `test_allocators.cpp` - 16 testes + 3 stress tests
- `test_containers.cpp` - 14 testes + 1 stress test
- `test_math.cpp` - 18 testes

---

## 12. Como Contribuir

### 11.1 Para Arquitetos (Implementação)

1. **Consultar R.I.C.O.:** Antes de implementar, seguir o ciclo Research → Idea → Conflict → Order
2. **Criar Branch:** `feature/fase-N-nome-da-feature`
3. **Implementar:** Seguir convenções de código (Seção 5)
4. **Code Review:** Pull Request com pelo menos 1 aprovação
5. **Merge:** Squash merge para manter histórico limpo
6. **Documentar:** Spec e código no mesmo commit

### 11.2 Para Scribes (Documentação)

1. **Antes de Codificar:** A spec deve existir em `/desing_planning/`
2. **Sincronia:** Se o código mudou, o doc mudou no mesmo commit
3. **Clareza:** "Se a explicação é mais complexa que o código, o sistema precisa ser simplificado"
4. **AGNOSTICISM:** Toda spec deve prever 2D e 3D

### 11.3 Fluxo Git

```
feature/fase-1-linear-allocator
        │
        ├── Implementa LinearAllocator
        ├── Escreve testes
        ├── Atualiza memory_model.md
        ├── Code Review aprovação
        │
        ▼
      Squash Merge → main
        │
        ▼
    Tag: v0.1.0-alpha
```

### 11.4 Checklist Antes de Commitar

- [ ] Código compila em todas as plataformas
- [ ] Sem `new` ou `delete` soltos
- [ ] Nomenclatura segue o Style Guide
- [ ] Headers têm documentação Doxygen
- [ ] Documentação correspondente atualizada
- [ ] `.gitignore` não vai incluir artefatos de build

---

## Apêndice A: Glossário

| Termo | Definição |
|---|---|
| **RHI** | Rendering Hardware Interface — abstração sobre a API gráfica |
| **ECS** | Entity Component System — arquitetura de dados para jogos |
| **DOD** | Data-Oriented Design — organização de dados para cache locality |
| **YAGNI** | You Ain't Gonna Need It — não implementar o que não é necessário |
| **Lock-free** | Algoritmo que não usa locks, apenas atômicos |
| **Archetype** | Grupo de entidades com o mesmo conjunto de componentes |
| **Draw Call** | Comando enviado à GPU para renderizar geometria |
| **Batch Rendering** | Agrupar múltiplos draw calls em um único |
| **SIMD** | Single Instruction Multiple Data — instruções vetoriais da CPU |
| **Cache Locality** | Padrão de acesso à memória que maximiza hits de cache |
| **Swapchain** | Buffer de frames para double/triple buffering |

---

## Apêndice B: Referências e Recursos

### Documentação
- [SDL3 Documentation](https://wiki.libsdl.org/)
- [SDL_GPU API](https://github.com/libsdl-org/SDL_gpu/)
- [C++20 Standard](https://en.cppreference.com/)

### Livros e Conceitos
- [Data-Oriented Design — Richard Fabian](https://www.dataorienteddesign.com/)
- [Game Programming Patterns — Robert Nystrom](https://gameprogrammingpatterns.com/)
- [Game Engine Architecture — Jason Gregory](https://www.gameenginebook.com/)

### Engines e Bibliotecas de Referência
- [flecs](https://github.com/SanderMertens/flecs) — ECS archetype-based com cache locality
- [EnTT](https://github.com/skypjack/entt) — ECS patterns e integração com game loops
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — Job System com barreiras
- [Box2D/LiquidFun](https://github.com/google/liquidfun) — Física 2D com broad/narrow phase

### Padrões de Game Loop
- [Handmade Hero](https://github.com/cmuratori/HandmadeHeroCode) — Padrões de engine de baixo nível
- [Fixed Timestep Demo](https://github.com/jakubtomsu/fixed-timestep-demo) — Padrão accumulator com interpolação
- [Dear ImGui SDL3](https://github.com/ocornut/imgui/blob/master/examples/example_sdl3_opengl3/main.cpp) — SDL3 game loop integration

### Patterns de Código
- [Endurodave StateMachine](https://github.com/endurodave/StateMachine) — State machine patterns em C++
- [Merrilledmonds GameLoop](https://github.com/merrilledmonds/GameLoop) — Boilerplate game loop modular

---

> ☕ *"Caffeine: Because great games are built on strong code and a lot of coffee."*
