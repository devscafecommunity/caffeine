# AGENTS.md — Caffeine Engine + Doppio

Rules for humans and coding agents working in **this repository**. The C++ engine lives in
`src/caffeine/`; the **Doppio** editor is the Rust/Iced workspace in `doppio/`. User-facing
documentation lives in the **`caffeine-website`** submodule, not here.

## Repository layout

| Path | Purpose |
|------|---------|
| `src/caffeine/` | **Caffeine core**: runtime, render, RHI, ECS, assets, physics, scripting, C ABI |
| `src/caffeine/capi/`, `include/caffeine/capi/` | **C ABI** consumed by external frontends (the Rust Doppio) |
| `src/caffeine/project`, `src/caffeine/scene` | Core services used by runtime and frontend (project metadata, view context, scene I/O) |
| `doppio/` | **Rust/Iced Doppio workspace** (see below) |
| `doppio/crates/engine/` | The **only** crate that loads `libcaffeine-capi` (libloading + C types) |
| `doppio/crates/ui/` | Iced UI: `uikit/{components,panels,id.rs}` and `windows/` |
| `doppio/crates/ui/src/windows/` | `main.rs` (state + PaneGrid), `projectloader.rs`, `main/tests.rs`, `main/agent.rs` |
| `doppio/docs/`, `doppio/.iced-mcp/` | Live Doppio docs, caption source, MCP flows and visual baselines |
| `doppio/tests/ui-flows/` | Headless UI flows (JSON) run by `cargo test` and the `doppio-headless` CLI |
| `doppio-archived/` | Frozen C++ Doppio snapshot; reference only, not built |
| `apps/` | C++ executables (`caffeine-runtime`, `caffeine-combined`) |
| `include/` | Public C++ headers shipped to games/tools/plugins (incl. legacy `caffeine/ui`) |
| `tests/` | Catch2 suites for the C++ engine |
| `scripts/` | Build, audit, Rust UI, agent and docs tooling (see below) |
| `cmake/`, `assets/` | CMake modules; sample content (no `node_modules` in git) |
| `caf-pack/` | Asset packer CLI + `.caf` / `.cap` pipeline (submodule) |
| `doppio-plugins/` | Official/community Doppio plugins (submodule, legacy C++/ImGui SDK) |
| `caffeine-website/` | Docs site (Svelte/HTML), TeX/PDF, docs MCP |
| `caffeine-launcher/` | Desktop hub — launch apps, plugins, updates (Rust/Iced) |

Do not add a root `docs/`, `planning/`, or `.planning/` back to this repo.
Doppio architecture/plugin/roadmap notes live in `doppio/docs/` (see the documentation
policy below).

## Layering

```
Vulkan / D3D12 / Metal
        ↓
      SDL3 GPU
        ↓
  caffeine-core  (src/, include/caffeine/)       ← no ImGui, no panels, no IDE state
        ↓
  libcaffeine-capi  (C ABI, versioned)           ← stable boundary for any frontend
        ↓
  doppio/crates/engine  (libloading)             ← only crate allowed to touch FFI
        ↓
  doppio/crates/ui  (Iced)                       ← Window → Panel → Component
        ↓
  plugins  (doppio-plugins submodule)            ← C++/ImGui tools that call public core APIs
```

- **Core must not include frontend code.** If a core service needs a view/serialization
  context, keep it under `src/caffeine/scene`, `src/caffeine/project` or another core module.
- **The IDE must be replaceable.** Any frontend can consume the engine through
  `include/caffeine/capi/EngineAPI.h`. Inside Doppio, only `doppio/crates/engine` calls
  FFI; `doppio/crates/ui` works with plain Rust types and `Message`s.
- **Plugins extend Doppio but are optional**: everything they expose must remain possible
  through the core/IDE APIs without the plugin. A plugin must not be required to load or
  run a scene. Plugins still target the legacy C++/ImGui SDK, not the Rust UI.
- **`caffeine-runtime` is still an ImGui host** (and the legacy SDK headers in
  `include/caffeine/ui/` still use ImGui); that is frontend/plugin code, not core.

## Doppio UI architecture

- **One window, five panes.** `windows/main.rs` owns `App` and the `PaneGrid` layout
  (Hierarchy / Assets / Viewport / Inspector / Console). Panes host tabbed panels; the
  free title-bar area beside the tabs is the pane drag handle. Individual tabs can be dragged
  between panes or split into a new pane. Layout presets live in the View menu and menu
  dropdowns are floating overlays. Hierarchy composes the reusable `tree_view` component (icons,
  hover, scene root, search, drag-to-reparent and `EntityView.parent` expansion) rather
  than card rows. Its **+** opens a built-in prefab palette (mesh, lights, camera, physics,
  terrain/water, duplicate selection). Parent groups compose position, rotation and scale
  into their children (`Scene::propagateTransforms`); the gizmo projects the resolved world
  position. A **Material** tab beside the Inspector edits `.mat` PBR assets with a rendered
  sphere preview, texture slots and a map baker (normal/AO/roughness/metallic/ORM and Unity
  packings); clicking a texture in Assets starts a material from it. A **Generator** tab hosts
  a reusable node canvas (`uikit/components/node_canvas.rs`) over the clean-room atomic graph in
  `uikit/generators.rs`, baking base, normal and ORM maps into the open material.
  Console entries are typed (Errors/Log/Actions/Engine/UI) and filterable; the Inspector
  has a combined Transform card, collapsible component cards and sliders/colour pickers
  for bounded/colour fields. A **Texture Set** tab hosts a TextureCraft-style
  UDIM/layer/mask painter (`uikit/texture_set.rs`, `uikit/components/uv_canvas.rs`),
  with 3D painting through `caffeine_session_pick_uv`, mesh-map bakes and
  `.texset` save/load.
- **Window → Panel → Component.** Windows own state, subscriptions and `Message`s; panels
  are pure `view(...) -> Element` functions; components (`uikit/components/`) are reusable
  primitives (button, slider, text field, field editor, tree view, component card/picker,
  panel, toolbar, status chip, viewport surface/shader, rendering canvas, gizmo, asset card).
- **Panels compose; they do not draw widgets.** A panel must not call `text_input`,
  `checkbox`, `pick_list`, `slider` or `button` directly. Extend a component under
  `uikit/components/` and compose it. `scripts/check-rust-ui.sh` fails on raw widgets in
  `uikit/panels/`.
- **Visual tokens live in `uikit/theme.rs`.** Panels and components use its colours,
  borders and spacing; no hardcoded colours outside the theme (enforced by the check).
  Window chrome uses a menu bar (`toolbar` + `menu`) plus `status`; every pane body is
  wrapped by `panel`. Prefer direct manipulation (click, right-click, double-click, drag,
  wheel) and shortcuts over adding a button for every action.
- **Viewport interaction is a component.** `viewport_surface` owns mouse handling (drag,
  pan, wheel, double-click, cursor); camera maths and state stay in `windows/main.rs`.
  `gizmo` overlays move/rotate/scale handles and emits absolute transform edits through
  `Message`s, never engine calls. Keep the viewport non-blocking (async readback/shaders)
  and show latency/FPS in the HUD.
- **Ids are hierarchical and catalogued.** Every interactive widget gets a
  `uikit::id::UiId` (`group.subgroup.object`, e.g. `editor.toolbar.add_entity`,
  `inspector.entity.name`, `hierarchy.list.entity_row.3`) and an entry in
  `uikit::id::CATALOG` with kind + description. List rows use `entity_row(i)` /
  `asset_card(i)`. Add ids to new widgets; tests, docs and agents rely on them.
- **Messages are the only mutation path.** `update` handles every `Message`; the MCP
  bridge exposes commands as actions that return `Message`s, so agents cannot do more
  than the UI.
- **Viewport is non-blocking.** The engine renders offscreen and the app polls an async
  readback (`render_begin` / `render_poll`) every 16 ms. SDL_GPU exposes no native
  texture handles, so a shared wgpu texture is **not possible through the public API**;
  zero-copy needs a renderer-level change (wgpu backend / SDL patch / separate window).
  Do not pretend a readback change is a shared-texture fix.
- **Fixtures keep tests deterministic**: `App::empty()` and the two-entity editor fixture.

Architecture details and migration status: [`doppio/docs/rust-ui.md`](doppio/docs/rust-ui.md).

## Ecosystem submodules

All sibling repos live under [devscafecommunity](https://github.com/devscafecommunity) and are pinned by commit in this monorepo.

| Path | Repository | Role | Built from engine CMake? | Launcher `AppId` |
|------|------------|------|--------------------------|------------------|
| *(this repo)* | `caffeine` | C++ engine, **Doppio** editor (`doppio/`, Rust/Iced), `libcaffeine-capi`, runtime, tests | yes (`caffeine-runtime`, `caffeine-capi`, …); Rust UI via `cargo` | `doppio`, `runtime` |
| `caf-pack/` | [caf-pack](https://github.com/devscafecommunity/caf-pack) | Raw assets → `.caf`, pack `.cap`; shared `CafTypes` with engine | linked into `CaffeineTest`/tools when submodule present | — |
| `caffeine-website/` | [caffeine-website](https://github.com/devscafecommunity/caffeine-website) | Public docs, i18n site, PDFs, **caffeine-docs** MCP | no (Node/npm) | — |
| `caffeine-launcher/` | [caffeine-launcher](https://github.com/devscafecommunity/caffeine-launcher) | Hub UI; discovers `doppio/target/<profile>/doppio`, `~/.local/share/caffeine/apps/` | `cargo build` / `scripts/build-launcher.sh` | hosts all apps |
| `doppio-plugins/` | [doppio-plugins](https://github.com/devscafecommunity/doppio-plugins) | Official/community Doppio plugins (terrain, procedural, git, post-processing, AI, VC&M) | yes, by default (`CAFFEINE_BUILD_DOPPIO_PLUGINS=ON`) into `build/plugins`; loading is opt-in | — |

**Clone / refresh submodules:**

```bash
git clone --recurse-submodules git@github.com:devscafecommunity/caffeine.git
# or after a plain clone:
./scripts/submodules.sh
```

**Dev layout the launcher expects:** engine build dir `build/` with binaries such as
`caffeine-runtime`; the Rust Doppio binary is `doppio/target/<profile>/doppio` (built with
`cargo build`). Doppio plugins are installed under a `plugins/` directory (project, user
config, or `build/plugins/`) and enabled in the Plugin Manager. Override with
`CAFFEINE_ENGINE_ROOT`.

## Documentation policy

- **No general Markdown documentation** in the engine tree (no root `docs/**/*.md`, no
  planning RFCs here).
- **Exception — Doppio only**: `doppio/docs/*.md` document the IDE architecture, ids,
  agent bridge, tests and roadmap. They must not duplicate core/engine docs.
  `doppio-archived/` is a frozen C++ snapshot and holds no live documentation.
- **Material pipeline roadmap**: `doppio/docs/material-pipeline.md` is the living
  checklist for materials, textures, map baking, node generators, MaterialCraft
  interoperability and TextureCraft-style texture sets. Keep it current when one of
  those areas changes.
- **Gizmos and markers**: `doppio/docs/gizmos-and-markers.md` documents the viewport overlays
  (transform gizmo, scene markers for lights / cameras / colliders / empties). Keep it current when
  they change.
- **Nature Generator**: `doppio/docs/nature-generator-guide.md` documents the separate nature tool
  (rocks, trees, plants built with a node graph: Mesh/Skeleton/Points ports, quality modes from low poly to hi-fi, your own materials and textures, `.obj` export, placement with per-part materials).
  Keep it current when that tool changes.
- **Terrain Generator**: `doppio/docs/terrain-generator-guide.md` documents the terrain tool (node graph of
  field sources, modifiers, masks and erosion; presets; splat layers from slope, height and sea level; the C ABI
  v8 terrain and its limits). `reference/nerthus` (MIT) is study material only: nothing is copied from it.
  Keep the guide current when the tool or the engine terrain changes.
- **Water Generator**: `doppio/docs/water-generator-guide.md` documents the water tool (node graph of
  wave sources, modifiers and look; presets; the C ABI v7 water surface and `water_height`; limits and the
  shader roadmap). Keep it current when that tool or the engine water changes.
- **Texture Generator**: `doppio/docs/texture-generator-guide.md` documents the separate texture
  tool (`.texgraph`, Texture Output, VFX & Shapes ops, presets, export, flipbooks). Keep it current
  when that tool changes.
- **Headless testing**: `doppio/docs/headless-testing.md` documents the `doppio-headless`
  driver (actions, targets, flows, recipes). Its reference section is generated from
  `headless/reference.rs`; a test fails if they drift apart.
- **Generated UI reference**: `doppio/docs/ui-captions.json` is the source of English and
  Portuguese widget descriptions. `scripts/generate-doppio-ui-docs.sh` captures every
  catalogued widget and can publish the Markdown + images into
  `caffeine-website/content/{en,pt}/docs/editor/ui-reference.md`.
- **Specs, guides, plans and screenshots**: `caffeine-website/content/{en,pt}/docs/`;
  PDFs and books under `caffeine-website/tex/`. Publish with `--publish`, then commit the
  website submodule and bump its pointer in the engine.
- **Agents**: Use the **caffeine-docs MCP** from `caffeine-website` (`search_docs`,
  `get_doc`). Do not scrape removed paths from git history unless the user asks.
- **README.md** at the repo root is only a short entrypoint with build commands and a link
  to the site submodule.
- **Plugins** are documented in `doppio-plugins/docs/` and linked from the site; the
  engine tree contains no plugin sources.

## Implementation rules

1. **C++20**, CMake 3.20+, SDL3 for the engine; **Rust 2021** + Iced 0.14 for Doppio —
   match the existing style in the file you edit.
2. **Minimal diffs** — one concern per change; no drive-by refactors.
3. **Tests** — C++ behavior changes need a test under `tests/` (`ctest` from `build/`).
   Rust UI changes need a test in `doppio/crates/ui/src/windows/main/tests.rs` (or the
   engine crate) and, for user-visible behaviour, a flow in `doppio/tests/ui-flows/*.json`;
   run `./scripts/check-rust-ui.sh`.
   **To see or exercise the UI, use the headless driver, never a real window or a desktop
   screenshot** (`doppio/docs/headless-testing.md`): `./scripts/doppio-headless.sh` takes
   JSON scripts (click/drag/type/keys/screenshot/expect), works with any agent and does
   not open anything.
4. **UI ids and history** — new interactive widgets need a `UiId` constant (or dynamic
   helper) plus a `CATALOG` entry. Every editing command must record an undo step
   (`App::commit`) and expose any agent-facing command as a `Message` action. Hierarchy
   edits use parent links (`Scene::Parent`), never UI-only nesting.
5. **UI composition and theme** — panels compose reusable components; never build
   interactive widgets inline. Keep field editors, cards, pickers, panel chrome, menus and
   viewport interaction in `uikit/components/`; colours/spacing come from `uikit/theme.rs`.
   Prefer menus, shortcuts and mouse gestures to a button per action.
6. **C ABI changes** — update `include/caffeine/capi/EngineAPI.h`, the C++ implementation,
   `doppio/crates/engine`, and both test suites in the same change. Version
   `CAFFEINE_CAPI_VERSION` for breaking changes; keep structs POD.
7. **FFI boundaries** — no `libloading`/`unsafe` FFI in `doppio/crates/ui`; only the engine
   crate may load symbols. Do not put ImGui back into `caffeine-core`.
8. **No C++ Doppio revival** — `src/doppio/` and `apps/doppio/` are gone. Use
   `doppio-archived/` for behaviour reference only.
9. **Agents** — commands that an agent should drive must be exposed as an MCP action
   (`main/agent.rs`) returning a `Message`; queries must be read-only.
10. **Submodules** — after clone, run `./scripts/submodules.sh`; see **Ecosystem submodules**.
11. **Editor vs runtime** — keep play-mode-only code out of editor-only paths unless shared
    APIs are intentional.
12. **Scripting / native modules** — follow public headers in `include/caffeine/`; document
    API changes in the website repo, not here.
13. **Commits** — only when the user requests; never commit secrets or local build artifacts.

## Build and checks

```bash
./scripts/submodules.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

The Rust Doppio loads `libcaffeine-capi`, so build the C ABI first:

```bash
cmake --build build --target caffeine-capi -j
cd doppio
cargo run -p doppio-ui -- ../assets/benchmarks/dense_outdoor.caf
```

Checks and tooling:

```bash
./scripts/check-rust-ui.sh                 # fmt, tests, clippy, core/ImGui audit, headless build
./scripts/doppio-headless.sh widgets       # headless UI driver: list what is on screen
./scripts/doppio-headless.sh run doppio/tests/ui-flows --format text   # run all UI flows
./scripts/doppio-headless.sh screenshot --annotate --out /tmp/shots    # annotated PNG
./scripts/render-material-gallery.sh       # engine-rendered material gallery + depth comparisons (docs)
./scripts/capture-material-docs.sh         # headless UI screenshots for the materials guide
./scripts/doppio-agent.sh                  # ICED_MCP=1 + agent feature
./scripts/generate-doppio-ui-docs.sh       # UI reference images + Markdown
./scripts/generate-doppio-ui-docs.sh --publish
./scripts/check-doppio-visual.sh           # iced-mcp visual regression (skips without display)
```

Cross-platform helpers: `scripts/build.sh`, `scripts/build.bat`, `scripts/build.fish`,
`scripts/build-launcher.sh`.

**caffeine-website:** `cd caffeine-website && npm install && npm run dev`. Publish doc
changes there; bump the submodule pointer in the engine when pinning a new docs revision.
