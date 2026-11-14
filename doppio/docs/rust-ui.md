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
    src/uikit/components/   button, slider, rendering_canvas, gizmo, asset_preview
    src/uikit/panels/       hierarchy, sceneviewport, assetmanager, inspector, console
```

- App/UI state lives in `windows/main.rs`; panels and components are
  reusable library modules.
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
- camera get/set (`CaffeineCamera`), driven by the viewport toolbar;
- tick/frame-stat bookkeeping (systems will be wired incrementally);
- no ImGui and no editor headers in the ABI;
- `libcaffeine-capi.so` shared library (CMake target `caffeine-capi`);
- a `libloading` Rust bridge in `doppio/crates/engine/src/lib.rs`, with a
  round-trip integration test that skips when the library is absent;
- the Rust Inspector shows the real C++ position and can nudge it with `+X`;
- 11 Rust tests: 1 engine bridge round-trip + 10 UI tests (`iced_test`).

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

### Viewport — CPU readback done

- `RenderDevice::initOffscreen()` creates a headless SDL_GPU device.
- `caffeine_session_render()` renders with `GpuSceneRenderer::renderWithCamera`
  into an RGBA8 target and reads it back with `RenderDevice::readTexture`.
- `doppio-engine::Engine::render()` wraps the ABI.
- `uikit/components/rendering_canvas.rs` displays the frame with Iced's image
  widget; the app refreshes it at 2 FPS via an `iced::time` subscription.
- The camera (yaw, pitch, distance, focus) is stored in `CaffeineCamera` and
  controlled from the viewport toolbar (Orbit -/+, Zoom +/-, Reset).
- The current render size is 640x360.

The viewport shows the real C++ scene (terrain, rocks, sky) in Iced.

Next viewport step is replacing readback with a shared GPU texture:

1. D3D12 shared handle on Windows.
2. Vulkan external memory on Linux.
3. Fallback: separate SDL3 viewport window while interop is unfinished.

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
  `shows`, `has` and `wait_for`, plus focused tests and one end-to-end
  `editor_workflow` scenario (add → select → rename → move → delete).
- Widgets that agents/tests target keep stable ids (`add-entity`, `entity-2`,
  `entity-name`, `nudge-x`, `reset-camera`, …); add an id to anything new.
- Fixtures (`App::empty` and two-entity editor) keep tests deterministic.

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
do, and the `messages` tool shows exactly what each call published.

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
