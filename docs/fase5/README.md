# 🌐 Fase 5: Transição Dimensional

> **Status:** 📅 Planejado (Futuro Distante)  
> **Responsável:** Artisans  
> **Versão alvo:** `0.5.x → 1.0.0`

Esta fase expande a engine para o mundo 3D **sem quebrar o 2D existente**. A API é desenhada para coexistência: `Camera2D` e `Camera3D` lado a lado, `Mesh3D` e `Sprite2D` no mesmo frame.

---

## 📋 Índice

| Módulo | Arquivo | Namespace | Fase |
|--------|---------|-----------|------|
| **3D Math** | [`3d-math.md`](3d-math.md) | `Caffeine::Math` | 5 |
| **Mesh Loading** | [`mesh-loading.md`](mesh-loading.md) | `Caffeine::Assets` | 5 |
| **Spatial Partitioning** | [`spatial-partitioning.md`](spatial-partitioning.md) | `Caffeine::Spatial` | 5 |
| **Camera 3D** | [`camera3d.md`](camera3d.md) | `Caffeine::Render` | 5 |
| **Skeletal Animation** | [`skeletal-animation.md`](skeletal-animation.md) | `Caffeine::Animation` | 5 |

---

## 🎯 Objetivo da Fase

> "Expansão para 3D sem quebrar o 2D existente."

Nesta fase a engine ganha:
- **Quaternion + Mat4 extended** — rotações 3D com SLERP
- **Mesh loading** — `.obj` e `.gltf` via Asset Manager
- **Shader system** — HLSL (Windows) / GLSL (Linux/macOS)
- **Octree** — spatial partitioning para 3D + frustum culling
- **Camera3D** — perspectiva, lookAt, FPS e orbital
- **Skeletal Animation** — bones, skinning, blend trees

---

## 📊 Requisitos Funcionais

| ID | Requisito | Critério de Aceitação | Módulo |
|----|-----------|----------------------|--------|
| **RF5.1** | Quaternion math | Multiplicação, SLERP, look rotation | [3d-math.md](3d-math.md) |
| **RF5.2** | Mesh loader | .obj e .gltf carregamento | [mesh-loading.md](mesh-loading.md) |
| **RF5.3** | Shader system | HLSL (Windows) / GLSL (Linux/macOS) | [mesh-loading.md](mesh-loading.md) |
| **RF5.4** | Octree spatial | Broad-phase collision 3D | [spatial-partitioning.md](spatial-partitioning.md) |
| **RF5.5** | Camera 3D | Projeção perspectiva, lookAt | [camera3d.md](camera3d.md) |
| **RF5.6** | Frustum culling | Objetos fora da view não renderizam | [camera3d.md](camera3d.md) |

---

## 🏗️ Arquitetura 3D

```
Camera3D
  └── perspective(fovY, aspect, near, far)
  └── lookAt(eye, target, up)

MeshSystem (ISystem, priority = 900)
  └── query Mesh3D + Position3D
  └── submits to BatchRenderer3D (futuro) ou RHI direto

PhysicsSystem3D (futuro — escalation trigger)
  └── usa Octree para broad phase
```

---

## 📁 Arquivos a Criar

```
src/
├── math/
│   └── Quat.hpp              # RF5.1 — Quaternion
├── assets/
│   └── MeshLoader.hpp        # RF5.2 — .obj/.gltf
├── render/
│   ├── Camera3D.hpp          # RF5.5 — perspectiva
│   └── ShaderSystem.hpp      # RF5.3 — HLSL/GLSL
├── spatial/
│   └── Octree.hpp            # RF5.4 — spatial partitioning
└── animation/
    └── SkeletalAnimation.hpp  # extensão do AnimationSystem
```

---

## 🔗 Critério de Progresso

**Demo:** Mesh 3D carregada com shader customizado, **60fps** com frustum culling ativo.

---

## 🔗 Dependências

| Depende de | Fornece para |
|------------|-------------|
| [Fase 4 — ECS completo](../fase4/README.md) | [Fase 6 — Editor 3D](../fase6/README.md) |
| [Fase 3 — RHI, Asset Manager](../fase3/README.md) | |
| [Fase 1 — Mat4, Vec3](../math/vectors.md) | |

---

## 📚 Referências

- [`docs/architecture_specs.md`](../architecture_specs.md) — §15 Math Library (Quat)
- [`docs/MASTER.md`](../MASTER.md) — Documentação unificada
