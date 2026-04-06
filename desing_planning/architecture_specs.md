# 🏗️ Architecture Specifications

> ⚠️ **Status:** A ser escrito conforme o desenvolvimento avança da Fase 1 em diante.  
> ⚠️ Para contexto completo, consulte [`docs/MASTER.md`](../docs/MASTER.md) §3.

Este documento contém as **especificações técnicas detalhadas** dos subsistemas arquiteturais da Caffeine Engine:

---

## 📋 Índice das Especificações

| Especificação | Fase | Status | Descrição |
|---|---|---|---|
| **RHI — Rendering Hardware Interface** | 3 | 📅 Planejado | Abstração sobre SDL_GPU |
| **ECS — Entity Component System** | 4 | 📅 Planejado | Arquitetura archetype-based |
| **Job System** | 2 | 📅 Planejado | Lock-free thread pool |
| **Event Bus** | 4 | 📅 Planejado | Pub/sub desacoplado |
| **Camera System** | 3 | 📅 Planejado | Matriz 4×4 agnóstica |
| **Scene Graph** | 4 | 📅 Planejado | Serialização e hierarquia |
| **Math Library** | 1+ | 📅 Planejado | Vec, Mat, Quat, SIMD hints |

---

## 📝 Como Escrever uma Especificação

Cada especificação neste documento segue o template:

```markdown
## [Nome do Subsistema]

### Visão Geral
- O que é
- Por que existe
- Como se relaciona com outros subsistemas

### Arquitetura
- Diagrama de componentes
- Interfaces públicas
- Fluxo de dados

### Interface Pública
```cpp
// API mínima viável
```

### Decisões de Design
- Por que não X
- Trade-offs considerados
- Previsões para 3D

### Critérios de Sucesso
- Como sabemos que está pronto
- Benchmarks de referência
```

---

## [A ser preenchido conforme desenvolvimento]
