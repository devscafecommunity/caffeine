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
                            component_card/picker, viewport_surface, rendering_canvas,
                            gizmo, asset_preview
    src/uikit/theme.rs      colours, borders and spacing for the whole workspace
    src/uikit/panels/       hierarchy, sceneviewport, assetmanager, inspector, console
```

- App/UI state lives in `windows/main.rs`; panels and components are
  reusable library modules.
- **Panels compose components; they never build interactive widgets inline.**
  Text fields, field editors, component cards/pickers and buttons live in
  `uikit/components/`; `scripts/check-rust-ui.sh` rejects raw `text_input`,
  `checkbox`, `pick_list`, `slider` or `button` calls in `uikit/panels/`.
- The UI crates never touch FFI directly: only `doppio-engine` loads the
  shared library.
- a five-pane `PaneGrid` layout (Hierarchy / Assets / Viewport / Inspector /
  Console);
- stable widget ids for the toolbar, inspector inputs and entity rows;
- seven `iced_test` cases: add, play/pause, nudge X, nudge rotation, scale up,
  rename and orbit;
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
- test: `tests/test_capi.cpp` (`[capi]`, 70 assertions / 4 cases)
- opaque `CaffeineSession`, `CaffeineEntity` and versioned `CaffeineResult`;
- session create/destroy and `.caf` scene load;
- entity count/list/name, create, destroy, rename and selection;
- position/rotation/scale get/set (setting a transform creates one when the
  entity has none);
- dirty state and `.caf` save; the Rust toolbar has Save + a dirty marker;
- project asset listing (`set_asset_root`, `refresh_assets`, `asset_at`); the
  Rust Asset Manager shows real files and a file count;
- hierarchy links (`entity_parent`, `set_entity_parent`) with cycle rejection;
  the Hierarchy panel renders a tree with expand/collapse chevrons;
- component schema and values (`component_type_*`, `entity_component_*`,
  `add/remove_component`); the Inspector is generic and edits every registered
  component (Transform, Camera 3D, lights, renderers, physics, colliders,
  scripts) with scalar, vector, bool, string and enum fields;
- non-blocking render (`render_begin` / `render_poll`): submit a download copy
  and poll its fence instead of waiting for the GPU;
- in-memory scene snapshots (`snapshot` / `restore`): the whole world serialized
  to bytes, used by the editor undo/redo timeline;
- camera get/set (`CaffeineCamera`), driven by the viewport toolbar;
- tick/frame-stat bookkeeping (systems will be wired incrementally);
- no ImGui and no editor headers in the ABI;
- `libcaffeine-capi.so` shared library (CMake target `caffeine-capi`);
- a `libloading` Rust bridge in `doppio/crates/engine/src/lib.rs`, with a
  round-trip integration test that skips when the library is absent;
- the Rust Inspector shows the real C++ position and can nudge it with `+X`;
- 44 Rust tests: 2 engine bridge/snapshot tests, 41 UI/widget tests (1
ignored docs generator) and 1 doctest.

The app loads a real scene through the ABI:

```bash
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

The hierarchy then shows the real C++ entities (Rock/Tree/...). Without the
shared library it falls back to fake data, so UI tests do not depend on CMake.

Still to add before the Rust UI is fully wired:

- component schemas and field snapshots in POD arrays;
- commands/undo/redo/play-pause and asset operations;
- events (dirty state, selection, logs, asset changes);
- a project loader (open/create project) in the Rust UI.

The existing `PluginHostApi` is a good function-table precedent. The existing
`DOPPIO_TEST_MODE` JSON loop (`TestRequestHandler`) is a good engine-side test
precedent.

### Component schema and generic Inspector

- The C ABI exposes a component catalog (id, name, fields with kind/limits/
  enum options) plus per-entity instances and field get/set/add/remove.
- The registry covers Transform, Position/Rotation/Scale 3D, Camera 3D,
  Light plus directional/point/spot, Mesh Renderer, Mesh Filter, Rigid Body
  3D/2D, Collider 3D/2D and Script.
- The Inspector is generic: it lists every component with scalar, vector,
  bool, string and enum editors, plus Remove per component and an Add
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
  download and returns immediately with a fence.
- `caffeine_session_render_poll()` returns `PENDING` until the fence is
  signalled, then maps the transfer buffer and copies the pixels.
- `doppio-engine::Engine::{render, render_begin, render_poll}` wrap the ABI.
- `uikit/components/viewport_surface.rs` owns the interactive surface: image or
  empty state, HUD chips (resolution, latency, FPS, camera), mouse handling
  (left drag orbit, right/middle drag pan, wheel zoom, double-click focus) and
  grab/grabbing cursor feedback.
- The app pumps `ViewportTick` every 16 ms, so rendering, readback and UI stay
  decoupled; the HUD shows measured latency and a rolling FPS.
- Camera maths (orbit, pan basis, zoom, focus selection) lives in
  `windows/main.rs`; there is no camera button row. Gestures and shortcuts
  (`Ctrl+F` focus, `Ctrl+0` reset, `Ctrl+R` render) drive it.
- The render size is 960x540.

The viewport shows the real C++ scene (terrain, rocks, sky) in Iced.

### Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+N` | add entity |
| `Ctrl+S` | save scene |
| `Ctrl+Enter` | play / pause |
| `Ctrl+F` | focus selection |
| `Ctrl+0` | reset camera |
| `Ctrl+R` | render viewport now |
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
| query | `selection` | the selected entity or null |
| query | `scene` | scene path, dirty flag, counts and the console log |
| query | `assets` | project assets with path, kind, size and directory flag |
| query | `camera` | viewport yaw, pitch, distance and focus |
| query | `layout` | pane list and kinds |
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
It loads the `editor` fixture, renames through the real text input, adds an
entity, drives the camera and asserts the console:

```bash
cd doppio
iced-mcp run-script .iced-mcp/scripts/editor-smoke.luau \
  --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf"
```

`scripts/doppio-agent.sh` builds the C ABI and launches the same app with
`ICED_MCP=1` for an interactive agent session.

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
