# 📜 Histórico de Desenvolvimento

> Artefatos de desenvolvimento comprimidos — fases, roadmaps, planos e revisões.
> Para documentação técnica real da engine, consulte o [`README.md`](README.md).

---

## Fases de Desenvolvimento

O projeto foi estruturado em 6 fases progressivas. Abaixo, o resumo de cada fase e seu status atual.

| Fase | Nome | Descrição | Status |
|:----:|------|-----------|:------:|
| 0 | Setup & Docs | Infraestrutura, repositório, CI, documentação base | ✅ Completo |
| 1 | Fundação Atômica | Core types, custom allocators, containers, math library | ✅ Completo |
| 2 | Concorrência | Timer, Job System, Game Loop, Input, Debug Tools | 📅 Pendente |
| 3 | RHI & 2D | RHI (SDL_GPU), Batch Renderer, Camera 2D, Asset Manager | 📅 Pendente |
| 4 | ECS & Sistemas | ECS Archetype, Scene, Events, Physics, Animation, Audio, UI | 📅 Pendente |
| 5 | Transição 3D | Quaternions, Mesh Loading, Camera 3D, Spatial Partitioning | 📅 Futuro |
| 6 | Caffeine Studio | Embedded UI (ImGui), Scene Editor, Asset Pipeline, Scripting | 📅 Pendente |

### Fase 0 — Setup & Documentação
- Repositório GitHub, estrutura de branches, CI (GitHub Actions)
- Build system CMake com testes
- Documentação: MASTER.md, ROADMAP.md, SPECS.md, architecture_specs.md
- Style guide e convenções de código

### Fase 1 — Fundação Atômica
- Tipos de largura fixa (`u8..u64`, `f32`, `f64`, `usize`)
- Custom allocators (Linear, Pool, Stack) com interface `IAllocator`
- Containers (Vector, HashMap, StringView, FixedString)
- Math library (Vec2, Vec3, Vec4, Mat4)
- Testes: `test_core.cpp` (8), `test_allocators.cpp` (16+3), `test_containers.cpp` (14+1), `test_math.cpp` (18)

### Fase 2 — Concorrência *(planejado)*
- High-Resolution Timer com precisão de microssegundos
- Job System com work-stealing, lock-free queue, 3 níveis de prioridade
- Fiber-based jobs (jobs podem suspender sem bloquear OS thread)
- Game Loop com fixed timestep (60 updates/s) + interpolação
- Input System com action mapping remapável
- Debug Tools: LogSystem, Profiler, DebugDraw

### Fase 3 — RHI & 2D *(planejado)*
- RHI: abstração sobre SDL_GPU com triple buffering
- Batch Renderer: 50K sprites em 1 draw call
- Texture Atlas com bin-packing
- Camera 2D com follow e shake
- Asset Manager com async loading e hot-reload
- Formato `.caf` (Caffeine Asset Format) — zero-parsing, zero-copy

### Fase 4 — ECS & Sistemas *(planejado)*
- ECS Archetype-based com cache locality
- Command Buffer diferido (create/destroy seguro durante iteração)
- Scene Manager com hierarquia parent/child e serialização `.caf`
- Event Bus pub/sub com `Event<T>` tipado
- Physics 2D (AABB/circle collision, rigid body)
- Animation 2D (sprite clips, state machine)
- Audio System (SDL_AudioStream, spatial 2D)
- UI System retained mode (widgets ECS, bindValue)
- Scripting bindings iniciais (Lua/AngelScript)

### Fase 5 — Transição 3D *(futuro distante)*
- Quaternion + SLERP para rotações 3D
- Mesh loading (.obj, .gltf) via Asset Manager
- Shader system (HLSL/GLSL)
- Octree para spatial partitioning + frustum culling
- Camera 3D (perspectiva, lookAt, FPS e orbital)
- Skeletal Animation (bones, skinning, blend trees)

### Fase 6 — Caffeine Studio IDE *(planejado)*
- Dear ImGui integrado ao SDL3
- Scene Editor com drag-and-drop, inspector, hierarchy
- Transform gizmos (Translate/Rotate/Scale)
- Asset Pipeline CLI (PNG/WAV/OBJ → `.caf`)
- Scripting (Lua/AngelScript, hot-reload)

---

## Convenções e Padrões de Desenvolvimento

Extraído de `SPECS.md` — regras que guiaram o desenvolvimento:

### Princípios
- **DOD** — Data-Oriented Design: dados organizados para cache locality
- **YAGNI** — You Ain't Gonna Need It: não implementar o que não é necessário
- **KISS** — Keep It Simple, Stupid: simplicidade acima de tudo
- **Lock-free** onde possível, mutex onde necessário
- **Zero-dependency**: reduzir ao máximo dependência da std, criar stdlib própria

### Convenções de Código
- C++20, namespaces aninhados (`Caffeine::Module`)
- Nomenclatura: PascalCase para tipos, camelCase para métodos/membros
- Headers com documentação Doxygen
- Sem `new`/`delete` soltos — usar custom allocators
- Sem RTTI, sem exceções, sem virtual em hot paths

### Fluxo de Trabalho (R.I.C.O.)
1. **Research** — Pesquisar padrões e referências
2. **Idea** — Propor solução
3. **Conflict** — Debater trade-offs
4. **Order** — Implementar e documentar

---

## Planos e Revisões Anteriores

Documentos de planejamento detalhado e revisões foram mantidos durante o desenvolvimento. Estão disponíveis no histórico do git.

---

> ℹ️ Este documento substitui os antigos `ROADMAP.md`, `SPECS.md`, templates e READMEs de fase.  
> Para documentação técnica atualizada, consulte os diretórios funcionais em [`docs/`](README.md).
