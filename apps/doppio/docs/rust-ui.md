# Rust UI frontend (Iced) — investigation and PoC

Status: **phase 0/PoC**. The C++ editor still ships with ImGui. The Rust crate
under `apps/doppio-rs/` is isolated from the CMake build and exists to validate
the UI stack, the docking model and headless/agent testing before the engine
boundary is refactored.

## Why Iced

- MIT licensed, suitable for an engine/editor that other people may redistribute.
- `wgpu` renderer, `PaneGrid` docking, dynamic widget trees in plain Rust.
- Official `iced_test` headless simulator (`click`, `typewrite`, `find`,
  snapshots) for deterministic CI tests.
- `iced-mcp` (MIT, iced 0.14) exposes the running widget tree to agents over MCP:
  snapshots, clicks, typing, assertions, flows and `run-suite`.

## Current PoC

`apps/doppio-rs` contains:

- a four-pane `PaneGrid` layout (Hierarchy / Viewport / Inspector / Console);
- a fake entity list, inspector name field, play/pause state and console log;
- stable widget ids for the toolbar, inspector input and entity rows;
- three `iced_test` cases: add entity, toggle play, rename selected entity;
- an optional `agent` feature that routes `main` through `iced_mcp::run`.

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

### Phase 1 — C ABI / data-oriented boundary

Expose an opaque, versioned C ABI:

- handles: engine/session/world/entity;
- lifecycle: create, load project/scene, tick, suspend/resume, shutdown;
- snapshots in POD arrays: hierarchy, component schemas, field values, selection,
  logs, frame stats (not one FFI call per field);
- commands: create/destroy entity, set field, undo/redo, play/pause, asset ops;
- events: dirty state, selection, log lines, asset changes.

The existing `PluginHostApi` is a good function-table precedent. The existing
`DOPPIO_TEST_MODE` JSON loop (`TestRequestHandler`) is a good engine-side test
precedent.

### Phase 2 — viewport

The engine already renders the scene into an RHI `m_colorTarget`. The hard part
is presenting that texture in Iced's wgpu renderer. Options, in order:

1. **PoC:** CPU readback of the offscreen target + wgpu upload. Cross-platform,
   simple, higher latency/bandwidth.
2. **Separate SDL3 viewport window** positioned by the UI. Good for a prototype,
   weaker for docking/Wayland.
3. **Production:** D3D12 shared handle on Windows, Vulkan external memory on
   Linux. SDL_GPU hides the native device, so this needs an RHI backend that can
   expose/accept the native device or a move away from SDL_GPU for the viewport.

Do not block the UI work on GPU interop; the ABI and panels can be validated with
the readback path first.

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
