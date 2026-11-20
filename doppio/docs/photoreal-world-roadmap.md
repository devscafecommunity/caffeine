# Photoreal procedural world — validation and roadmap

<div class="doc-meta">
  <span class="doc-badge doc-badge--reference">Reference</span>
  <span class="doc-badge">Doppio</span>
  <span>Last updated: <time datetime="2026-11-20">2026-11-20</time></span>
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
| Vegetation/rocks adapted to terrain, scattered by masks | **Missing** | "Scatter over terrain" is listed as not done in both guides |
| Instancing, LOD, impostors for mass vegetation | **Partial** | instanced draws and `MeshLOD` exist; vegetation is merged meshes, no impostors |

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
- [ ] Replace `hash01` use in the streamer with it; keep the old path under a `generator_version` (`hash01` is already 32-bit
      integer arithmetic, so it is stable; switching changes existing worlds, hence the version).
- [x] `generator_version` field in every `generator.*` recipe (nature, material, texture, terrain, water).
- [ ] The same field in bare `.mat` files and in the world file (there is no world file yet).
- [x] Test: derived seeds for a block of cells are identical on 1 and on 7 threads (`tests/test_hash.cpp`).
- [ ] The same test for whole generated chunks, once the streamer uses the new hash.

### Phase 1 — Terrain masks exported to the engine

- [ ] Output node exposes named **masks** (slope, height band, wear, deposit, flow, wetness, curvature) as
      engine-readable 8/16-bit fields, not only the four splat layers.
- [ ] C ABI: read a mask at `(x, z)` and over a rectangle; terrain normal at `(x, z)` (versioned,
      POD structs, tests on both sides as the rules require).
- [ ] Biome layer: a small graph (or table) that turns masks into per-class density maps.

### Phase 2 — Scatter engine (core, no UI)

- [ ] `scatter` module in `src/caffeine`: jittered-grid / Poisson placement per class, density from
      the biome maps, exclusion radii, priority order, cell ownership and margin for chunk borders.
- [ ] Output: instance list `(transform, variant id, sub-seed, tint)`; conform to height and normal with
      per-class rules (tree: slight tilt; rock: sink by footprint minimum; grass: follow normal).
- [ ] Tests: no overlap inside and across chunk borders; same result for any chunk load order; density
      matches the mask statistically.

### Phase 3 — Nature assets that can be scattered

- [ ] **Vertex colour** and a **wind mask** in the Nature mesh output (needs a C ABI version).
- [ ] **Material slots per submesh** (already the next engine step in the Nature guide).
- [ ] Base deformation: root flare and rock skirt from a ground height callback at bake time.
- [x] Variants: N seeds per nature recipe baked into a cached variant set (`recipe_ref::nature_variants`, `doppio-material variants`).
- [ ] Per-instance tint and scale range.
- [ ] Ground-contact blend in the shader (rock/trunk to ground colour using vertex colour).

### Phase 4 — Rendering at scale

- [ ] GPU-instanced vegetation with per-chunk instance buffers (the instanced path exists, with a
      256-per-batch limit; lift it for scatter).
- [ ] LOD chain from the same graph (full, simplified, impostor), distance and screen-size switching with
      dithered cross-fade; billboard impostor atlas baked per variant.
- [ ] Culling by chunk, then by instance, on the CPU first; GPU culling only if profiling asks for it.
- [ ] Wind (vertex stage), leaf translucency and a normal map on leaf cards (listed as missing in the Nature
      guide).

### Phase 5 — Streaming and caching

- [ ] Chunk jobs on the engine thread pool with a priority queue and a per-frame time budget; never block the
      viewport (same rule as the generator bakes).
- [ ] Persistent bake cache keyed by `(seed, chunk, rule hashes, generator_version)`; invalidation when a
      rule file changes.
- [ ] Edit layer: sparse deltas for sculpt, moved or removed instances; applied after generation.
- [ ] Terrain tiling and bigger-than-513 heightfields (the terrain guide already lists a tiled or GPU path).

### Phase 6 — Editor integration

- [ ] A **World** tab: seed, layer list, biome table, rule files per class, region painting of overrides.
- [ ] Place and preview a chunk grid in the viewport; show cache hits and chunk build time in the HUD.
- [ ] `UiId` entries, undoable edits (`App::commit`), MCP actions and a headless flow for each step.
- [ ] Docs: update the Nature, Terrain and Water guides and the website pages; regenerate captures through
      the headless driver.

### Phase 7 — Visual quality pass (parallel track)

- [ ] Prefiltered environment cubemap + BRDF LUT (split-sum); keep the current path as fallback.
- [ ] Per-instance wetness, dust and seasonal tint driven by the same masks.
- [ ] Bark, grass and soil surfaces from the Rock Surface bake (Nature guide).
- [ ] Reference scenes with fixed seeds, rendered headless, used as visual regression baselines.

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
