# 🏛️ Fase 6: O Olimpo — Caffeine Studio IDE

> **Status:** 📅 Planejado  
> **Responsável:** Full Guild  
> **Versão alvo:** `1.0.0+`

Esta fase transforma a engine em uma **ferramenta visual completa** — o Caffeine Studio IDE. Com ela, designers e scribes podem criar jogos sem tocar em código C++.

---

## 📋 Índice

| Módulo | Arquivo | Namespace | Fase |
|--------|---------|-----------|------|
| **Embedded UI (ImGui)** | [`embedded-ui.md`](embedded-ui.md) | `Caffeine::Editor` | 6 |
| **Scene Editor** | [`scene-editor.md`](scene-editor.md) | `Caffeine::Editor` | 6 |
| **Asset Pipeline** | [`asset-pipeline.md`](asset-pipeline.md) | `Caffeine::Tools` | 6 |
| **Scripting** | [`scripting.md`](scripting.md) | `Caffeine::Script` | 6 |

---

## 🎯 Objetivo da Fase

> "Transformar a engine em ferramenta visual para a comunidade."

Nesta fase a engine ganha:
- **Dear ImGui** integrado — UI do editor, profiler, console
- **Scene Editor** — drag-and-drop, entity inspector, hierarchy
- **Transform gizmos** — Translate/Rotate/Scale visual
- **Asset Pipeline** — conversor de PNG/WAV/OBJ → `.caf`
- **Scripting** — Lua ou AngelScript bindings (TBD)

---

## 📊 Requisitos Funcionais

| ID | Requisito | Critério de Aceitação | Módulo |
|----|-----------|----------------------|--------|
| **RF6.1** | Embedded UI | Dear ImGui integrado ao SDL3 | [embedded-ui.md](embedded-ui.md) |
| **RF6.2** | Profiler UI | Frame timing + memory profiling | [embedded-ui.md](embedded-ui.md) |
| **RF6.3** | Scene Editor | Drag-drop, inspector, hierarchy | [scene-editor.md](scene-editor.md) |
| **RF6.4** | Transform gizmos | Translate/Rotate/Scale visual | [scene-editor.md](scene-editor.md) |
| **RF6.5** | Asset Pipeline | Processador → .caf bundles | [asset-pipeline.md](asset-pipeline.md) |
| **RF6.6** | Hot-reload runtime | Textures/shaders recarregáveis | [embedded-ui.md](embedded-ui.md) |

---

## 🏗️ Arquitetura do Editor

```
Caffeine Studio (Fase 6)
  │
  ├── GameWindow (jogo rodando em tempo real)
  │     └── ECS World + Game Loop
  │
  ├── HierarchyPanel (árvore de entidades)
  │     └── Lista de entidades com nome
  │
  ├── InspectorPanel (componentes da entidade selecionada)
  │     └── Edição de Position, Sprite, RigidBody, etc.
  │
  ├── SceneViewport (gizmos de transform 3D/2D)
  │     └── Manipulação visual de entidades
  │
  ├── AssetBrowser (assets do projeto)
  │     └── Drag-drop → cria entidades com assets
  │
  ├── ProfilerWindow (frame timing)
  │     └── Dados do Profiler da Fase 2
  │
  └── ConsoleWindow (log do jogo)
        └── Dados do LogSystem da Fase 2
```

---

## 🔗 Critério de Progresso

**Primeiro game completo** feito 100% na Caffeine, do início ao fim, sem crash.

---

## 🔗 Dependências

| Depende de | Fornece para |
|------------|-------------|
| Todas as fases anteriores | — (fase final) |
| [Debug Tools](../fase2/debug.md) | Profiler + Console UI |
| [ECS + Scene](../fase4/ecs.md) | Entity Inspector |

---

## 📚 Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §16 Debug Tools
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
- [Dear ImGui](https://github.com/ocornut/imgui) — UI do editor
- [Dear ImGui SDL3 example](https://github.com/ocornut/imgui/blob/master/examples/example_sdl3_opengl3/main.cpp)
