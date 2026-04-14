# 🧠 Fase 4: O Cérebro

> **Status:** 📅 Planejado  
> **Responsável:** Architects  
> **Versão alvo:** `0.3.x`

Esta fase implementa o **ECS completo** — a arquitetura de dados central que conecta todos os sistemas. Com ela, é possível criar jogos 2D funcionais completos.

---

## 📋 Índice

| Módulo | Arquivo | Namespace | Fase |
|--------|---------|-----------|------|
| **ECS Core** | [`ecs.md`](ecs.md) | `Caffeine::ECS` | 4 |
| **Scene Manager** | [`scene.md`](scene.md) | `Caffeine::Scene` | 4 |
| **Event Bus** | [`events.md`](events.md) | `Caffeine::Events` | 4 |
| **Audio System** | [`audio.md`](audio.md) | `Caffeine::Audio` | 4 |
| **Animation System** | [`animation.md`](animation.md) | `Caffeine::Animation` | 4 |
| **Physics 2D** | [`physics.md`](physics.md) | `Caffeine::Physics2D` | 4 |
| **UI System** | [`ui.md`](ui.md) | `Caffeine::UI` | 4 |

---

## 🎯 Objetivo da Fase

> "ECS completo, sistemas de gameplay, comunicação desacoplada."

Nesta fase a engine ganha:
- **ECS Archetype-based** — cache-friendly, queries, systems com prioridade
- **Command Buffers diferidos** — criação/destruição segura durante iteração
- **Scene serialization** — salvar/carregar cenas em `.caf` binário
- **Event Bus** — comunicação desacoplada entre sistemas
- **Audio**, **Animation**, **Physics 2D**, **UI** — sistemas completos de gameplay

---

## 📊 Requisitos Funcionais

| ID | Requisito | Critério de Aceitação | Módulo |
|----|-----------|----------------------|--------|
| **RF4.1** | ECS Core (Archetype) | Entities = IDs, Components = arrays contíguos | [ecs.md](ecs.md) |
| **RF4.2** | ComponentPool<T> | Arrays contíguos, grow como Vector | [ecs.md](ecs.md) |
| **RF4.3** | World query system | Query por combinação de componentes | [ecs.md](ecs.md) |
| **RF4.4** | Deferred Command Buffer | Commands diferidos aplicados em safe point | [ecs.md](ecs.md) |
| **RF4.5** | Scene serialization | Save/load .caf formato binário | [scene.md](scene.md) |
| **RF4.6** | Event Bus pub/sub | Event<T> tipado, priority queue | [events.md](events.md) |
| **RF4.7** | Scripting Bindings (early) | Lua/AngelScript bindings básicos | — |
| **RF4.8** | Audio System | SDL3 audio, pooling, spatial 2D | [audio.md](audio.md) |
| **RF4.9** | Animation System | Sprite frames, state machine | [animation.md](animation.md) |
| **RF4.10** | Physics 2D | AABB/circle collision, layers | [physics.md](physics.md) |
| **RF4.11** | UI System (retained) | ECS integration, widget instances | [ui.md](ui.md) |

---

## 🏗️ Arquitetura ECS

```
ECS World
  │
  ├── Archetypes (entidades agrupadas por ComponentSet)
  │     ├── [Position2D + Velocity2D + Sprite]   → Pool contíguo
  │     ├── [Position2D + Velocity2D + RigidBody] → Pool contíguo
  │     └── [UIWidget + RectTransform]             → Pool contíguo
  │
  ├── Systems (executados em ordem de prioridade)
  │     ├── PhysicsSystem2D   (priority = 100)
  │     ├── MovementSystem    (priority = 150)
  │     ├── AnimationSystem   (priority = 200)
  │     ├── UISystem          (priority = 500)
  │     └── RenderSystem      (priority = 1000)
  │
  └── CommandBuffer diferido
        └── Aplicado no fim do update — sem quebrar iteração
```

---

## 📁 Arquivos a Criar

```
src/
├── ecs/
│   ├── World.hpp              # RF4.1-4.4 — ECS central
│   ├── Entity.hpp             # Handle de entidade
│   ├── ComponentQuery.hpp     # Query builder
│   └── ISystem.hpp            # Interface de sistemas
├── scene/
│   ├── SceneManager.hpp       # RF4.5 — Transições e stack
│   └── SceneSerializer.hpp    # Serialização .caf
├── events/
│   └── EventBus.hpp           # RF4.6 — Pub/sub tipado
├── audio/
│   └── AudioSystem.hpp        # RF4.8 — SDL3 audio
├── animation/
│   └── AnimationSystem.hpp    # RF4.9 — State machine
├── physics/
│   └── PhysicsSystem2D.hpp    # RF4.10 — AABB/circle
└── ui/
    └── UISystem.hpp            # RF4.11 — Retained mode
```

---

## 🔗 Critério de Progresso

**Demo:** 100 entidades dinâmicas com 5+ sistemas rodando, serialização end-to-end (save/load < 200ms).

---

## 🔗 Dependências

| Depende de | Fornece para |
|------------|-------------|
| [Fase 2 — Job System](../fase2/job-system.md) | [Fase 5 — 3D](../fase5/README.md) |
| [Fase 3 — RHI, Batch Renderer](../fase3/README.md) | |
| [Fase 1 — Memory, Containers](../architecture/memory.md) | |

---

## 📚 Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §4 ECS, §5 Scene, §6 Events, §11 Audio, §12 Animation, §13 Physics, §14 UI
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [flecs](https://github.com/SanderMertens/flecs) — ECS archetype-based referência
- [EnTT](https://github.com/skypjack/entt) — ECS patterns
