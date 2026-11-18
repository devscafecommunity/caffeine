# Rust UI frontend (Iced)

Status: **the active editor**. `caffeine-core` is built without ImGui/ImNodes;
the ImGui draw-list adapters live in `caffeine-editor-support`. The Rust
workspace under `doppio/` is the live Doppio frontend and talks to the engine
through `libcaffeine-capi`. The old C++ editor is frozen in `doppio-archived/`
and is no longer part of the CMake build.

## Why Iced

- MIT licensed, suitable for an engine/editor that other people may redistribute.
- `wgpu` renderer, `PaneGrid` docking, dynamic widget trees in plain Rust.
- Official `iced_test` headless simulator (`click`, `typewrite`, `find`,
  snapshots) for deterministic CI tests.
- `iced-mcp` (MIT, iced 0.14) exposes the running widget tree to agents over MCP:
  snapshots, clicks, typing, assertions, flows and `run-suite`.

## Workspace

```
doppio/
  crates/engine/            libloading bindings to libcaffeine-capi
  crates/ui/
    src/windows/            main.rs (app state + PaneGrid), projectloader.rs
    src/uikit/components/   button, panel, toolbar, status, text_field, field_editor,
                            tree_view, component_card/picker, viewport_surface,
                            viewport_shader, rendering_canvas, gizmo, scene_markers,
                            viewport_math, asset_preview,
                            breadcrumb, code_editor
    src/uikit/theme.rs      colours, borders and spacing for the whole workspace
    src/uikit/panels/       hierarchy, sceneviewport, assetmanager, inspector, console,
                            material, material_generator, texture_set, project_manager,
                            script, dryad_nodes
    src/uikit/project.rs    project.caffeine manifest, recursive scan, recent list
    src/uikit/script.rs     open script documents, editor buffers and save/revert
    src/uikit/dryad.rs      Dryad graph, catalog, emitter, .node.json and round-trip
```

- App/UI state lives in `windows/main.rs`; panels and components are
  reusable library modules.
- **Panels compose components; they never build interactive widgets inline.**
  Text fields, field editors, component cards/pickers and buttons live in
  `uikit/components/`; `scripts/check-rust-ui.sh` rejects raw `text_input`,
  `checkbox`, `pick_list`, `slider` or `button` calls in `uikit/panels/`.
- The UI crates never touch FFI directly: only `doppio-engine` loads the
  shared library.
- a five-group `PaneGrid` layout: Hierarchy | Viewport (+ Generator, Texture Set,
  Script, Dryad Nodes) | Inspector (+ Material) on top and a Project-style bottom
  dock with **Assets** (folder tree + file grid) next to the Console;
- **docking**: dragging a tab away from its header shows guides on every pane
  (`uikit/components/dock_overlay.rs`): the centre adds the tab to that pane's
  group, the edge guides split the pane and dock on that side. Panes also have
  a maximise/restore button; open menus only close on a click outside, Escape
  or choosing an item (hover updates from widgets behind never dismiss them);
- the **Assets wizard** (`+ New` / `Import` in the Assets toolbar,
  `uikit/asset_wizard.rs` + `uikit/panels/asset_wizard.rs`): Create (folder,
  Lua, Dryad script; material and texture set open their editors), Import
  (copies files or folders into the current folder, never overwriting; dropped
  files join the queue) and Pack (`caf-pack --input <asset root> --output
  <project>.cap`, found via `CAF_PACK`, `CAFFEINE_ENGINE_ROOT`, a sibling
  `caf-pack/build/src` or `PATH`);
- stable widget ids for the toolbar, inspector inputs and entity rows;
- a headless `iced_test` harness covering editing, panes, console filters,
  hierarchy search, viewport gestures and flight;
- an optional `agent` feature that routes `main` through `iced_mcp::run`.

The C++ boundary:

- `caffeine-core`: engine only, no ImGui/ImNodes.
- `caffeine-editor-support`: ImGui adapters for engine render helpers.
- `caffeine-capi`: the C ABI used by Rust (and any other external frontend).

Run it with:

```bash
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

Materials, procedural library and real depth: [`materials-guide.md`](materials-guide.md).

Drive and inspect the UI without a window (any agent, CI or human; plain JSON
over stdin/stdout, see [`headless-testing.md`](headless-testing.md)):

```bash
./scripts/doppio-headless.sh widgets                      # what is on screen
./scripts/doppio-headless.sh run doppio/tests/ui-flows --format text
./scripts/doppio-headless.sh screenshot --annotate --out /tmp/shots
```

Run the checks used by CI/agents:

```bash
./scripts/check-rust-ui.sh
# cargo fmt --check, cargo test, cargo check --features agent, clippy -D warnings
# plus the caffeine-core ImGui coupling audit
```

## Migration status

### Engine / ImGui split — done

- `caffeine-core` builds without ImGui/ImNodes; `nm` reports 0 ImGui/ImNodes
  symbols in `libcaffeine-core.a`.
- `caffeine-editor-support` contains the engine ImGui adapters
  (`Overlay2D::draw`, `SkyboxRenderer`).
- 18 UI-kit headers and 1 editor-UI header still reference ImGui; that is the
  legacy SDK/editor side, not core.
- `caffeine-runtime` is still an ImGui frontend and needs an RHI render path
  before it can be made UI-free.

Measure the coupling with:

```bash
python3 scripts/audit_core_ui_coupling.py
python3 scripts/audit_core_ui_coupling.py --fail-on-leaks
```

### C ABI / data-oriented boundary — first slice done

- header: `include/caffeine/capi/EngineAPI.h`
- implementation: `src/caffeine/capi/EngineAPI.cpp`
- version: `CAFFEINE_CAPI_VERSION` 2; `caffeine_capi_version()` is exposed to
  frontends and shown as a status-bar badge.
- test: `tests/test_capi.cpp` (`[capi]`, 318 assertions / 13 cases)
- opaque `CaffeineSession`, `CaffeineEntity` and versioned `CaffeineResult`;
- session create/destroy and `.caf` scene load;
- entity count/list/name, create, destroy, rename and selection;
- position/rotation/scale get/set (setting a transform creates one when the
  entity has none), plus resolved world positions
  (`entity_world_position`, `set_entity_world_position`) so groups compose
  position, rotation and scale; the offscreen renderer calls
  `Scene::propagateTransforms` every frame before drawing;
- dirty state and `.caf` save; the Rust toolbar has Save + a dirty marker;
- project asset listing (`set_asset_root`, `refresh_assets`, `asset_at`); the
  Rust Asset Manager shows real files and a file count;
- hierarchy links (`entity_parent`, `set_entity_parent`) with cycle rejection;
  the Hierarchy **+** opens a create-entity palette with built-in prefabs
  (empty/folder, mesh primitives, lights, camera, physics props, player,
  terrain/water) plus a duplicate-selection recipe. Prefabs are applied through
  the same C ABI components and are undoable;
- the Hierarchy panel composes the reusable `tree_view` component: compact
  full-width rows, semantic icons, expand/collapse chevrons, hover/selection
  states and per-row context menus. Rows are draggable: dropping one onto
  another reparents it, and dropping it on the scene root unparents it. The
  Assets browser shares the breadcrumb, text field and asset-preview components;
- PBR material assets (`.mat`): `CaffeineMaterial` mirrors the engine surface
  (albedo, metallic, roughness, reflectance, emission, transmission, clearcoat,
  sheen, iridescence, reflection and UV tiling/offset), load/save, built-in
  presets, assignment to the selected entity's Mesh Filter and a synchronous
  sphere preview rendered by `MaterialPreviewRenderer`.
  `caffeine_texture_generate_map` derives normal, AO, roughness, metallic and
  height maps; `caffeine_texture_pack_maps` writes ORM, Unity HDRP mask and
  Unity metallic-smoothness packings; `caffeine_texture_save_rgba` writes
  generator output. `asset_path`, `asset_project_path` and
  `resolve_path` bridge asset-relative, project-relative and absolute paths;
- component schema and values (`component_type_*`, `entity_component_*`,
  `add/remove_component`); the Inspector is generic and edits every registered
  component (Transform, Camera 3D, lights, renderers, physics, colliders,
  scripts) with scalar, vector, bool, string and enum fields;
- Dryad preview (`caffeine_session_run_dryad`) runs `onCreate` + one
  `onUpdate` on an entity without entering play mode; `caffeine_dryad_available`
  reports whether the interpreter was linked;
- non-blocking render (`render_begin` / `render_poll`): submit a download copy
  and poll its fence instead of waiting for the GPU;
- in-memory scene snapshots (`snapshot` / `restore`): the whole world serialized
  to bytes, used by the editor undo/redo timeline;
- camera get/set (`CaffeineCamera`), driven by viewport gestures and WASD;
- viewport settings (`set/get_viewport_settings`): reference grid, FXAA/TAA and
  internal render scale;
- tick/frame-stat bookkeeping (systems will be wired incrementally);
- no ImGui and no editor headers in the ABI;
- `libcaffeine-capi.so` shared library (CMake target `caffeine-capi`);
- a `libloading` Rust bridge in `doppio/crates/engine/src/lib.rs`, with a
  round-trip integration test that skips when the library is absent;
- the Rust Inspector shows the real C++ position and can nudge it with `+X`;
- 222 Rust tests: 2 engine bridge/snapshot tests, 3 material-preview
integration tests, 216 UI/widget tests (1 ignored docs generator) and 1 doctest.

The app loads a real scene through the ABI:

```bash
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

The hierarchy then shows the real C++ entities (Rock/Tree/...). Without the
shared library it falls back to fake data, so UI tests do not depend on CMake.

Still to add before the Rust UI is fully wired:

- component schemas and field snapshots in POD arrays;
- commands/undo/redo/play-pause and remaining asset operations;
- events (dirty state, selection, logs, asset changes).

The existing `PluginHostApi` is a good function-table precedent. The existing
`DOPPIO_TEST_MODE` JSON loop (`TestRequestHandler`) is a good engine-side test
precedent.

### Component schema and generic Inspector

- The C ABI exposes a component catalog (id, name, fields with kind/limits/
  enum options) plus per-entity instances and field get/set/add/remove.
- The registry covers Transform, Position/Rotation/Scale 3D, Camera 3D,
  Light plus directional/point/spot, Mesh Renderer, Mesh Filter, Rigid Body
  3D/2D, Collider 3D/2D and Script.
- The Inspector is generic but composed as a property editor: an entity header,
  a combined Transform card (position/rotation/scale axes), collapsible
  component cards with icons and compact remove actions, striped property rows,
  right-aligned labels, sliders with values, vector axis groups and a colour
  picker. Each component has a compact remove button and the pane has an Add
  Component picker. Every edit is an undo step.
- The panel itself only composes uikit components (`text_field`,
  `field_editor`, `component_card`, `component_picker`); it contains no raw
  `text_input`/`checkbox`/`pick_list`. `scripts/check-rust-ui.sh` enforces it.
- Window chrome is a **menu bar** (`Scene`, `Edit`, `View`) plus `status` chips;
  every pane body is wrapped by the `panel` component with a title and muted
  subtitle. Colours and spacing come from `uikit/theme.rs` (the check rejects
  hardcoded colours outside it).
- Prefer direct manipulation over button rows: hierarchy rows support click,
  double-click (focus) and right-click (context menu: add/focus/delete);
  viewport gestures are drag/pan/wheel/double-click/right-click. Menu entries
  and `Ctrl` shortcuts cover the rest.

### Project Manager and Assets

- When no `.caf` scene is passed on the command line, Doppio opens its own
  **Project Manager** over the workspace (the editor fixtures and screenshot
  runs pass a scene, so they stay deterministic). The Project menu and
  `Ctrl+O` reopen it at any time.
- The launcher may also pass a project root or `project.caffeine` as the first
  argument. Doppio opens it directly, sets the engine asset root, loads
  `last_scene` and shows the active-project chip instead of the startup dialog.
- The manager has **Create New**, **Open Recent** and **Browse Projects** tabs,
  mirroring the archived startup dialog. Create validates the name, creates
  `assets/raw/{Animations,Models,Materials,Textures,Audio,Prefabs,Sky}`, `assets/processed`,
  `scripts`, `scenes` and `build`, writes `project.caffeine`
  and opens the project. Browse recursively scans a folder up to six levels for
  `project.caffeine`, and Recent reads `~/.config/caffeine/recent.txt`
  (`$CAFFEINE_RECENT_PROJECTS` overrides the file for tests and portable runs).
  Rows select on click and open on double-click; the primary action opens the
  selection.
- Opening a project sets the engine asset root from `paths.assets_raw`, loads
  `last_scene` when it exists, or clears the world and saves a starter
  `scenes/main.caf` for a new project. Saving the scene updates `last_scene` in
  the manifest. The active project is shown as a chip in the title bar.
- The manifest format is the existing `project.caffeine` JSON shape
  (`project_name`, `engine_version`, `paths`, `template_type`, `last_scene`),
  so projects created by the C++ tools and by Doppio are interchangeable.
  `uikit::project` owns create/load/scan/recent helpers and injects the recent
  file in tests so no real user state is touched.
- The **Assets** pane is now a real browser: clickable breadcrumbs, Up/Root,
  refresh, folder-first navigation and a Grid/List switch. Folder navigation is
  derived from `AssetInfo.relative_path`; no new C ABI call is required.
  Search walks the whole asset root (not only the current folder), thumbnails
  and material tints are reused, and the existing internal drag/drop path keeps
  original asset indices so dropping a `.mat` or image on the Hierarchy or
  Inspector still works.

### Script editor and Dryad nodes

- The **Script** pane opens `.lua` and `.dyd` files as tabs, with per-file dirty
  tracking, Save/Revert/Close, line numbers, syntax colouring and the shared
  project path. Scripts open from Assets (single-click for `.lua`; double-click
  for the source of a `.dyd`), the Scene menu, `Ctrl+Shift+E`, or the MCP
  `open_script` action.
- Iced 0.14's stock `text_editor` paints empty under this workspace's
  renderer, so `uikit/components/code_editor.rs` wraps the canvas-based
  `iced-code-editor` widget. The pane still owns the document state, dirty
  checks and disk I/O; the component is the only place that knows the editor
  implementation.
- **Attach Selected** writes the script's project-relative path into the
  selected entity's `Script` component (adding the component when missing);
  **Detach** clears it. Both are undoable scene edits.
- The **Dryad Nodes** pane (`Ctrl+Shift+D`, Scene menu, or a `.dyd` asset)
  edits the same `.dyd` plus its sibling `.node.json`. The Rust `dryad`
  module matches the C++ `NodeEmitter`/`NodeGraphIO`/`NodeRoundTrip` formats
  byte-for-byte: deterministic `// @node` markers, sorted JSON ids/edges and
  `%.17g` numbers. It supports OnUpdate, Set/Get Position, Set/Get Scale,
  Translate, DeltaTime, Constant, Branch and Log, with typed exec/data ports,
  connection validation, inline input fallbacks, Add motion and Sync from
  `.dyd`.
- **Attach** and **Run once** reuse the script component and the C ABI
  `caffeine_session_run_dryad` preview (onCreate + one onUpdate). Saving the
  graph writes both files; opening a `.dyd` also stages its source in the
  Script pane for hand editing. Sources without `// @node` markers stay valid
  Dryad and the graph editor reports the mismatch instead of rewriting them.

### Material studio

- `.mat` files are first-class assets. Clicking a `.mat` in Assets opens the
  **Material** pane (a tab beside the Inspector); clicking a texture creates an
  unsaved material whose albedo map points at it. The pane is a reusable
  `panels/material` composition of `material_preview`, `texture_slot`,
  `card`, `slider`, `color_picker`, `dropdown` and `checkbox` components.
- The preview is a real engine-rendered sphere
  (`caffeine_session_render_material_preview`), 256 px, throttled to ~8 Hz and
  only while the pane is visible. Dragging the image orbits it. The preview
  renderer has its own targets, so it never resizes the scene viewport target.
- Surface controls cover the full CAFMAT3 surface: albedo/alpha, metallic,
  roughness, reflectance, emission, alpha mode/cutoff, UV tiling/offset,
  normal/AO strength, transmission/IOR, clearcoat, sheen, iridescence and
  reflection. Presets keep the material name, maps and UV layout. Normal maps
  can be marked DirectX (green down); the renderer flips green before tangent
  space shading.
- Texture slots (albedo, normal, ORM, emission) open a searchable project
  picker with image thumbnails. The Assets pane filters to textures, materials,
  meshes and scenes and has its own search field, so a material or map is a few
  keystrokes away instead of scrolling through source files. Material cards
  fall back to an albedo swatch when a thumbnail cannot be decoded.
- The map baker writes beside the saved `.mat`: normal from a height/colour
  source (OpenGL or DirectX green), AO from ring-average cavity, roughness
  (inverted albedo luminance) and metallic (bright-surface mask), then packs
  ORM, Unity HDRP mask or Unity metallic-smoothness. Generated normal/ORM maps
  are assigned back to the material; Unity packings are export-only because
  the renderer's ORM slot expects R=AO, G=roughness, B=metallic.
- The Inspector shows a compact Material slot for the selected entity's Mesh
  Filter; clicking it opens the material, `×` unassigns it (undoable).
- Apply assigns the open material through the C ABI (undo step
  `Assign Material`). Scene edits never rewrite `.mat` files, and material
  edits stay unsaved until Save.
- Texture paths are stored project-relative, as existing scene/material files
  do. `asset_path`, `asset_project_path` and `resolve_path` bridge asset
  browser paths, `.mat` paths and absolute paths; `MeshCache` also accepts the
  sample materials' repository-relative `assets/...` prefix.
- The workflow follows the public conventions used by TextureCraft and
  MaterialCraft (PBR channels, texture slots, ORM/Unity packing, live preview)
  but is implemented from Caffeine's own C ABI and renderer; no reference code
  or assets were copied.

### Material generator (node canvas)

- `uikit/components/node_canvas.rs` is a reusable canvas component: nodes are
  graph-space cards with typed input/output ports, cubic wires, selection,
  node dragging, empty-space panning and wheel zoom. It emits
  `Select`, `MoveNode`, `Connect` and `Viewport` events; the caller owns the
  graph model. The same component can back other node tools.
- `uikit/generators.rs` is a small clean-room atomic graph: Color, Image,
  Normal Map, Noise (value/Perlin), Voronoi (cells/distance), Checker, Bricks,
  Gradient, Blend (13 modes plus a scalar mask input), Normal Blend (whiteout/
  linear), Levels, Curve, HSL, Threshold, Blur, Sharpen, Emboss, Directional
  Blur, Edge/Cavity/Distance masks, Warp, Value, Luminance, Dot, an Output node
  with Color/Roughness/Metallic/Height/AO inputs and imported `Unknown`
  placeholders. Ports are typed colour/scalar; scalar inputs read the luminance
  of connected colour maps and fall back to Output constants. Evaluation is
  CPU, tileable at 128–2048 px, cycle-safe and memoized; 2K is the safe max
  until the evaluator gets a tiled/parallel 4K path.
- The **Generator** tab (opened from Material → Generate…, the Scene menu, or
  the MCP action) puts the palette, canvas and selected-node properties side by
  side and maximizes the pane while editing. Image and Normal Map nodes use the
  shared texture picker and support UV/planar/tri-planar/cylindrical/spherical
  projection, tiling, offset, linear/nearest filtering and clamp/repeat
  wrapping. **Bake to Material** runs on a worker thread with progress and
  cancel; image decoding also happens on that worker. The progress bar follows
  the reachable nodes, then reports `Packing maps`, `Baking occlusion`,
  `Writing maps` and `Done`, so the final AO and map packing no longer look
  stuck at the last node. Occlusion uses a separable box blur (O(pixels)
  instead of O(pixels × radius)), which keeps 2K bakes fast. It writes
  `<material>_gen_base_<bits>.png`, height/roughness/metallic/packed maps,
  derives a normal map from height and assigns the maps to the open `.mat`
  before marking it unsaved. A successful bake now saves the `.mat` and assigns
  it to the selected entity automatically, so the 3D viewport and the
  generator's **3D PREVIEW** (the live material preview sphere) show exactly
  what the graph produced. The generator's **OUTPUT PREVIEW** is a low-resolution
  live bake of the Output node, refreshed after each graph edit with a 160 ms
  debounce; no manual bake is needed to see parameter/brick/mask changes. Direct
  specular is energy-normalised and the height field darkens direct, ambient,
  IBL and reflected light (and point/spot POM shadows), so a face pointing at
  the sun keeps its grooves instead of flattening to a white plastic plate. Every
  node also shows a live 72 px thumbnail of its own evaluated output, computed
  in the same worker pass. The right-hand panel edits the channel-packing
  preset/sources, saves named user presets and chooses 8/16-bit output.
- Saving a material also writes its `<name>.mgraph` sidecar (versioned text)
  beside the `.mat`; reopening restores nodes, links, parameters, positions and
  resolution. Graphs are per material, not shared session state.
- Graph edits have their own snapshot timeline (`GraphHistory`), separate from
  scene undo. Add/delete node, connect, disconnect, move, parameters,
  resolution, copy/paste, image assignment and reroute dots are undoable;
  drags and repeated parameter changes coalesce. `Ctrl+Z` / `Ctrl+Shift+Z` /
  `Ctrl+Y` route to graph history while the Generator pane is active, and the
  header has ↶/↷ buttons.
- Wires are hit-tested as bézier curves. Clicking selects a wire (the
  properties panel offers Delete link); right-click deletes it; dragging a
  connected input detaches its source so it can be dropped on another input.
  Double-clicking a wire inserts a pass-through **Dot** reroute node, with the
  original source and target rewired in one undo step.
- The palette uses the shared `tree_view` grouped by category (Textures,
  Generators, Filters, Utilities, Output), with collapsible groups and search
  filtering. Nodes snap to a 16-unit grid (toggle), **Fit** frames the whole
  graph, and Copy/Paste duplicates the selected node with a 24-unit offset.
- `node_canvas` ignores mouse presses whose cursor is outside its bounds unless
  a drag is already active, so clicking property sliders never starts an
  empty-canvas pan or clears the selected node.
- The Material panel's **AO from scene** button calls Photon's BVH/GPU AO bake
  for the selected entity and assigns the resulting ORM map to the open
  material (`caffeine_session_bake_ao`).
- `uikit/materialcraft.rs` reads/writes portable `.mcmat` archives (ZIP with
  `material.json`, cooked maps and `source.sbs`), parses the SBS XML subset and
  exports a minimal SBS source that is re-imported and checked before writing.
  Unknown filters/outputs become `Unknown` placeholder nodes with warnings.
- `uikit/materialcraft_io.rs` maps imported maps into a `.mat` (packing separate
  AO/roughness/metallic maps into ORM) and writes `.sbs`, `.mgraph` and map
  files. The editor exposes MCP `import_materialcraft` /
  `export_materialcraft`; the headless binary is `doppio-material`.
- The generator follows MaterialCraft's public primitive-editor workflow
  (palette → canvas → properties, typed ports, output node, live baked maps)
  without sharing its code, node definitions or assets.
- The remaining material/texture/generator work is tracked in
  [`material-pipeline.md`](material-pipeline.md): scene/geometry bakes,
  MaterialCraft SBS import and TextureCraft-style texture sets/painting.

### Texture sets (TextureCraft-style foundation)

- `uikit/texture_set.rs` owns UDIM tiles 1001–1010, six default channels
  (baseColor, roughness, metallic, height, AO, curvature), paint/fill layers,
  per-tile layer masks, brush settings, compositing and per-tile undo snapshots.
- The **Texture Set** pane (Material toolbar → Texture Set, or the MCP
  `new_texture_set` action) stacks tiles/layers on the left, a reusable
  `uv_canvas` in the centre and channels/brush/export controls on the right.
  The pane maximizes while painting; Material restores the normal layout.
- `uv_canvas` paints RGBA or layer-mask dabs with brush radius, hardness,
  opacity and colour. **3D paint** toggles viewport painting: `pick_uv` in the
  C ABI ray-picks the selected mesh against the editor camera and returns the
  hit UV; strokes are written to the active tile/layer and the resulting base
  colour is applied to the entity for viewport feedback.
- **AO from height** and **Curvature** bake new layers; **AO from scene** calls
  Photon and imports the ORM red channel. **Normal**, **Position** and **ID**
  call `caffeine_session_bake_mesh_map` and import world-normal,
  world-position or object-ID UV maps into matching channels. All imports keep
  per-tile undo.
- The `.texset` project is a ZIP with `texture-set.json`, RGBA PNG tiles and
  grayscale mask PNGs. `TileHistory` snapshots only the affected tile (cap 64)
  so undo never copies the whole project.
- Export presets: separate PBR maps, glTF ORM and Unity HDRP mask. The
  `doppio-material texset-*` CLI and MCP `export_texture_set` use the same code.

### Phase 5 polish

- Material preview: sphere/cube/plane subjects, HDRI path + exposure controls,
  and an asynchronous begin/poll readback (`render_material_preview_begin` /
  `_poll`) so orbit drags and edits do not block the UI. It renders at 512 px
  and uses contain-fit with a wider camera so the subject is never cropped.
  The old synchronous entry point remains for tools and tests.
- Thumbnails now decode through the engine (`caffeine_texture_load_thumbnail`)
  before falling back to Iced's path loader, which fixes PNG/JPG/HDR assets that
  Iced could not decode. The asset grid, material slots and pickers share this
  path; the cache is keyed by resolved path + modification stamp so refreshing
  an asset list only re-decodes thumbnails that changed.
- Inspector **Choose material…** opens the searchable `.mat` picker. Assets can
  be dragged from the Assets grid onto a Hierarchy entity (material assignment
  or material-from-texture) or onto Material map slots (texture assignment).
  OS file drops still assign `.mat`/textures and import mesh files.
- `uikit/mesh_material_import.rs` imports OBJ/MTL and `.gltf`/`.glb` materials
  (albedo, normal, occlusion, roughness/metallic factors and maps, emission,
  alpha and `KHR_texture_transform` UV tiling/offset) into `.mat` assets and
  writes an `.mgraph` sidecar with an Image node for albedo. MCP
  `import_mesh_materials` and file drop use the same importer.
- Material badges show map presence with decoded resolution or **missing**,
  plus the normal convention and alpha mode; the status bar shows the C ABI
  version (now **v2**).
- **Apply material** saves the open material first, so baked generator maps and
  complex masks reach the `.mat` file before assignment. Applying to an entity
  without a Mesh Filter now creates a visible cube preview mesh instead of
  failing silently.

### Undo and redo

- `doppio/crates/ui/src/history.rs` keeps a timeline of editor states: the UI
  entity list, an optional engine snapshot, the dirty flag and the selection.
- States are full scene snapshots rather than per-field commands, so undo also
  covers components the UI does not edit yet.
- Steps are named (`Add Entity 3`, `Rename Boss`, `Move Rock to x=…`), typing in
  one field coalesces into a single step, and a new edit drops the redo branch.
- Cap: 200 steps / ~128 MB; the oldest steps are dropped first.
- Toolbar Undo/Redo, Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y, and MCP `undo` / `redo`
  actions. The `history` query returns the timeline, cursor and can-undo/redo.
- Snapshot restore remaps entity ids, so the UI re-reads the hierarchy; the
  serializer writes and recreates entities in stable id order to preserve it.

### Viewport — asynchronous CPU readback

- `RenderDevice::initOffscreen()` creates a headless SDL_GPU device.
- `caffeine_session_render()` is the blocking path (tools/tests): render with
  `GpuSceneRenderer::renderWithCamera` into an RGBA8 target and read it back.
- `caffeine_session_render_begin()` renders, starts a texture→transfer-buffer
  download and returns immediately with a fence. `RenderDevice::endOffscreen()`
  does **not** call `SDL_WaitForGPUIdle`; the C API keeps one offscreen frame
  in flight and releases upload buffers when its readback fence signals.
- `caffeine_session_render_poll()` returns `PENDING` until the fence is
  signalled, then maps the transfer buffer and copies the pixels.
- The transfer buffer is reused across frames instead of being allocated and
  destroyed every frame. A 33 ms render interval and a once-per-second HUD
  update keep the UI thread from being flooded.
- Finished frames are drawn by `uikit/components/viewport_shader.rs`, not by a
  new Iced image handle per frame. The shader owns one persistent wgpu texture
  and writes into it only when the frame id changes, so animated updates no
  longer create/destroy textures or flicker on the compositor.
- `doppio-engine::Engine::{render, render_begin, render_poll}` wrap the ABI.
- `uikit/components/viewport_surface.rs` owns the interactive surface: image or
  empty state, HUD chips (resolution, grid, latency, FPS, camera), mouse
  handling (left drag orbit, right/middle drag pan, wheel zoom, double-click
  focus) and grab/grabbing cursor feedback.
- The scene viewport reports its real pane size through an Iced `sensor`; the
  app renders at 2x logical pixels (capped at 3 megapixels) so the image stays
  sharp after Iced scales it back down. The C ABI adds 1.5x internal
  supersampling and FXAA (TAA/off are also exposed), and enables the GPU reference grid with adaptive cell
  spacing and coloured X/Z axes. The grid fades out between 1,500 and 5,000
  units of visible range: at extreme zoom its screen-space lines sit almost
  coplanar with distant terrain and alias into dashed confetti. The near plane
  also scales with orbit distance so a 50k far plane does not destroy depth
  precision. The View menu (or `G` over the viewport) toggles the grid.
- `WASD` flies along the view direction, `Q`/`E` move vertically and `Shift`
  boosts speed. The camera focus is never height-clamped; the orbit pitch only
  stops just short of the pole so `lookAt` stays stable. That allows reaching
  cloud layers and orbiting beyond the old 5,000-unit zoom limit.
- `uikit/components/gizmo.rs` draws the selected entity's transform gizmo over
  the shader surface using the same orbit camera projection (shared maths in
  `components/viewport_math.rs`). `1`/`2`/`3` (or View → Move/Rotate/Scale Tool)
  switch modes. Move has shaded 3D arrows and plane handles, Rotate has depth-shaded
  rings with a swept-angle wedge, and Scale follows the object's **local** axes and has a
  centre cube for uniform scale. The handle under the cursor is highlighted. Edits flow
  through the normal commit path and coalesce into one undo step per handle drag. The
  overlay projects the entity's resolved world position, so a child stays attached to a
  moved/rotated/scaled group, and move edits are converted back through the parent
  transform. It projects with the camera snapshot of the frame currently on screen, not
  the UI's newest camera, so an orbiting world and its gizmo stay aligned despite the
  asynchronous readback.
- `uikit/components/scene_markers.rs` draws markers for entities that have no mesh: light
  rays, bulbs and cones, camera frustums, colliders and locators. They sit under the gizmo,
  can be clicked to select their entity and are toggled from the View menu. Details, limits
  and images: [`gizmos-and-markers.md`](gizmos-and-markers.md).
- `ViewportTick` still fires every 16 ms, but the viewport submits at most one
  frame per 33 ms; the HUD shows measured latency and a rolling FPS. The
  persistent shader texture is kept when a frame is unchanged, so dropped
  frames can never show a partial or empty viewport.
- Camera maths (orbit, pan basis, fly movement, zoom, focus selection) lives in
  `windows/main.rs`; there is no camera button row. Gestures and shortcuts
  (`Ctrl+F` focus, `Ctrl+0` reset, `Ctrl+R` render) drive it.

The viewport shows the real C++ scene (terrain, rocks, sky) in Iced.

### Console

Entries are typed as Errors, Log, Actions, Engine or UI and prefixed in the
list. The filter chips hide/show each category independently; the scrollable
anchors to the newest entry.

### Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+N` | add empty entity |
| `Ctrl+Shift+N` | open the create-entity palette |
| `Ctrl+Shift+M` | new material |
| `Ctrl+S` | save scene |
| `Ctrl+Enter` | play / pause |
| `Ctrl+F` | focus selection |
| `Ctrl+0` | reset camera |
| `Ctrl+R` | render viewport now |
| `G` (over the viewport) | toggle the reference grid |
| `1` / `2` / `3` (over the viewport) | move / rotate / scale gizmo |
| `W` / `A` / `S` / `D` | fly along the view / strafe |
| `Q` / `E` | fly down / up |
| `Shift` | boost flight speed |
| `Ctrl+1` / `Ctrl+2` | default / viewport panel layout |
| right-click hierarchy row | add / focus / delete context menu |
| double-click hierarchy row or viewport | focus selection |
| left drag / right drag / wheel | orbit / pan / zoom |

### Why not a shared GPU texture yet

SDL_GPU exposes no native resource handles (no `VkImage`, D3D12 resource or
shared handle), so Iced/wgpu cannot import the engine's colour target through
the public API. The async readback above removes the GPU stall; the copy
remains. Real zero-copy requires one of:

1. a wgpu RHI backend for the engine (or rendering the scene in wgpu directly);
2. Vulkan external memory via a custom SDL_GPU patch/extension;
3. an embedding strategy with a separate SDL3 viewport window.

Until then, keep the readback path responsive and treat the texture hand-off as
a renderer-level project, not a UI change.

### Plugins

Plugins are C++ and draw ImGui. The Rust UI needs one of:

- a legacy ImGui host window for old plugins; or
- a plugin v2 contract (data/commands, optional process/WASM isolation).

This is the largest hidden cost and should be designed before removing ImGui
from the runtime and the legacy SDK.

### Tests and agents

UI tests are Playwright-style but headless: they drive the real Iced widget
tree, click by id, type into inputs, apply the resulting messages and assert on
the next frame. No window, GPU or engine build is required.

- `crates/ui/src/windows/main/tests.rs` has a `Harness` with `click`, `type_into`,
  `shows`, `has`, `bounds`, `capture` and `wait_for`, plus focused tests and one
  end-to-end `editor_workflow` scenario (add → select → rename → move → delete).
- Fixtures (`App::empty` and two-entity editor) keep tests deterministic.

### UI ids, bounds and visual captures

Iced ids are flat strings, which does not scale to hundreds of widgets. Doppio
uses a typed taxonomy in `uikit::id`:

```
group.subgroup.object        e.g. editor.toolbar.add_entity
                                  inspector.entity.name
                                  hierarchy.list.entity_row.3
                                  assets.grid.asset_card.7
```

- `UiId` implements `Into<iced::widget::Id>`, so components take ids directly.
- Static ids are constants (`id::ADD_ENTITY`, `id::PANE_INSPECTOR`, …).
- List rows use `id::entity_row(index)` / `id::asset_card(index)`; the group
  and subgroup stay stable while the object carries the index.
- `id::CATALOG` documents every stable widget with a description and kind, and
  `id::description(key)` resolves dynamic rows too.
- Panes are wrapped in an id'd container, so a screenshot can target
  `editor.pane.inspector` as naturally as a button.

The native harness exposes the layout of any id (`Harness::bounds`) and writes
annotated PNGs:

- `Harness::capture(id, path, caption)` crops a headless `iced_test` frame to
  the target, adds a highlight border and a caption rendered with the bundled
  JetBrains Mono (`assets/jetbrains-mono/`).
- `Harness::capture_group(&[(id, label)], …)` crops the union of several
  widgets, draws numbered colored boxes and a legend — the "annotated panel"
  format.

The gallery generator walks the catalog, captures every widget that exists in
the fixture, and writes images, a JSON manifest and English/Portuguese
Markdown:

```bash
./scripts/generate-doppio-ui-docs.sh            # doppio/target/doppio-ui-docs
./scripts/generate-doppio-ui-docs.sh --publish  # update caffeine-website
```

Descriptions and translations live in `doppio/docs/ui-captions.json`, so an
agent can improve the prose without touching Rust. The manifest
(`manifest.json`) carries id/group/kind/bounds/image for each entry.

Agents get the same views through iced-mcp: `snapshot` returns ids and bounds,
`screenshot` accepts a target id plus `highlight` (or a region) and saves a
PNG, and `ui_catalog` lists the documented ids, groups and descriptions.

### Agents (MCP)

`iced-mcp` is behind the `agent` feature and attaches only when `ICED_MCP=1`:

```bash
cargo install iced-mcp-server        # once
cd doppio
ICED_MCP=1 cargo run -p doppio-ui --features agent
```

The MCP client (Claude Code, Cursor, any MCP client) launches the app, reads
the widget tree (`snapshot`), clicks/types by id or text, records flows and
replays them with `iced-mcp run-suite`. On top of the widget tools, Doppio
registers scene-level queries and actions:

| Kind | Name | What it does |
|------|------|--------------|
| state | — | engine connection, entity/asset counts, selection, play state, dirty flag, camera |
| query | `entities` | every entity with id, name, transform and selection |
| query | `selection` | the selected entity (local transform and `world_position`) |
| query | `scene` | scene path, dirty flag, counts and the console log |
| query | `assets` | project assets with path, kind, size and directory flag |
| query | `project` | active project, manager tab, recent/browse results and asset navigation |
| query | `script_editor` | open script documents, active tab, paths and dirty state |
| query | `dryad` | Dryad script, graph nodes/edges, selection, dirty state and status |
| query | `camera` | viewport yaw, pitch, distance and focus |
| query | `viewport` | pane/render size, grid, AA, render scale and hover |
| query | `gizmo` | active transform tool and selected transform |
| query | `markers` | scene markers (id, name, kind, world position) and their visibility |
| action | `toggle_scene_markers` | shows or hides the scene markers |
| action | `select_marker` | `{ "id": entity id }`, like clicking the marker icon |
| query | `prefabs` | built-in prefab categories and recipes |
| query | `materials` | project `.mat` assets |
| query | `material` | open material surface, maps, path and dirty state |
| query | `material_presets` | built-in PBR preset names and categories |
| query | `generator` | generator graph nodes, typed ports, packing, resolution and bake state |
| query | `texture_set` | UDIM tiles, channels, layers, masks, brush and undo state |
| query | `layout` | pane list and kinds |
| action | `toggle_grid` | shows or hides the reference grid |
| action | `open_project_manager` | opens the Project Manager dialog |
| action | `create_project` | `{ "name", "location"?, "template"?: "Empty"\|"2D"\|"3D" }` |
| action | `open_project` | `{ "path": "project root or project.caffeine" }` |
| action | `open_script` / `open_dryad` | `{ "path": "script.lua" \| "script.dyd" }` |
| action | `dryad_add_node` / `dryad_connect` / `dryad_set_number` / `dryad_set_bool` / `dryad_set_text` | graph editing |
| action | `dryad_save` / `dryad_sync` / `dryad_delete` / `dryad_fit` / `dryad_add_motion` | graph file and layout actions |
| action | `dryad_attach` / `dryad_run` | attach or preview the open Dryad script |
| action | `asset_navigate` / `asset_up` / `asset_root` | asset-browser folder navigation |
| action | `refresh_assets` / `set_asset_view` | rescan assets / `{ "mode": "grid"\|"list" }` |
| action | `create_prefab` | `{ "key": "cube" \| "light" \| ... }` |
| action | `open_material` | `{ "path": "materials/name.mat" }` |
| action | `import_mesh_materials` | `{ "path": "model.obj\|model.gltf\|model.glb" }` |
| action | `new_material` / `save_material` / `apply_material` | material workflow |
| action | `set_material_texture` | `{ "slot": "albedo\|normal\|orm\|emission\|source", "path" }` |
| action | `generate_material_maps` | `{ "operation": "normal\|ao\|roughness\|metallic\|orm\|all" }` |
| action | `open_generator` | opens the material node canvas |
| action | `add_generator_node` | `{ "kind": "image\|noise\|voronoi\|checker\|bricks\|gradient\|blend\|levels\|curve\|hsl\|threshold\|blur\|warp\|value\|luminance\|color\|dot\|output" }` |
| action | `connect_generator` | `{ "from", "to", "input"? }` |
| action | `delete_generator_link` | `{ "to", "input" }` |
| action | `insert_generator_dot` | `{ "to", "input", "x"?, "y"? }` |
| action | `generator_undo` / `generator_redo` | graph history |
| action | `copy_generator_node` / `paste_generator_node` | node clipboard |
| action | `fit_generator` / `set_generator_snap` / `set_generator_search` | canvas view options |
| action | `set_generator_image` | `{ "node", "path" }` image-node texture |
| action | `set_generator_bits` | `{ "bits": 8 \| 16 }` generated map depth |
| action | `set_generator_packing_preset` | `{ "preset": "orm\|hdrp\|metallic_smoothness\|custom" }` |
| action | `set_generator_packing_channel` | `{ "channel": "r\|g\|b\|a", "source": "ao\|roughness\|metallic\|height\|smoothness\|base_luminance\|zero\|one" }` |
| action | `bake_scene_ao` | `{ "resolution"?, "rays"?, "max_distance"? }` Photon AO for the selected entity |
| action | `new_texture_set` / `save_texture_set` / `open_texture_set` | texture-set project workflow |
| action | `paint_texture_set` | `{ "tile"?: 1001, "u": 0..1, "v": 0..1 }` one brush dab |
| action | `texture_set_undo` / `texture_set_redo` | per-tile paint history |
| action | `texture_set_bake_ao` / `texture_set_bake_curvature` | height-derived bakes |
| action | `texture_set_bake_scene_ao` | Photon AO into the texture set |
| action | `texture_set_bake_mesh_map` | `{ "kind": "normal"\|"position"\|"id" }` |
| action | `export_texture_set` | `{ "path"?: directory }` selected export preset |
| action | `import_materialcraft` | `{ "path": "file.mcmat" }` |
| action | `export_materialcraft` | `{ "path": "file.mcmat" }` |
| action | `bake_generator` | starts the background bake |
| action | `cancel_generator_bake` | cancels the running bake |
| action | `open_prefab_palette` | opens the create-entity palette |
| action | `set_gizmo_mode` | `{ "mode": "move"\|"rotate"\|"scale" }` |
| action | `set_viewport` | `{ "grid"?, "anti_aliasing"?, "render_scale"? }` |
| action | `set_entity_parent` | `{ "entity_id", "parent_id"?: number \| null }` |
| query | `ui_catalog` | every documented widget id with group, kind and description |
| query | `component_types` | component schema: fields, kinds, limits, enum options |
| query | `entity_components` | components and values for an entity |
| query | `history` | undo/redo timeline, current cursor, names and size |
| action | `add_component` / `remove_component` | edits the selected entity's components |
| action | `set_component_field` | `{ "component", "field", "kind", "value" }` |
| action | `undo` / `redo` | moves through the editor timeline |
| action | `add_entity` | `{ "name"?: string }` |
| action | `delete_selected` | deletes the selected entity |
| action | `select_entity` | `{ "index"?: number, "name"?: string }` |
| action | `rename_selected` | `{ "name": string }` |
| action | `toggle_play` | toggles play/pause and ticks the engine |
| action | `save_scene` | saves the current `.caf` |
| action | `set_camera` | `{ "yaw"?: number, "pitch"?: number, "distance"?: number }` |
| action | `reset_camera` | restores the default camera |
| action | `close_scene` | drops the engine session and clears the editor |
| fixture | `empty`, `editor` | deterministic starting states for flows |

Because every action is an iced `Message`, agents can only do what the UI can
do, and the `messages` tool shows exactly what each call published. Agent edits
are ordinary undo steps (`undo` / `redo`), and the `history` query exposes the
timeline.

### Visual regression

`doppio/.iced-mcp/scripts/visual-regression.luau` captures the hierarchy,
assets, inspector and console panes with `screenshot_diff` (the GPU viewport is
excluded on purpose). Baselines are per-machine/GPU and live in
`.iced-mcp/baselines/`, which is gitignored:

```bash
./scripts/check-doppio-visual.sh --update-baselines  # seed on this machine/image
./scripts/check-doppio-visual.sh                     # CI check
```

The script skips cleanly without `iced-mcp` or a display. Commit baselines only
from a controlled CI image if you want cross-machine visual diffs.

A full scripted session lives in `doppio/.iced-mcp/scripts/editor-smoke.luau`.
`material-smoke.luau` covers opening a `.mat`, the rendered preview, assigning it
to an entity, presets and texture slots; `material-generator-smoke`,
`materialcraft-smoke` and `texture-set-smoke` cover the node canvas, portable
round-trip and texture-set painting/export; `project-smoke` covers creating and
reopening a project plus asset navigation; `script-dryad-smoke` covers the
Script Editor, Dryad graph save and the one-frame preview; `prefab-smoke`,
`hierarchy-smoke`, `components-smoke`, `viewport-smoke` and `tabs-smoke` cover
the rest.
It loads the `editor` fixture, renames through the real text input, adds an
entity, drives the camera and asserts the console:

```bash
cd doppio
iced-mcp run-script .iced-mcp/scripts/editor-smoke.luau \
  --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf"
```

`scripts/doppio-agent.sh` builds the C ABI and launches the same app with
`ICED_MCP=1` for an interactive agent session.

### MaterialCraft headless bridge

```bash
cargo run -p doppio-ui --bin doppio-material -- \
  inspect ../assets/vegetation-kit/materials/celandine_01.mcmat
cargo run -p doppio-ui --bin doppio-material -- \
  import material.mcmat --out imported/
cargo run -p doppio-ui --bin doppio-material -- \
  import-sbs material.sbs --out imported/
cargo run -p doppio-ui --bin doppio-material -- \
  export materials/paint.mat --out paint.mcmat
cargo run -p doppio-ui --bin doppio-material -- \
  bake-graph materials/paint.mgraph --size 2048 --out maps/
cargo run -p doppio-ui --bin doppio-material -- \
  texset-info painted.texset
cargo run -p doppio-ui --bin doppio-material -- \
  texset-new --out painted.texset --size 1024
cargo run -p doppio-ui --bin doppio-material -- \
  texset-export painted.texset --out maps/ --preset "glTF ORM"
```

The CLI and editor MCP actions share `materialcraft_io`; the portable export
validates its own SBS node/link round-trip and reports imported placeholders.

Session teardown keeps the SDL GPU device for the process lifetime on purpose:
`SDL_DestroyGPUDevice` unloads Vulkan entry points that the host's wgpu/Iced
renderer still holds. Sessions are created rarely, so the editor leaks one
small device instead of crashing the host.

- Keep/extend the engine JSON protocol for scene/component/viewport assertions.
- Add fixtures for project templates, scenes and component schemas.

## Rules

- Do not re-add the C++ editor to the live tree; use `doppio-archived/` as a
  behaviour reference only.
- Add widget ids for anything an agent must find.
- Prefer snapshots/arrays over per-field FFI calls.
- Keep the Rust frontend out of the CMake build; `scripts/check-rust-ui.sh`
  builds the C ABI first and then the workspace.
