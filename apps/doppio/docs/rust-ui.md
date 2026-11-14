# Rust UI frontend (Iced) — investigation and PoC

Status: **phase 0 started + PoC**. `caffeine-core` is now built without
ImGui/ImNodes; the ImGui draw-list adapters live in `caffeine-editor-support`.
The C++ editor still ships with ImGui. The Rust crate under `apps/doppio-rs/`
is isolated from the CMake build and exists to validate the UI stack, the
docking model and headless/agent testing.

## Why Iced

- MIT licensed, suitable for an engine/editor that other people may redistribute.
- `wgpu` renderer, `PaneGrid` docking, dynamic widget trees in plain Rust.
- Official `iced_test` headless simulator (`click`, `typewrite`, `find`,
  snapshots) for deterministic CI tests.
- `iced-mcp` (MIT, iced 0.14) exposes the running widget tree to agents over MCP:
  snapshots, clicks, typing, assertions, flows and `run-suite`.

## Current PoC

The `doppio/` workspace contains:

```
doppio/
  crates/engine/        libloading bindings to libcaffeine-capi
  crates/ui/
    src/windows/        main.rs, projectloader.rs
    src/uikit/components/  button, slider, rendering_canvas, gizmo, asset_preview
    src/uikit/panels/      hierarchy, sceneviewport, assetmanager, inspector, console
```

- App/UI state lives in `windows/main.rs`; panels and components are
  reusable library modules.
- a five-pane `PaneGrid` layout (Hierarchy / Assets / Viewport / Inspector /
  Console);
- stable widget ids for the toolbar, inspector inputs and entity rows;
- six `iced_test` cases: add, play/pause, nudge X, nudge rotation, scale up and
  rename;
- an optional `agent` feature that routes `main` through `iced_mcp::run`.

The C++ boundary now has:

- `caffeine-core`: engine only, no ImGui/ImNodes.
- `caffeine-editor-support`: ImGui adapters for engine render helpers.
- `doppio-ui-kit`: the element kit and editor host helpers.

Run it with:

```bash
cd apps/doppio-rs
cargo run
```

Run the checks used by CI/agents:

```bash
./scripts/check-rust-ui.sh
# cargo fmt --check, cargo test, cargo check --features agent, clippy -D warnings
```

## Migration phases

### Phase 0 — separate engine from ImGui (prerequisite)

Today `caffeine-core` links ImGui/ImNodes publicly and contains the ImGui UI
kit, despite the CMake comment saying it is pure. Split the targets:

- `caffeine-engine`: ECS, scene, assets, RHI, physics, audio, scripting. No
  ImGui/ImNodes and no `include/caffeine/ui`.
- `doppio-ui-kit` / editor: ImGui panels, workspace, plugins.
- `caffeine-runtime`: no ImGui.

Add a CI check that rejects ImGui includes/symbols in the engine target. Keep
the ImGui editor building until the Rust frontend reaches feature parity.

Measure the current coupling with:

```bash
python3 scripts/audit_core_ui_coupling.py
# after the split:
python3 scripts/audit_core_ui_coupling.py --fail-on-leaks
```

Current baseline:
- `caffeine-core` builds without ImGui/ImNodes and `nm` reports 0 ImGui/ImNodes
  symbols in `libcaffeine-core.a`.
- `caffeine-editor-support` contains the engine ImGui adapters
  (`Overlay2D::draw`, `SkyboxRenderer`).
- 18 UI-kit headers and 1 editor-UI header still reference ImGui; that is the
  editor/UI side.
- `caffeine-runtime` is still an ImGui frontend and needs an RHI render path
  before it can be made UI-free.

### Phase 1 — C ABI / data-oriented boundary

First slice implemented and tested:

- header: `include/caffeine/capi/EngineAPI.h`
- implementation: `src/caffeine/capi/EngineAPI.cpp`
- test: `tests/test_capi.cpp` (`[capi]`, 53 assertions)
- opaque `CaffeineSession`, `CaffeineEntity` and versioned `CaffeineResult`;
- session create/destroy and `.caf` scene load;
- entity count/list/name, create, destroy, rename and selection;
- position get/set (setting creates a `Transform` when the entity has none);
- dirty state and `.caf` save; the Rust toolbar has Save + a dirty marker;
- tick/frame-stat bookkeeping (systems will be wired incrementally);
- no ImGui and no editor headers in the ABI;
- `libcaffeine-capi.so` shared library (CMake target `caffeine-capi`);
- a `libloading` Rust bridge in `doppio/crates/engine/src/lib.rs`, with a
  round-trip integration test that skips when the library is absent;
- the Rust Inspector shows the real C++ position and can nudge it with `+X`;
- 7 Rust tests: 1 engine bridge round-trip + 6 UI tests (`iced_test`).

The PoC now loads a real scene through the ABI:

```bash
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

The hierarchy then shows the real C++ entities (Rock/Tree/...). Without the
shared library it falls back to fake data, so UI tests do not depend on CMake.

Still to add before the Rust UI can be fully wired:

- component schemas and field snapshots in POD arrays;
- commands/undo/redo/play-pause and asset operations;
- events (dirty state, selection, logs, asset changes);
- Rust bindings crate and a shared-library build profile.

The existing `PluginHostApi` is a good function-table precedent. The existing
`DOPPIO_TEST_MODE` JSON loop (`TestRequestHandler`) is a good engine-side test
precedent.

### Phase 2 — viewport

The CPU-readback path is implemented and working:

- `RenderDevice::initOffscreen()` creates a headless SDL_GPU device.
- `caffeine_session_render()` renders with `GpuSceneRenderer::renderWithCamera`
  into an RGBA8 target and reads it back with `RenderDevice::readTexture`.
- `doppio-engine::Engine::render()` wraps the ABI.
- `uikit/components/rendering_canvas.rs` displays the frame with Iced's image
  widget; the app refreshes it at 2 FPS via an `iced::time` subscription.
- The current render size is 640x360.

The viewport now shows the real C++ scene (terrain, rocks, sky) in Iced.

Next viewport step is replacing readback with a shared GPU texture:

1. D3D12 shared handle on Windows.
2. Vulkan external memory on Linux.
3. Fallback: separate SDL3 viewport window while interop is unfinished.

### Phase 3 — plugins

Current plugins are C++ and draw ImGui. A Rust UI needs one of:

- a legacy ImGui host window for old plugins; or
- a plugin v2 contract (data/commands, optional process/WASM isolation).

This is the largest hidden cost and should be designed before removing ImGui.

### Phase 4 — tests and agents

- `iced_test` for component/layout logic and deterministic flows.
- `iced-mcp` behind an optional feature for agent-driven E2E; pin the version.
- Keep/extend the engine JSON protocol for scene/component/viewport assertions.
- Add fixtures for project templates, scenes and component schemas.

## Rules

- Keep the ImGui editor working until the Rust frontend covers the same panels.
- Add widget ids for anything an agent must find.
- Prefer snapshots/arrays over per-field FFI calls.
- Do not put the Rust frontend in the CMake build until the C ABI exists.
