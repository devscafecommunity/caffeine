# Editor backlog — Phase B–D (Oct 2026)

Phase A (materials save, asset previews, ImGui IDs) landed in the same sprint. This document tracks items 4–7 from the product list.

## 4. Image Manager tab

**Goal:** Preview, manipulate, and edit 2D images, textures, and related assets.

**Proposed slices:**

1. Docked panel `ImageManagerPanel` — open from View/Modules; list recent + project textures.
2. Canvas: zoom/pan, channel view (RGBA), sRGB toggle, metadata (resolution, format).
3. Non-destructive ops first: resize, flip, generate mips preview; export to `.png`/recompile to `.caf` via `TextureCompiler`.
4. Optional: simple paint/rect fill (can delegate heavy work to Convoy Architect long term).

**Touch points:** `AssetBrowser` (open image here), `TextureCompiler`, `EditorContext`, layout profiles in settings.

## 5. Animation core + tab + animator

**Goal:** 2D and 3D animation with state-machine style transitions; scriptable from Lua and C++.

**Proposed slices:**

1. **Core (`src/animation/`):** `AnimationClip` (keyframes: transform, sprite frame, morph), `AnimationPlayer` component, clock/evaluator shared by 2D/3D.
2. **Editor tab:** timeline already stubbed (`animationTimeline`, `animatorController`) — wire to core assets (`.anim`, `.controller`).
3. **Animator:** graph nodes (states, transitions, conditions: float/bool/trigger), blend trees for 3D.
4. **API:** C++ public headers in `caffeine-core`; Lua bindings mirroring `AnimationPlayer` + `AnimatorController` setters.

**Dependencies:** scene serialization, existing skeletal/mesh path for 3D; sprite/tile components for 2D.

## 6. Post-processing plugin (complete)

**Goal:** Use HDR forward + `PostProcessStack` end-to-end in editor and runtime.

**Proposed slices:**

1. Audit `PostProcessEditorUI` vs runtime stack — ensure same effect order and parameter IDs.
2. Register plugin with editor menu; persist stack per scene or global volume component.
3. Viewport + gameplay preview both route through stack when enabled.
4. Document effect list and Lua/C++ hooks for toggling volumes.

**Touch points:** recent commit “HDR forward PBR pipeline, GPU post stack”; `doppio-plugins/` if packaged separately.

## 7. Pathfinding base + AI development plugin

**Goal:** Engine-level nav/path API; editor plugin to author NPC behaviour (not combat-only).

**Proposed slices:**

1. **Core:** nav mesh or grid abstraction (`NavMesh`, `PathQuery`), async request queue, debug draw in viewport.
2. **ECS:** `NavAgent` + `AIController` components; steering along path.
3. **Plugin:** behaviour trees or state scripts (Lua), templates for patrol/chase/interact; visual debugger.
4. **Integration:** use physics/world queries from existing Bullet integration where applicable.

---

## Suggested PR order

1. Image Manager (vertical slice, user-visible).
2. Animation core + minimal clip playback in viewport.
3. Post-process plugin completion (smaller, builds on recent pipeline work).
4. Pathfinding + AI plugin (largest; split core vs plugin).
