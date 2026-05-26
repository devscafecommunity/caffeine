# Caffeine Engine — Audit Report

**Date**: 2026-05-26  
**Auditor**: Sisyphus (automated session)  
**Scope**: Full engine review — Rendering 3D, ECS, Memory, Math, Editor, Serialization, Audio/Preview, Build/Tests  
**Status of Fixes**: Rendering bugs (Bug 1–3) were corrected during this session. All other findings below are **unresolved**.

---

## Severity Legend

| Level | Meaning |
|---|---|
| 🔴 CRITICAL | Crash, data corruption, undefined behaviour, infinite loop |
| 🟠 HIGH | Incorrect behaviour, wrong results, silent data loss |
| 🟡 MEDIUM | Degraded correctness, performance problem, dangerous pattern |
| 🟢 LOW | Code smell, minor inconsistency, latent risk |
| 🔵 INFO | Stub / not implemented / known limitation |

---

## 1. Rendering 3D (`src/editor/SceneViewport.cpp`)

### ✅ Verified Correct

| Item | Location | Notes |
|---|---|---|
| `Mat4::perspective()` | `src/math/Mat4.hpp` | Matches OpenGL RH spec exactly |
| `Mat4::lookAt()` | `src/math/Mat4.hpp` | Right-handed, translation correct |
| `Mat4::transformVec4()` | `src/math/Mat4.hpp` | Correct |
| NDC→screen mapping `(ndcX+1)*0.5*w` | `SceneViewport.cpp:651` | Correct |
| Y-flip `1.0 - ndcY` | `SceneViewport.cpp:651` | Correct |
| Near-plane clipping in `drawLine3D` | `SceneViewport.cpp` | Existed and correct |

### ✅ Fixed This Session

| ID | Severity | File:Line | Description | Fix Applied |
|---|---|---|---|---|
| R-1 | 🔴 CRITICAL | `SceneViewport.cpp:1807–1810, 1827, 1831` | Grid spacing: `(int)(...) * (int)spacing` with `(int)0.5f == 0` → infinite loop when `camDistance < 1.0f` | Changed to `(int)(floor(...) * spacing)`; loops changed from `int` to `float` |
| R-2 | 🟠 HIGH | `SceneViewport.cpp:651` | `projectToScreen` lacked NDC bounds check; off-screen objects behind the frustum sides generated huge ImGui coords | Added `if (ndcX/Y outside [-1,1]) return (-10000,-10000)`; W threshold raised from `0.001f` to `0.1f` |
| R-3 | 🟡 MEDIUM | `SceneViewport.cpp:651` | `projectToScreen` recomputed `view + proj + VP` matrix on every call; called per-entity per-frame in `drawEmptyEntities` + `drawLightGizmos` | Added `computeVP3D()` static helper + `projectToScreenVP()` overload; callers now pre-compute once per frame |

---

## 2. ECS (`src/ecs/World.hpp`, `src/ecs/Archetype.hpp`)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| E-1 | 🟠 HIGH | `src/ecs/World.hpp` | No generation/version counter on Entity IDs. After `world.destroy(e)`, the same ID can be reused and assigned to a new entity. Any stale `Entity` handle silently points to the wrong entity. | Add a 16-bit generation field to `Entity` (upper bits). Increment on destroy. Validate on access. |
| E-2 | 🟡 MEDIUM | `src/ecs/World.hpp` — `forEach` | `forEach` iterates `m_archetypes` by index. If a callback calls `world.add<T>(e)` on an entity (triggering archetype migration), `m_archetypes` may reallocate. The raw `Archetype*` captured before iteration could dangle. | Snapshot archetype count before loop, or use a stable indirection (index into stable slab). Mark `forEach` callbacks as "no structural mutation" in docs. |
| E-3 | 🟢 LOW | `src/ecs/World.hpp` | No assertion or compile-time guard preventing structural mutations (add/remove/destroy) inside `forEach`. | Add a `m_iterating` boolean; assert in `add`, `remove`, `destroy` when set. |

---

## 3. Memory (`src/memory/`)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| M-1 | 🟢 LOW | `src/memory/LinearAllocator.hpp` | Reset is all-or-nothing. No partial rewind. Clients that mix lifetimes can't selectively free. | Document clearly or add a marker/rewind API. |
| M-2 | 🟢 LOW | `src/memory/PoolAllocator.hpp` | No double-free detection. Freeing the same block twice silently corrupts the free list. | Add a debug-mode allocated-block bitset. |
| M-3 | 🟢 LOW | `src/memory/StackAllocator.hpp` | Stack unwind depends on callers pushing/popping in matched order. No guard against mismatched pop. | Add stack-marker cookies in debug builds. |

---

## 4. Math (`src/math/`)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| MA-1 | 🟢 LOW | `src/math/Vec3.hpp` — `normalized()` | Returns `Vec3(0,0,0)` on zero-length input. Silent — callers downstream may produce NaN normals. | Return an `Optional<Vec3>` or log a warning in debug builds. |
| MA-2 | 🟢 LOW | `src/math/Mat4.hpp` — `inverted()` | Returns identity on singular matrix. Silent fallback can mask bugs in projection/transform chains. | Assert in debug builds or return `Optional<Mat4>`. |
| MA-3 | 🟢 LOW | `src/math/Quat.hpp` — `normalized()` | Returns identity quaternion on zero quaternion. Same silent-fallback concern. | Assert in debug builds. |
| MA-4 | 🟡 MEDIUM | `src/math/Mat4.hpp` — `lookAt()` | Degenerates when `eye == target` (forward = zero → `normalized()` = zero → bad matrix). Currently guarded only by camera pitch clamp `±1.5f` in the editor. | Add explicit `CF_ASSERT(eye != target)` or early-out returning identity with a warning. |

---

## 5. Editor

### 5a. Selection & Raycasting (`src/editor/EditorContext.cpp`, `SceneViewport.cpp`)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| S-1 | 🟠 HIGH | `SceneViewport.cpp` — `raycastSelectEntity` | Raycast only tests entities that have `ECS::Transform` (2D transform component). Entities that exist purely in 3D (`Position3D` only, no `ECS::Transform`) are invisible to selection. | Add a separate raycast pass over entities with `Position3D`. |
| S-2 | 🟢 LOW | `SceneViewport.cpp` — `rayIntersectsAABB` | AABB is axis-aligned in world space — correct. But no OBB support. Rotated meshes use a world-space AABB that can be very loose, making small-angle selection imprecise. | Acceptable limitation. Document or add OBB for selected component types. |

### 5b. Undo Stack (`src/editor/EditorContext.hpp`, `EditorContext.cpp`)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| U-1 | 🟡 MEDIUM | `EditorContext.hpp` — `UndoStack` | Snapshot-based undo serialises the entire world on every `beginUndo`. For large scenes this is O(scene size) per action. | Consider command-based undo (store delta / inverse operation) for frequent transforms. |
| U-2 | 🟡 MEDIUM | `EditorContext.hpp:MAX_UNDO=256` | Static array of 256 `EditorCommand`, each holding `std::vector<u8> beforeState + afterState`. Memory scales with scene size × 256. A 10 MB scene = up to 5 GB undo buffer. | Cap total buffer size in bytes, not command count. Evict oldest on overflow. |
| U-3 | 🟠 HIGH | `EditorContext.cpp` — `applySnapshot` | `CF_ASSERT(!snapshot.empty())` — if `beginUndo` fails silently (serializer returns empty bytes), this assert fires and crashes the editor on next undo/redo. | Guard `beginUndo` return value; skip pushing if serialization failed; log error. |

---

## 6. Serialization

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| SE-1 | 🔵 INFO | `src/editor/AssetCooker.cpp:123,129` | Asset cooking cache not implemented (stub comments). Assets re-cook on every load. | Implement file-hash–based cache. |
| SE-2 | 🟢 LOW | General | No versioning observed on serialized scene format. Format changes will silently corrupt or crash on older scene files. | Add a format version header; write migration path for each bump. |

---

## 7. Audio / Preview (Stubs)

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| A-1 | 🔵 INFO | `src/editor/AudioPreviewPanel.cpp:188,229` | Audio asset load not implemented. Panel exists but plays nothing. | Implement or gate behind a feature flag. |
| A-2 | 🔵 INFO | `src/editor/PreviewRenderer.cpp:91–102` | Shader pipeline blocked — RHI incomplete. Preview renderer renders nothing. | Track RHI completion milestone; hook up once ready. |
| A-3 | 🔵 INFO | `src/editor/ProjectManager.cpp:236` | AssetBrowser load capacity (CAP) not implemented. | Implement or document limit. |

---

## 8. Build & Tests

### 8a. Hardcoded Absolute Paths

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| B-1 | 🟠 HIGH | `tests/doppio_ui_client.py:264–265` | Hardcoded `/home/pedro/repo/caffeine/...` — test suite fails on any machine other than the author's. | Replace with `Path(__file__).resolve().parents[N]` or a `CAFFEINE_ROOT` env var. |
| B-2 | 🟠 HIGH | `tests/editor_test_automation.py:379` | Same hardcoded absolute path. | Same fix. |
| B-3 | 🟠 HIGH | `tests/gen_test_scene.py:133` | Same hardcoded absolute path. | Same fix. |
| B-4 | 🟠 HIGH | `tests/test_viewport_systems.py:335–336` | Same hardcoded absolute path. | Same fix. |

### 8b. Test Infrastructure

| ID | Severity | File:Line | Description | Suggested Fix |
|---|---|---|---|---|
| B-5 | 🟡 MEDIUM | `tests/CMakeLists.txt` | New `DoppioUIAutomated` CTest target added this session (timeout 60s). No CI pipeline observed to run it automatically. | Hook CTest into CI (GitHub Actions / CMake preset). |
| B-6 | 🟢 LOW | `tests/run_ui_tests.sh` | Convenience runner script added. Assumes editor binary is already built and at a relative path. | Add a build step or document prerequisite clearly. |

---

## Summary Table

| Severity | Count | Areas |
|---|---|---|
| 🔴 CRITICAL (fixed) | 1 | Rendering |
| 🟠 HIGH (fixed) | 2 | Rendering |
| 🟠 HIGH (open) | 6 | ECS (E-1), Editor (S-1, U-3), Build (B-1–B-4) |
| 🟡 MEDIUM (open) | 7 | ECS (E-2), Rendering (R-3 fixed), Math (MA-4), Editor (U-1, U-2), Build (B-5) |
| 🟢 LOW (open) | 10 | ECS (E-3), Memory (M-1–M-3), Math (MA-1–MA-3), Editor (S-2), Serialization (SE-2), Build (B-6) |
| 🔵 INFO (stubs) | 4 | Serialization (SE-1), Audio/Preview (A-1–A-3) |

---

## Recommended Priority Order

1. **E-1** — Entity generation counters (prevents silent dangling-handle bugs as scene complexity grows)
2. **B-1–B-4** — Hardcoded paths (CI is broken for anyone else)
3. **U-3** — Undo crash on empty snapshot
4. **S-1** — 3D-only entities not selectable
5. **U-1/U-2** — Undo memory explosion in large scenes
6. **E-2** — Archetype mutation safety inside `forEach`
7. **MA-4** — `lookAt` degeneration assert
8. **SE-2** — Scene format versioning (before first external release)
9. Stubs (A-1–A-3, SE-1) — track on RHI milestone
