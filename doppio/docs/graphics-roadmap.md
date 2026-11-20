# Graphics improvement — Micromeshes and Photon roadmap

<div class="doc-meta">
  <span class="doc-badge doc-badge--reference">Reference</span>
  <span class="doc-badge">Engine</span>
  <span>Last updated: <time datetime="2026-11-20">2026-11-20</time></span>
  <span>Source: <a href="https://github.com/devscafecommunity/caffeine/blob/main/doppio/docs/graphics-roadmap.md"><code>graphics-roadmap.md</code></a></span>
</div>

<p class="doc-lede">How to take the renderer from "good forward PBR with a screen-space GI" to dense,
virtualised geometry (<strong>Micromeshes</strong>) and world-space dynamic global illumination
(<strong>Photon</strong>), using only what SDL3 GPU can do on Vulkan, D3D12 and Metal.</p>

Names: the geometry system is called **Micromeshes** and the GI system **Photon**. Both are our own designs.
No code, shader text or data layout is taken from other engines; the public literature (cluster hierarchies,
radiance cascades, voxel clipmaps, visibility buffers) is the only reference. This page is the graphics
companion of [`photoreal-world-roadmap.md`](photoreal-world-roadmap.md), which feeds it with terrain, nature and
materials.

> **Status (2026-11-20):** no phase of this roadmap has started. The items need GPU work (indirect draws, storage buffers,
> culling and voxelisation shaders) that cannot be validated in the headless test setup, so they are not marked done.
> The deterministic-world foundation they share with the world roadmap (`core/Hash.hpp`) is.

## 1. Verdict

**Photon: already under way, finish it.** The engine has a compute radiance-cascade GI with a four-level voxel
far field and multi-bounce feedback (`PhotonCascades.hpp`). The proposal to "build a GI" is mostly
done; the work left is quality, validation and the world-space accuracy items its own header lists.

**Micromeshes: not started, feasible in stages, with one hard limit.** There are no clusters, GPU culling or
indirect draws today. SDL3 GPU has indirect draw calls, storage buffers and compute passes, but **no mesh
shaders and no portable 64-bit atomics**. So the plan is a hardware-raster cluster renderer first (large
win, low risk) and a compute software rasteriser only for sub-pixel triangles later, using 32-bit atomics.

**Dropped as written:** an SDF-based GI (Photon already uses radiance cascades plus voxels; a second GI
would double the maintenance), and "trillions of triangles". The realistic target is **tens of millions of
source triangles in view with a frame-bound cost**, which is what clusters deliver.

## 2. What exists (verified in the tree)

| Area | Status | Where |
|---|---|---|
| GI near field | **Done**: world-aligned cascades K-1..0, fused trace+merge, SH-L1 resolve | `PhotonCascades.cpp`, `photon_cascade.comp` |
| GI far field | **Done (fallback fill)**: four clipmap levels 0.25/0.5/1/2 m, probes with SH, Chebyshev queries | `PhotonCascades.hpp`, `PhotonFarField.hpp` |
| Multi-bounce | **Done**: previous irradiance fed back with a cap and reprojected history | header notes P5 |
| Baked AO | **Done**: BVH/GPU AO bake | `PhotonAOBake.cpp`, `PhotonGpuBake.cpp` |
| Direct light into GI | **Gap**: injection uses a hemispherical sky term, not the real lights and shadows | header "KNOWN GAPS" |
| Voxelisation | **Gap**: screen depth-accumulation fallback, so off-screen and back faces are missing | header "KNOWN GAPS" |
| Motion vectors | **Gap**: history reprojects with the previous matrix only | header P5 |
| Geometry LOD | **Partial**: 3 clustered-vertex LODs per mesh, terrain chunk LODs, CPU frustum cull | `MeshLOD.cpp`, `TerrainLodSystem.cpp` |
| Occlusion | **Partial**: coarse CPU AABB occluders (max 16), Min-Z pyramid for GI only | `CoarseOcclusion.hpp`, `MinZPyramid.cpp` |
| Instancing | **Done, CPU-grouped**: batches of up to 256 by key | `InstanceBatch.hpp` |
| Shadows | **Done**: 4 directional cascades at 2048, point and spot maps | `GpuDirectionalShadowMap.hpp` |
| Post | **Done**: TAA, ACES, bloom, DOF, SSAO, motion blur, auto exposure | `PostProcessStack.cpp` |
| Compute and indirect in the RHI | **Compute yes, indirect no**: SDL3 exposes it, `CommandBuffer` does not wrap it | `rhi/CommandBuffer.hpp` |
| Website page for Photon | **Stale**: says v0.1, no probe field, single bounce; the code is past that | `caffeine-website/.../editor/photon.md` |

## 3. Review of the proposal

* **"Cluster in Rust on a background thread."** Wrong place. Clustering must be an **engine core** module
  (`src/caffeine/micromesh`) behind the C ABI, because the renderer, the `.caf` packer and the runtime (no
  Rust) all need it. The Rust editor only triggers a bake and shows stats. Generators produce plain meshes;
  the cluster build is the last step of the asset pipeline and is cached, never run at game time.
* **"128-256 triangles per cluster, bounds plus geometric error."** Correct. Use 128 triangles and at most
  128 vertices per cluster, a bounding sphere, a normal cone for back-face culling and a **simplification
  error** per cluster group, with the parent's error never smaller than its children's (monotonic), so the
  cut through the hierarchy is crack-free and view-independent.
* **"Compute shaders choose visible clusters and detail."** Correct, and it is the core: two-phase GPU
  culling (previous-frame survivors first, then the rest against a fresh Min-Z pyramid) writing an indirect
  argument buffer. We already have the Min-Z pyramid code for GI to reuse.
* **"Software rasterisation in compute."** Only for triangles that cover under about 4 pixels, and only after
  the hardware path works. It needs a visibility buffer with atomic min on a packed value. Without 64-bit
  atomics, pack 24-bit depth plus 8-bit cluster-local id into 32 bits and resolve the rest in a second pass,
  or skip the software path. Treat it as optional.
* **"Eliminates LOD pop-in."** Only with crack-free simplification plus TAA-friendly dithered transitions.
  It does not remove material or shadow cost: shadow cascades must use the same cluster cut (coarser).
* **"Trillions of polygons."** No. Memory (cluster data streaming) and the per-pixel cost bound it. Plan for
  a fixed budget (visible clusters, rasterised pixels) and measure.
* **"Foliage and alpha."** Cluster rendering suits opaque geometry. Alpha-tested leaves and grass stay on
  the instanced path (with impostors) until a cutout-aware cluster path is justified.
* **GI "route A, SDF".** Not adopted: Photon already covers it with probes plus voxels. What the SDF idea
  adds that we lack is **accurate occlusion from off-screen and back-face geometry**, which the conservative
  triangle voxeliser (below) provides inside the existing design.
* **GI "route B, POM GI".** Only as a **micro-contact** term: the height maps already give POM
  self-shadow and a cavity term (`material-pipeline.md`). Extending that to a small indirect bounce in cavities
  is cheap and local, but it is a detail layer on top of Photon, not a GI.
* **"Anisotropic filtering, ACES, sRGB."** Already in (ACES fitted, sRGB, mip chains). Only the explicit
  anisotropic sampler setting is worth checking.

## 4. Roadmap — Photon (GI)

Photon first: it is closest to done, benefits every scene now, and Micromeshes will need its voxels.

### Phase P-A — Make it correct

- [ ] Inject **real direct light** (sun, point, spot, with the shadow term) into the voxel/probe radiance
      instead of the hemispherical sky term. Test: a red wall lit by the sun tints a nearby white floor.
- [ ] **Motion vectors** pass (per object matrices, previous frame), used by Photon history, TAA and motion
      blur. Remove the "no motion vectors" gap; invalidate history on disocclusion by depth/normal test.
- [ ] Conservative **triangle voxeliser** with dirty-region updates (static geometry once, dynamic per
      frame), replacing the G-buffer fallback. Keep the fallback behind a quality tier.
- [ ] Headless regression scenes: Cornell box, light-through-door, thin wall leak. Fixed seeds, image
      thresholds in `doppio/tests/ui-flows`.

### Phase P-B — Quality and stability

- [ ] Normal-aware spatial-temporal denoise of the SH resolve; clamp fireflies; ghosting metrics in tests.
- [ ] Emissive surfaces as lights (materials already have emissive maps).
- [ ] Sky as a proper source: feed the prefiltered environment (split-sum) to Photon far field and to
      specular IBL, one source of truth.
- [ ] Glossy reflections from the probe field when screen-space tracing misses (replaces the per-object
      probe cubemaps as the fallback, so off-screen reflections stay stable).
- [ ] Per-view scratch resources (the header notes one shared scratch instance); needed for editor
      previews and split view.

### Phase P-C — Scale and cost

- [ ] Quality tiers with measured GPU budgets (Eco/Low/Balanced/High) and a frame-time governor.
- [ ] Clipmap far field following the camera with toroidal updates; larger worlds without re-fill spikes.
- [ ] Feed terrain and vegetation chunks (from the world roadmap) into the voxel volume as they stream in.
- [ ] Update the website Photon page to the real state and add the new knobs to the editor UI with `UiId`s.

## 5. Roadmap — Micromeshes (virtual geometry)

Each phase ships alone and keeps the old path as a fallback behind a flag.

### Phase M-0 — RHI and tooling prerequisites

- [ ] Wrap `SDL_DrawGPUIndexedPrimitivesIndirect` and graphics/compute **storage buffers** in
      `rhi/CommandBuffer` with device capability checks and a fallback when unsupported.
- [ ] GPU timestamp or CPU-side pass timers per stage, visible in the Profiler; a stats overlay
      (clusters in, culled, drawn, triangles, pixels).
- [ ] A reproducible benchmark set (dense rock field, forest, terrain) using `assets/benchmarks`.

### Phase M-1 — Cluster build (offline, core)

- [ ] `micromesh` module: split a mesh into clusters of at most 128 triangles (spatial locality, meshopt-style
      greedy, our own implementation), with bounding sphere, normal cone and local vertex remap.
- [ ] Build a **hierarchy**: group clusters, simplify groups by edge collapse with a quadric error and
      locked boundaries, split again, repeat to a single root. Store per-group error and monotonic
      parent error.
- [ ] `.micromesh` section in `.caf` (versioned, POD) and a CLI in `caf-pack`; deterministic output.
- [ ] Tests: crack-free cuts at random error thresholds, watertightness preserved, triangle count per
      level, byte-identical rebuilds.

### Phase M-2 — GPU culling and hardware draw

- [ ] Upload cluster pages to GPU buffers; per-instance transform buffer (reuse the instance path).
- [ ] Compute pass 1: frustum + normal-cone + screen-space error test against the hierarchy, writing an
      indirect draw list. Compute pass 2: Min-Z occlusion against the pyramid built from pass 1's depth.
- [ ] Draw visible clusters through indirect indexed draws into the existing G-buffer targets, so
      Photon, SSAO, TAA and post keep working unchanged.
- [ ] Shadow cascades draw their own coarser cut; no cracks between the two (same hierarchy).
- [ ] Terrain tiles and Nature rocks/trunks are first adopters; foliage stays instanced.

### Phase M-3 — Streaming and memory

- [ ] Page cluster data in and out by distance and error with a fixed memory budget and an async
      upload queue; never stall a frame.
- [ ] Geometry compression (quantised positions and octahedral normals) and an LRU of pages.
- [ ] Hook the world chunks: a chunk's baked meshes are clustered when first cached and reused afterwards.

### Phase M-4 — Small-triangle path (optional, measure first)

- [ ] Compute rasteriser for clusters whose triangles are under ~4 pixels, writing a visibility buffer
      with 32-bit atomics and a resolve pass that fetches attributes. Gate on a measured win over the
      hardware path on dense rock/bark scenes.
- [ ] Material resolve in screen tiles to avoid one pass per material.

### Phase M-5 — Editor integration

- [ ] Inspector toggle "Micromesh" on a Mesh Filter, a stats panel and a debug view (cluster colours,
      error cut, culled clusters), `UiId` catalogue entries and MCP actions.
- [ ] Nature, Terrain and Water generators export through the cluster build; Nature gets a
      **cluster LOD** option replacing the old billboard-only plan for non-foliage parts.
- [ ] Docs and headless captures (cluster debug view) in the site.

## 6. Shared items

- [ ] **One visibility source**: the cluster cull and Photon voxelisation read the same instance data.
- [ ] **Frame graph light**: explicit pass list with resource lifetimes so culling, G-buffer, Photon,
      shadows and post are ordered and memory is reused; today the order is hand-written.
- [ ] **Quality governor**: one setting maps to Photon tier, shadow resolution, cluster error threshold,
      and runtime scalers (resolution, TAA upsampling).
- [ ] **Reference scenes** with fixed seeds rendered headless and compared as visual baselines.

## 7. Risks

| Risk | Mitigation |
|---|---|
| No mesh shaders, no 64-bit atomics in SDL3 GPU | hardware indirect draws first; software raster optional and 32-bit |
| Cracks between LOD groups | locked group boundaries, monotonic error, tests that cut at random thresholds |
| Photon history ghosts with moving objects | motion vectors, disocclusion tests, regression scenes |
| Voxeliser cost on dynamic geometry | static baked once, dynamic dirty-region updates only |
| Two geometry paths drift apart | flag-gated, same G-buffer output, shared tests |
| Scope | Photon P-A and Micromeshes M-0/M-1 are independent and can run in parallel |

## 8. Suggested order

1. **P-A** (correct Photon: real lights, motion vectors, voxeliser) and **M-0** (RHI indirect, stats), in parallel.
2. **M-1** cluster build, then **M-2** GPU culling on terrain and rocks.
3. **P-B**, then **M-3** streaming tied to the world chunks.
4. **P-C**, **M-5**, and **M-4** only if profiling asks for it.

## 9. Related

- [`photoreal-world-roadmap.md`](photoreal-world-roadmap.md): the content generated for this renderer.
- [`material-pipeline.md`](material-pipeline.md): POM, displacement and height-derived AO.
- [`nature-generator-guide.md`](nature-generator-guide.md): vegetation LOD and instancing gaps.
- [`terrain-generator-guide.md`](terrain-generator-guide.md): tiled and large terrains.
