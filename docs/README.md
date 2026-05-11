# ☕ Caffeine Engine — Documentação

> Documentação técnica organizada por módulos funcionais.

---

## Índice da Documentação

### Engine Core

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Core Types** | [`core/types.md`](core/types.md) | Tipos fundamentais (`u8..u64`, `f32`, `f64`, `usize`), plataforma, compilador, assertions |
| **Timer** | [`core/timer.md`](core/timer.md) | High-Resolution Timer, `TimePoint`, `Duration`, `ScopeTimer` |
| **Game Loop** | [`core/game-loop.md`](core/game-loop.md) | Fixed timestep, accumulator, interpolação, spiral of death prevention |
| **Event Bus** | [`core/events.md`](core/events.md) | Pub/sub tipado, `Event<T>`, fila diferida, thread-safe |

### Memory & Data

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Allocators** | [`memory/allocators.md`](memory/allocators.md) | `IAllocator`, Linear, Pool, Stack allocators |
| **Memory Model** | [`memory/memory-model.md`](memory/memory-model.md) | Modelo de memória da engine, estratégias de alocação |
| **Containers** | [`memory/containers.md`](memory/containers.md) | Vector, HashMap, StringView, FixedString |

### Math Library

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Vectors & Matrices** | [`math/vectors.md`](math/vectors.md) | Vec2, Vec3, Vec4, Mat4, operações e transformações |
| **Quaternions** | [`math/quaternions.md`](math/quaternions.md) | Quaternion, SLERP, extensões 3D de Mat4 (perspective, lookAt) |

### Concurrency

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Job System** | [`concurrency/job-system.md`](concurrency/job-system.md) | Thread pool, work-stealing, job priorities, fiber support |

### Input

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Input System** | [`input/input-system.md`](input/input-system.md) | Action mapping, polling, gamepad/keyboard/mouse abstraction |

### Rendering

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **RHI** | [`rendering/rhi.md`](rendering/rhi.md) | RenderDevice, CommandBuffer, Triple Buffering, abstração SDL_GPU |
| **Batch Renderer** | [`rendering/batch-renderer.md`](rendering/batch-renderer.md) | Sprite batching, Texture Atlas, Radix Sort, Persistent Mapped Buffers |
| **Camera 2D** | [`rendering/camera-2d.md`](rendering/camera-2d.md) | Projeção ortográfica, follow, shake, bounds |
| **Camera 3D** | [`rendering/camera-3d.md`](rendering/camera-3d.md) | Projeção perspectiva, lookAt, frustum culling |

### Assets

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Asset Manager** | [`assets/asset-manager.md`](assets/asset-manager.md) | Async loading, hot-reload, `AssetHandle<T>`, cache |
| **CAF Format** | [`assets/caf-format.md`](assets/caf-format.md) | Formato binário `.caf` — zero-parsing, zero-copy |
| **Mesh Loading** | [`assets/mesh-loading.md`](assets/mesh-loading.md) | Mesh loader (.obj, .gltf), Shader system HLSL/GLSL |
| **Asset Pipeline** | [`assets/asset-pipeline.md`](assets/asset-pipeline.md) | CLI converter: PNG/WAV/OBJ → `.caf` |

### ECS (Entity Component System)

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **ECS Core** | [`ecs/core.md`](ecs/core.md) | Archetypes, World, Entity, ComponentQuery, Command Buffer |
| **Scene Manager** | [`ecs/scene.md`](ecs/scene.md) | Scene stack, hierarquia parent/child, serialização `.caf` |
| **ECS Overview** | [`ecs/README.md`](ecs/README.md) | Visão geral do ECS |
| **ECS Examples** | [`ecs/examples.md`](ecs/examples.md) | Exemplos de uso do ECS |

### Physics

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Physics 2D** | [`physics/physics-2d.md`](physics/physics-2d.md) | AABB/circle collision, rigid body, layers, raycast |
| **Spatial Partitioning** | [`physics/spatial-partitioning.md`](physics/spatial-partitioning.md) | Octree, broad-phase collision, frustum culling |

### Animation

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Animation 2D** | [`animation/animation-2d.md`](animation/animation-2d.md) | Sprite clips, state machine, frame events |
| **Skeletal Animation** | [`animation/skeletal-animation.md`](animation/skeletal-animation.md) | Bones, skinning, blend trees, glTF animation |

### Audio

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Audio System** | [`audio/audio-system.md`](audio/audio-system.md) | SFX, music streaming, spatial 2D, SDL_AudioStream |

### UI

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Game UI** | [`ui/game-ui.md`](ui/game-ui.md) | UI retained mode, widgets ECS, bindValue |
| **Editor UI** | [`ui/editor-ui.md`](ui/editor-ui.md) | Dear ImGui integrado, ProfilerWindow, ConsoleWindow |

### Editor

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Scene Editor** | [`editor/scene-editor.md`](editor/scene-editor.md) | Entity Inspector, Hierarchy, Gizmos, drag-and-drop |

### Scripting

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Scripting** | [`scripting/scripting.md`](scripting/scripting.md) | Lua/AngelScript bindings, ECS integration, hot-reload |

### Debug

| Módulo | Documentação | Descrição |
|--------|-------------|-----------|
| **Debug Tools** | [`debug/debug-tools.md`](debug/debug-tools.md) | LogSystem, Profiler, DebugDraw |
| **Testing** | [`debug/testing.md`](debug/testing.md) | Test system, Catch2, CI integration |

### API Reference

| Documentação | Descrição |
|-------------|-----------|
| [`api/README.md`](api/README.md) | Referência completa da API por módulo |

---

## Documentos de Referência

| Documento | Descrição |
|-----------|-----------|
| [`MASTER.md`](MASTER.md) | Documentação unificada completa (visão geral) |
| [`architecture_specs.md`](architecture_specs.md) | Especificações técnicas detalhadas |
| [`HISTORY.md`](HISTORY.md) | Histórico de desenvolvimento (fases, roadmap, planos) |

---

> ☕ *"Caffeine: Because great games are built on strong code and a lot of coffee."*
