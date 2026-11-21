# Photoreal procedural world — validation and roadmap

<div class="doc-meta">
  <span class="doc-badge doc-badge--reference">Reference</span>
  <span class="doc-badge">Doppio</span>
  <span>Last updated: <time datetime="2026-11-21">2026-11-21</time></span>
  <span>Source: <a href="https://github.com/devscafecommunity/caffeine/blob/main/doppio/docs/photoreal-world-roadmap.md"><code>photoreal-world-roadmap.md</code></a></span>
</div>

<p class="doc-lede">One seed, rule files (<code>.nature</code>, <code>.mat</code>, terrain and water graphs) and
deterministic chunk streaming, so a whole photoreal world is generated on demand, always identical, and
mostly baked. This page checks the idea against the code and lists what to build, in order.</p>

## 1. Verdict

**Implementable, with the current stack (Rust/Iced editor, C++ engine on SDL3 GPU, C ABI).** Nothing in the idea
needs a new renderer. The hard parts are not graphics: they are *determinism*, *chunk borders* and
*connecting the generators*. Most of the rendering features a "photoreal" pass asks for already exist; the
missing piece is the **world layer** that connects terrain, nature, materials and water.

The idea has four parts. Their status in the repository:

| Idea | Status | Evidence |
|---|---|---|
| Procedural materials, multi-frequency, anti-tiling | **Done**, tune with recipes | ~123 procedural ops, 78 presets, Variation class (`material-pipeline.md`) |
| Depth: parallax, micro-shadows, AO, IBL | **Done** | POM with self-shadow, geometric displacement, height-derived AO, env map + probe IBL (`scene_lit.frag`) |
| Rules as files (`.nature`, `.mat`, terrain/water graphs) | **Done per tool**, not linked | each generator saves its own graph and places assets |
| Seed → deterministic world, streamed by chunk | **Partial** | `ProceduralStreamer` + `hash01(cx, cz, seed)` exist, but use a built-in fbm terrain and props, not the generators |
| Vegetation/rocks adapted to terrain, scattered by masks | **Partial** | scatter core + biome table + **Scene > Scatter on Terrain** (C ABI v16) place plans by slope, wetness and height (`terrain-generator-guide.md`); streaming by chunk and the generators' output as scatter content are still open |
| Instancing, LOD, impostors for mass vegetation | **Partial** | instanced draws (also with painted vertex colour), wind, translucency and alpha-tested shadows exist; **Micromesh cluster LOD** now draws dense opaque parts (rocks, trunks) through the scene renderer (`micromeshes.md`); foliage cards still have no LOD chain or impostors |

## 2. Review of the claims in the original discussion

Kept as true, with caveats that matter for the design.

* **"Stable by derived seeds."** True, and already how the streamer works (`hash01(cx, cz, seed, salt)`).
  Caveats: (1) use an **integer hash** (xxHash or SplitMix) per cell, never floating-point sums;
  (2) never let a parallel reduction decide the order of results, sort by cell id; (3) pin the noise and
  `libm` calls (or avoid `sin`/`pow` in anything that decides *placement*), otherwise two platforms place
  a tree a metre apart; (4) version the algorithm: a change to a `.nature` op changes every tree, so each rule
  file carries a `generator_version` and old worlds keep the old behaviour or are re-baked on purpose.
* **"Save only rule + position + sub-seed."** True for *generated* content. Anything the user *edits* (sculpted
  terrain, a moved tree, a deleted rock) must be stored as a sparse **delta layer** over the generated
  world, or editing breaks determinism. Add this from the start.
* **"Generate once per chunk, freeze the result."** Right, and it fixes the performance worry. Cache by
  `(world_seed, chunk, rule hashes, generator_version)`, on disk as well as in RAM (this is the "pre-compiled"
  part of the idea).
* **Chunk borders are the real trap.** A tree whose crown crosses a chunk edge must not be duplicated or
  cut. Use **cell ownership**: placement runs on a global jittered grid (or blue-noise/Poisson cells); a cell
  belongs to exactly one chunk, and a chunk generates its own cells *plus a margin read-only* to resolve
  collisions with neighbours' cells, using the same deterministic order.
* **"Trees do not cross rocks or each other."** Solve it at *placement*, not at mesh time: priority classes
  (rocks, then trees, then bushes, then grass), exclusion radius per class, and a density mask. This is cheap
  and deterministic. Per-vertex raycasts are only for the trunk base.
* **Ground conforming.** Sample `terrain_height(x, z)` (exists in the C ABI) and the terrain normal. Trees
  take a *blend* of up and the normal (small factor), rocks sink by the lowest height under their footprint,
  grass follows the normal. Root flare and rock skirts need a deformed base mesh and vertex colour (not done,
  see phase 3).
* **Contact blending.** Correct: vertex colour on the base plus a ground blend in the shader. Needs the
  **vertex colour** channel that the Nature Generator does not have yet.
* **The pasted Height-to-AO code** (Rust, 8 directions): the idea is right, and we already ship an AO from
  height. If it is reused: the parallel version has a typo, the direction `(0, 2)` should be `(0, -1)`, and
  the slope term `height_diff / distance` ignores the world scale of the texel, so AO strength changes with
  resolution. Compare with ours before replacing anything.
* **"Photorealism is only the graph."** Partly. Photoreal also depends on **scale and variety**:
  one bark texture on every trunk looks fake at any quality. Per-instance tint, wetness, wind and
  seasonal variation matter as much as extra noise octaves.

Corrections specific to this project:

* The original text speaks of `SDL_CreateTexture`, a 2D API. The engine uses **SDL3 GPU**; mips are already
  generated (`createMippedTexture`, box filter). Better filtering (Kaiser) and an explicit anisotropic
  sampler setting are optional improvements, not a gap.
* Normal blending (whiteout) already exists as a graph node; the preview material path does not need a
  new blend function.
* Specular IBL here uses an equirectangular sky and a probe; a **prefiltered cubemap with a BRDF LUT** is
  the proper split-sum form and a possible quality upgrade, not a missing feature.

## 3. Target architecture

```text
world.cworld  (seed, generator_version, layer list, edit deltas)
   ├─ terrain graph  → height + masks (slope, wetness, flow, biome)   [exists: terrain generator]
   ├─ biome rules    → density per class from the masks               [new]
   ├─ scatter rules  → .nature / .rock / grass refs + placement rules [new]
   ├─ water graph    → water bodies from flow and sea level           [partial]
   └─ material layers→ splat/blend from the same masks                [partial]

per chunk (worker thread, deterministic):
   masks → placement (cells, priority, exclusion) → instances (transform + sub-seed + variant)
        → bake cache (RAM + disk) → GPU instance buffers per LOD → draw
```

Principles: (1) a chunk is a **pure function** of the world file and its coordinates; (2) generators produce
**data** (fields, instance lists, meshes), the renderer never runs a graph; (3) only commands via
`Message`s and MCP actions, as in the rest of Doppio; (4) `libcaffeine-capi` stays the only boundary.

## 4. Roadmap

Each phase ends with something visible and testable headless. Do them in order; phases 1 and 2 are
independent of renderer work.

### Phase 0 — Determinism foundation

- [x] `core/Hash.hpp`: SplitMix64 `deriveSeed(parent, salt, x, z)`, `toUnit`, `GENERATOR_VERSION`; golden test
      `tests/test_hash.cpp` pins outputs, `recipe_ref.rs` pins the same numbers in Rust.
- [x] The streamer's placement decisions go through `placementHash(version, ...)`: version 1 is the legacy `hash01`, version 2 the
      SplitMix64 of `core/Hash.hpp`. `ProceduralWorldComponent::generatorVersion` stores the choice and **defaults to 1**, so
      existing worlds do not move; new worlds should set `Core::GENERATOR_VERSION`. Tested in `tests/test_scatter.cpp`.
- [x] `generator_version` field in every `generator.*` recipe (nature, material, texture, terrain, water).
- [ ] The same field in bare `.mat` files and in the world file (there is no world file yet).
- [x] Test: derived seeds for a block of cells are identical on 1 and on 7 threads (`tests/test_hash.cpp`).
- [x] The same test for whole generated scatter chunks: 1 and 7 threads give identical results, and cutting the world into
      32 m, 48 m or 96 m chunks gives the same instances (`tests/test_scatter.cpp`). Whole streamer chunks (cubes) still to do.

### Phase 1 — Terrain masks exported to the engine

- [x] The terrain exposes named **masks** as engine-readable fields: `height`, `slope` and `curvature` derived by the engine from the
      heightfield (exact, nothing stored), and `flow` (D8 drainage), `wetness`, `wear` and `deposit` (erosion) sent by the generator as 8-bit
      masks, saved with the terrain file (version 3). The Output node gained optional Wear and Deposit inputs (the presets with erosion wire
      them). 8 bits is the one precision; 16-bit masks were dropped as unneeded for weights. Picture in `terrain-generator-guide.md`.
- [x] C ABI v15: `set_terrain_mask`, `terrain_mask_count/at`, `terrain_mask` at a point, `terrain_mask_rect` over a rectangle,
      `terrain_normal` at a point (POD info struct); the Rust crate wraps them. Tested on both sides: `tests/test_terrain_masks.cpp`,
      `tests/test_capi.cpp`, and an engine-backed Doppio test that places the Alpine Range and checks every mask against the heights.
- [x] **Biome table** (`terrain/TerrainBiome`): per scatter class, `base x` a product of trapezoid terms over masks, as plain text, plugged into
      the scatter module's density callback; an unknown mask places nothing and is listed by `missingMasks`. Tested with the scatter module
      (trees never on steep or wet ground, rocks only on the ridge). Not yet read or written by the editor: that is the World tab (phase 6).
- [ ] Masks still to make: height *bands* as named masks (`height` covers the raw value; a band is a biome term today), a `wear` mask for
      terrains that did not use hydraulic erosion, and masks painted in the editor.

### Phase 2 — Scatter engine (core, no UI)

- [x] `scatter` module (`src/caffeine/scatter/Scatter.hpp`): jittered grid per class, density from a callback (the biome maps will
      plug in there), exclusion radii, priority order, ownership by position and a margin for chunk borders. Conflicts use a
      **non-recursive rule** (a point yields to any closer point with a higher key, whether or not that one survives), so a chunk
      needs only a margin of two radii and never depends on its neighbours or on load order.
- [x] Output: instance list `(position, up, yaw, scale, variant, sub-seed, tint)`; conformed to height and normal with per-class
      rules (`tiltToNormal`, `sink` per scale, tint jitter, scale range). Rock sink by footprint minimum is still a single
      sample at the centre.
- [x] Tests: no overlap inside and across chunk borders (all pairs over 3 x 3 chunks); same result for any chunking and load order;
      density follows the mask; priority classes are unaffected by lower ones.
- [x] **C ABI v16 and the Doppio *Scatter* action**: `scatterTerrain` (region, chunk-size independent) behind `caffeine_session_scatter_terrain` and
      `caffeine_biome_check`; *Scene > Scatter on Terrain* and the agent action `scatter_on_terrain` place a text plan (`Scatter/*.scatter`) as entities in
      one undo step, with the biome table as density. Tested in `tests/test_scatter.cpp`, `tests/test_capi.cpp` and engine-backed in Doppio
      (`terrain-generator-guide.md`).

### Phase 3 — Nature assets that can be scattered

- [x] **Vertex colour** and a **painted flutter weight** in the Nature mesh output: OBJ `v x y z r g b a`, read by the engine into a
      second vertex stream (no ABI change); wind sway, flutter and translucency are material fields (C ABI v9).
- [ ] **Material slots per submesh** (already the next engine step in the Nature guide).
- [ ] Base deformation: root flare and rock skirt from a ground height callback at bake time.
- [x] Variants: N seeds per nature recipe baked into a cached variant set (`recipe_ref::nature_variants`, `doppio-material variants`).
- [x] Per-instance tint and scale range (`ScatterClass::tintJitter`, `minScale`/`maxScale`). Applying the tint through the Mesh Renderer
      modifier (C ABI v10) is the missing last step of the scatter integration.
- [ ] Ground-contact blend in the shader (rock/trunk to ground colour using vertex colour).

### Phase 4 — Rendering at scale

- [ ] GPU-instanced vegetation with per-chunk instance buffers. The instanced path now also covers meshes with a painted
      colour stream (nature scatter parts are placed as batched copies); the 256-per-batch limit and per-chunk buffers remain.
- [x] **Cluster LOD for opaque parts** (rocks, trunks, boulders): Mesh Renderer -> Micromesh draws a mesh as a GPU-selected cluster cut,
      crack-free, with shadow cascades, vertex colours and sub-meshes, and optional occlusion. Measured on 81 dense rocks and a 300-tree
      forest: 6.1x and 3.3x fewer triangles (`micromeshes.md`, `./scripts/benchmark-micromesh.sh`).
- [ ] LOD chain from the same graph for what Micromesh does not take (alpha-tested foliage cards, wind-animated parts): simplified
      cards and a **billboard impostor atlas** baked per variant, distance and screen-size switching with dithered cross-fade. Also open:
      the Nature generator does not switch Micromesh on for the parts it places (roadmap M-5, "cluster LOD" option).
- [ ] Culling by chunk, then by instance, on the CPU first; GPU culling only if profiling asks for it. (Per-instance frustum culling
      exists and is counted in the frame stats; there are no chunks yet; Micromesh culls clusters on the GPU, with occlusion against last
      frame's depth, for the meshes that opt in.)
- [x] Wind (vertex stage, also in the shadow pass), leaf translucency, a normal map on leaf cards and alpha-tested shadows for
      cut-out cards.

### Phase 5 — Streaming and caching

- [ ] Chunk jobs on the engine thread pool with a priority queue and a per-frame time budget; never block the
      viewport (same rule as the generator bakes).
- [ ] Persistent bake cache keyed by `(seed, chunk, rule hashes, generator_version)`; invalidation when a
      rule file changes.
- [ ] Edit layer: sparse deltas for sculpt, moved or removed instances; applied after generation.
- [x] Engine side of bigger heightfields: the C ABI accepts terrains up to 4097 x 4097 and the chunked LOD draws them; a 1025 x 1025 terrain
      (2.1 M triangles) submits about 0.84 M in about 410 draws (benchmark set). Measured, not just allowed.
- [ ] The terrain generator still outputs 129, 257 or 513 only, and there is no tiling or streaming of a terrain larger than one heightfield
      (the terrain guide still says 513 and lists a tiled or GPU path; its limit text predates the 1025 measurement).

### Phase 6 — Editor integration

- [ ] A **World** tab: seed, layer list, biome table, rule files per class, region painting of overrides.
- [ ] Place and preview a chunk grid in the viewport; show cache hits and chunk build time in the HUD.
- [ ] `UiId` entries, undoable edits (`App::commit`), MCP actions and a headless flow for each step.
- [ ] Docs: update the Nature, Terrain and Water guides and the website pages; regenerate captures through
      the headless driver.

### Phase 7 — Visual quality pass (parallel track)

- [ ] Prefiltered environment cubemap + BRDF LUT (split-sum); keep the current path as fallback. (Today: the equirectangular sky with mips,
      per-object probes and an analytic split-sum approximation of the BRDF term in `scene_lit.frag`.)
- [x] Run-time modifiers (C ABI v10): tint, wetness, snow, age on the Mesh Renderer and the material; scripts can drive them per
      entity. Driving them from terrain masks is open.
- [x] Water on the GPU: Gerstner waves in the vertex shader (no per-frame mesh rebuild), ripple detail, depth colour and foam.
- [x] Generated bark sets (oak, pine, birch) with parallax relief; grass and soil surfaces from the Rock Surface bake are still open.
- [ ] Reference scenes with fixed seeds, rendered headless, used as visual regression baselines. (The benchmark set already
      generates its content by hashes, byte-identically, and its triangle and draw counts reproduce exactly, but nothing compares
      pictures against a stored baseline yet; the water-dark-patch and Micromesh scene tests compare two renders of one run.)

### Where it stands (2026-11-20)

21 items done, 18 open. The finished work is the **foundation and the rendering side**; what is missing is the **world layer** that
connects the generators, which this page called the missing piece from the start.

| Phase | Done | Open | State |
|---|---|---|---|
| 0 Determinism | 5 | 1 | complete for what exists (hash, versions, thread-count tests); the world file does not exist yet |
| 1 Terrain masks | 3 | 1 | **done** (7 masks, C ABI v15, biome table); painted masks and bands open |
| 2 Scatter engine | 4 | 0 | **done**: core, C ABI v16 and the Scatter action place plans by the terrain's masks |
| 3 Scatterable nature | 3 | 3 | vertex colour, variants, tint/scale done; material slots, root flare and ground blend open |
| 4 Rendering at scale | 2 | 3 | wind/translucency/shadows and **Micromesh cluster LOD** done; foliage LOD and impostors, chunk instance buffers open |
| 5 Streaming and caching | 1 | 4 | engine accepts big terrains; no chunk jobs, no bake cache, no edit layer |
| 6 Editor integration | 0 | 4 | **not started** (no World tab) |
| 7 Visual quality | 3 | 2 | modifiers, GPU water, bark done; prefiltered cubemap and image baselines open |

**What is left, in the order that unblocks the most** (each item ships alone):

1. ~~Phase 1, masks to the engine~~ **done**: the scatter module's density callback, the run-time modifiers and the splat/material blend can
   now read masks.
2. ~~Phase 2, the Scatter action~~ **done**: the finished scatter core, the biome table, the variants, the tint and the Micromesh/instanced draw paths
   now show up in a scene (*Scene > Scatter on Terrain*). Its limit is scale: every instance is an entity, which is what phase 4's per-chunk buffers fix.
3. **Phase 3, material slots per submesh** (the renderer already draws sub-meshes and Micromesh handles them; the Nature output and the
   authored-material path still give one material to a mesh) and the Nature generator switching **Micromesh** on for rocks and trunks.
4. **Phase 4, foliage**: simplified cards and a billboard impostor atlas, then per-chunk instance buffers and chunk culling.
5. **Phase 5**: chunk jobs with a time budget, the bake cache, then the edit layer (design the delta format before the editor exists).
6. **Phase 6**: the World tab, once 1 to 5 give it something to drive. Phase 7's image baselines can start now: a scatter scene exists to render
   (`scripts/render-scatter-docs.sh`).

The one decision that is not engineering: **the world file** (`world.cworld`) has no owner yet. Phases 0, 5 and 6 each need it; define its
layers and delta format before phase 5.

## 5. Risks

| Risk | Mitigation |
|---|---|
| Float differences change placement across machines | integer hash, versioned algorithm, golden tests (phase 0) |
| Chunk-border duplicates or gaps | cell ownership and read-only margin (phase 2) |
| Generation hitches while moving | time-budgeted jobs, cache, prefetch ring (phase 5) |
| Many materials per tree break batching | slots per submesh, atlas variants, impostors (phases 3, 4) |
| User edits break determinism | sparse delta layer from day one (phase 5) |
| Scope: seven generators in one world | each phase ships alone and is useful without the next |

## 6. Related

- [`material-pipeline.md`](material-pipeline.md): materials, depth, variation.
- [`nature-generator-guide.md`](nature-generator-guide.md): recipes, LOD and instancing gaps.
- [`terrain-generator-guide.md`](terrain-generator-guide.md): masks, splat layers, limits.
- [`water-generator-guide.md`](water-generator-guide.md): water surface and `water_height`.
- [`graphics-roadmap.md`](graphics-roadmap.md): the renderer side (Micromeshes, Photon).
- [`micromeshes.md`](micromeshes.md): the cluster hierarchy and GPU cut that dense trunks and rocks use now, with the benchmark set.
