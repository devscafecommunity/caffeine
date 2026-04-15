# ⚡ Fase 2: O Pulso e a Concorrência

> **Status:** 📅 Planejado  
> **Responsável:** Architects  
> **Versão alvo:** `0.1.x`

Esta fase transforma a engine de uma fundação de dados para um sistema **vivo** — com loop de tempo real, multithreading e input. É o primeiro passo para um jogo executável.

---

## 📋 Índice

| Módulo | Arquivo | Namespace | Fase |
|--------|---------|-----------|------|
| **High-Resolution Timer** | [`timer.md`](timer.md) | `Caffeine::Core` | 2 |
| **Job System** | [`job-system.md`](job-system.md) | `Caffeine::Threading` | 2 |
| **Game Loop** | [`game-loop.md`](game-loop.md) | `Caffeine::Core` | 2 |
| **Input System** | [`input.md`](input.md) | `Caffeine::Input` | 2 |
| **Test System** | [`testing.md`](testing.md) | `Caffeine::Testing` | 2 |
| **Debug Tools** | [`debug.md`](debug.md) | `Caffeine::Debug` | 2 |

---

## 🎯 Objetivo da Fase

> "Utilizar todos os núcleos da CPU e manter clock estável. Primeira camada de input e ferramentas de debug."

Nesta fase a engine ganha:
- **Concorrência real** via Job System com work-stealing
- **Clock determinístico** com fixed timestep e interpolação
- **Input abstrato** com action mapping remapável
- **Ferramentas de debug** para diagnosticar performance

---

## 📊 Requisitos Funcionais

| ID | Requisito | Critério de Aceitação | Módulo |
|----|-----------|----------------------|--------|
| **RF2.1** | High-Resolution Timer | Precisão de microssegundos | [timer.md](timer.md) |
| **RF2.2** | Job System com workers | N workers = cores - 1 | [job-system.md](job-system.md) |
| **RF2.3** | Lock-free job queue | Zero locks no hot path | [job-system.md](job-system.md) |
| **RF2.4** | JobHandle para dependências | Jobs dependentes completam antes | [job-system.md](job-system.md) |
| **RF2.5** | Job Priority Queue | 3 níveis (Crítico/Normal/Background) | [job-system.md](job-system.md) |
| **RF2.6** | Fiber-based Job System | Jobs podem suspender sem block OS thread | [job-system.md](job-system.md) |
| **RF2.7** | Fixed timestep game loop | 60 updates/segundo fixo | [game-loop.md](game-loop.md) |
| **RF2.8** | Variable timestep render | Delta time variável | [game-loop.md](game-loop.md) |
| **RF2.9** | Input System (actions) | Action mapping, polling | [input.md](input.md) |
| **RF2.10** | Debug Tools (logging) | Log com níveis configuráveis | [debug.md](debug.md) |

---

## 🏗️ Arquitetura da Fase

```
Fase 2 — Módulos e Dependências

┌──────────────────────────────────────────────────────────┐
│                    GAME LOOP (Core)                      │
│  fixedDt=1/60s  ─────────────────────────────────────── │
│    │                                                      │
│    ├── Timer.tick()          ← High-Res Timer             │
│    ├── InputManager.poll()   ← SDL Events                 │
│    ├── [Fixed Updates]                                    │
│    │     ├── EventBus.dispatch()                          │
│    │     └── World.update(dt) [Fase 4]                    │
│    ├── interpolate(alpha)                                 │
│    └── render(alpha)         [Fase 3]                     │
│                                                           │
│    JobSystem ◄─── Physics/Animation/AssetLoad             │
└──────────────────────────────────────────────────────────┘

Fase 1 (Fundação) → Fase 2 (Pulso) → Fase 3 (Olho)
```

---

## 📁 Arquivos a Criar

```
src/
├── time/
│   └── Timer.hpp              # RF2.1 — High-Resolution Timer
├── threading/
│   ├── JobSystem.hpp          # RF2.2-2.6 — Job System
│   └── JobQueue.hpp           # Lock-free MPMC queue
├── core/
│   └── GameLoop.hpp           # RF2.7-2.8 — Game Loop
├── input/
│   └── InputManager.hpp       # RF2.9 — Input System
└── debug/
    ├── LogSystem.hpp           # RF2.10 — Logging
    ├── Profiler.hpp            # Profiler markers
    └── DebugDraw.hpp           # Debug visualization
```

---

## 🔗 Critério de Progresso

**Stress test:** 10K partículas simuladas em paralelo, todos os núcleos a 80%+ carga, `tsan` clean (sem data races).

**Demo esperada:** Janela SDL com 10K círculos se movendo, FPS counter no canto.

---

## 🔗 Dependências

| Depende de | Fornece para |
|------------|-------------|
| [Fase 1 — Fundação Atômica](../architecture/core.md) | [Fase 3 — RHI & 2D](../fase3/README.md) |
| `Caffeine::Core::Types` | [Fase 4 — ECS](../fase4/README.md) |
| `SDL3` (events, timer) | todos os sistemas subsequentes |

---

## 📚 Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §1 Game Loop, §2 Job System, §3 Input, §16 Debug
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [Jolt Physics Job System](https://github.com/jrouwe/JoltPhysics) — Referência de work-stealing
- [Game Programming Patterns — Game Loop](https://gameprogrammingpatterns.com/game-loop.html)
