# UI kit migration map

This is the working map for moving Doppio and its plugins from ad-hoc ImGui /
free-function widgets to the composable element kit (`caffeine/ui/UI.hpp`,
namespace `Caffeine::UIKit::Elements`).

Status legend:

| Status | Meaning |
|--------|---------|
| ✅ done | Panel renders through `Elements` end to end. |
| 🟡 partial | Some sections use elements; custom areas still use ImGui. |
| ⏳ pending | Not migrated yet. |
| 🧱 blocked | Needs a missing element (listed in the backlog). |

One panel is migrated at a time. Every migration must build `doppio`,
`CaffeineTest` and `DoppioTest`, and pass `[ui]` plus
`[editor][ui][elements]`. Custom drawing is allowed through `Elements::Canvas`;
new `ImGui::` calls are not allowed in migrated panel chrome.

## Tier 1 — forms, lists and simple panels

These already have (or need only small) elements. Best place to keep momentum.

| Panel | File | Size | Status | Notes / next element |
|-------|------|------|--------|----------------------|
| **Particles** | `ParticleEditorPanel.cpp` | 147 | ✅ done | `Panel`, `Card`, `ListBox`/`ListItem`, `PropertyGrid`, `BufferProperty` |
| **Audio Preview** | `AudioPreviewPanel.cpp` | 254 | ✅ done | transport `Row`/`Button`, waveform `Canvas`, settings `PropertyGrid` |
| **Terrain** | `TerrainPanel.cpp` | 259 | ✅ done | `Panel`, `TabBar`/`TabPage`, `Section`, `SegmentedControl`, `PropertyGrid`, tooltips |
| **Script Editor** | `ScriptEditorWindow.cpp` | 167 | ✅ done | `Panel`, `DropZone`, toolbar `Row`, `MultilineText` |
| **Entity Presets** | `EntityPresetsPanel.cpp` | 212 | ✅ done | three-column `Row` of `Card`s, `ListBox`, wizard `PropertyGrid` |
| **Effect Editor** | `EffectEditorPanel.cpp` | 270 | ✅ done | create `Card`, effect list, kind-specific `PropertyGrid` |
| **Image Manager** | `ImageManagerPanel.cpp` | 250 | ✅ done | library `ListBox`, toolbar `Row`, interactive zoom/pan `Canvas` |
| **Gameplay Preview** | `GameplayPreviewPanel.cpp` | 338 | ✅ done | `Panel` with hidden header; viewport renderer via `CustomElement` |
| **Camera Preview** | `CameraPreviewPanel.cpp` | 654 | ✅ done | `Panel` with hidden header; 2D/3D/fallback viewport via `CustomElement` |
| **Material Editor** | `MaterialEditorPanel.cpp` | 593 | ✅ done | `Panel`/menu, two-column `ScrollArea`, `Section`/`PropertyGrid`, `PathProperty`, `Canvas` GPU preview |
| **Settings** | `SettingsPanel.cpp` | 530 | ✅ done | `Panel`, nav `ListBox`, `Card`/`PropertyGrid`, layout profiles, shortcut `TreeView`, save/apply preferences |
| **Entity Debugger** | `EntityDebugger.cpp` | 402 | ✅ done | `Panel`, `Card`, `PropertyGrid`, `TreeView` components, hex dump labels, system queries |
| **HUD Editor** | `HudEditorPanel.cpp` | 377 | ✅ done | `Panel`, create `Row`s, widget `ListBox`, interactive `Canvas` with move/resize/selection |
| **Tilemap Editor** | `TilemapEditor.cpp` | 403 | ✅ done | toolbar, map `Canvas`, layer `ListBox`, tile palette `Grid`/`ImageButton` |
| **Texture / material pickers** | `FilePicker.cpp` | 571 | ✅ done | `Dialog`, path bar, folder `ListBox`, file `ListBox`, search and Save/PickFolder actions |
| **Build dialog** | `BuildDialog.cpp` | 267 | ✅ done | `Panel`, `Card`/`PropertyGrid`, `ProgressBar`, scrolling log, build actions |
| **Project startup** | `ProjectStartupDialog.cpp` | 506 | ✅ done | `Panel`/`TabBar`, `Card`/`PropertyGrid`, template selector, recent/browse `ListBox`, inline errors, toast overlay |

## Tier 2 — trees, drag-and-drop and large lists

| Panel | File | Size | Status | Notes / next element |
|-------|------|------|--------|----------------------|
| **Hierarchy** | `HierarchyPanel.cpp` | 838 | ✅ done | `Panel`, `SearchField`, `TreeView`/`TreeItem`, selection, entity drag/drop, context menus, rename and prefab dialogs |
| **Asset Browser** | `AssetBrowser.cpp` | 1891 | ✅ done | `Panel`/`Section`/`Row`, `AssetCard` grid, `DataTable` list, `Dialog` modals, `CustomElement` preview renderer |
| **Dryad Nodes** | `DryadNodesPanel.cpp` | 100 | ✅ done | `Panel`, `SearchField`, `TreeView`/`TreeItem`, `DRYAD_NODE` drag payload |
| **Plugin Manager** | `PluginSystem.cpp` | 1046 | ✅ done | `Panel`, plugin `Card`s, status labels, Enable/Disable/Reload actions |
| **Command Palette** | `CommandPalette.cpp` | 266 | ✅ done | `Dialog`, `SearchField`, `ListBox`/`ListItem`, keyboard navigation |
| **Console** | `ConsoleWindow.hpp` | 110 | ✅ done | `Panel`, `Row` toolbar, `Combo` level filter, `ScrollArea` log, `TextInput` submit |
| **Profiler** | `ProfilerWindow.hpp` | 315 | ✅ done | `Panel`, toolbar `Row`s, frame-time `Canvas`, hardware `KeyValue`s, expandable `DataTable` scope tree |
| **Stats overlay** | `StatsOverlay.hpp` | 69 | ✅ done | `Panel` overlay with `Label`/`KeyValue` rows and color-coded FPS |

## Tier 3 — custom canvases and graphs

These are mostly a drawing surface plus side panels. `Elements::Canvas` unlocks
the drawing; graph semantics stay in the host.

| Panel | File | Size | Status | Notes / next element |
|-------|------|------|--------|----------------------|
| **Scene Viewport** | `SceneViewport.cpp` | 4515 | 🧱 blocked | viewport texture/`Canvas` element, toolbar, overlay host |
| **Animation Timeline** | `AnimationTimeline.cpp` | 2217 | 🧱 blocked | `TreeView` tracks available; still needs `Canvas` timeline + `CurveEditor` |
| **Animator Controller** | `AnimatorController.cpp` | 1832 | 🧱 blocked | `Canvas` graph + node/transition elements |
| **Dryad Node Script** | `DryadNodeScriptPanel.cpp` | 1351 | ✅ done | `Panel`, toolbar `Row`s, status/hint labels; ImNodes graph via `CustomElement` |
| **Shader Graph** | `ShaderGraph.cpp` | 333 | 🧱 blocked | `Canvas` graph, node elements |
| **Curve widget** | `CurveWidget.hpp` | 161 | ✅ done | `CurveEditor` element, mode `Combo` and preset `Row` |
| **Transform gizmo** | `TransformGizmo.cpp` | 639 | n/a | drawing/input, not panel chrome |

## Tier 4 — host shell and shared widgets

| Item | File | Status | Notes |
|------|------|--------|-------|
| **UI Gallery** | `UiGalleryPanel.cpp` | ✅ done | Living catalogue and visual test |
| **Scene Editor shell** | `SceneEditor.cpp` | 🟡 partial | menu bar/dockspace/status bar stay host-specific; panels use elements |
| **Toolbox** | `ToolboxPanel.hpp` | 183 | ✅ done | `Panel`, `Section`, segmented tool `Row`s, viewport `PropertyGrid`, terrain brush controls |
| **Inspector widgets** | `InspectorWidgets.hpp` | 🟡 partial | `PropertyGrid`, `PathProperty` and `AssetCard` available; component drawers still use the legacy helpers |
| **Editor panel utils** | `EditorPanelUtils.hpp` | n/a | helpers, not UI |
| **Shader node** | `ShaderNode.cpp` | 🧱 blocked | node element/`Canvas` |
| **Editor spinner** | `EditorSpinner.cpp` | n/a | custom draw; can become `Spinner` element |

## Plugins

| Plugin | File | Status | Notes |
|--------|------|--------|-------|
| **Natural Fluids** | `plugins/natural-fluids/src/main.cpp` | ✅ done | `Card`/`PropertyGrid`/`Canvas`; live previews in `Canvas` |
| **Hello** | `plugins/hello-plugin/src/main.cpp` | ✅ done | `Column`/`Card`/`Label`/`Badge`/action `Row` |
| **Terrain Generator** | `plugins/terrain-generator/src/main.cpp` | ✅ done | `Card`/`PropertyGrid`, busy label, import `Section` |
| **Procedural Tools** | `plugins/procedural-tools/src/main.cpp` | ✅ done | streaming/generator `Section`s, benchmark action `Row`, API reference `Label`s |
| **Post Processing** | `plugins/post-processing/src/main.cpp` | ✅ done | `Column`/`Card`/`Section` chrome; core effect stack via `CustomElement` |
| **Git** | `plugins/git/src/main.cpp` | ✅ done | per-file `Row`s, commit `MultilineText`, branch combo, history `ListBox` |
| **AI Development** | `plugins/ai-development/src/main.cpp` | ✅ done | nav volume/agent `PropertyGrid`s, `Vec3Property`, script action `Row` |
| **Volumetric Clouds** | `plugins/volumetric-clouds/src/main.cpp` | ✅ done | weather/layer/mist `PropertyGrid`s, panel and component drawer |
| **Hello template** | `templates/hello-plugin/src/main.cpp` | ✅ done | SDK example with `Column`/`Card`/`Label`/`Button` |

## Missing elements backlog

Ordered by how many panels they unlock.

| Element | Unlocks | Notes |
|---------|---------|-------|
| `DragSource` / `DropTarget` mixins | generic Cards/custom elements | `TreeItem`, `ListItem`, `AssetCard` and `DataRow` already support payloads and drops; a mixin for arbitrary elements is still missing |
| `ContextMenu` / `Popup` element | Asset Browser, custom cards | `TreeItem` has a context-menu callback; a standalone popup element is still missing |
| `GraphCanvas` | Animation Timeline, Animator Controller, Shader Graph | axes, zoom/pan, nodes/transitions; use `Canvas` underneath |
| `MediaView` / `ViewportTexture` | Scene Viewport | texture display + input forwarding |
| `Spinner` | Editor Spinner, loading states | custom draw element |
| `StatusBar` | Scene Editor shell | host-specific but reusable |
| `Toast` / `Notification` | optional | transient feedback |

## Completed in this pass

| Panel | What changed |
|-------|--------------|
| **Particles** | `Panel`/`Card`/`ListBox`+`ListItem`/`PropertyGrid`/`BufferProperty` |
| **Audio Preview** | transport `Row`, waveform `Canvas`, settings `PropertyGrid` |
| **Terrain** | `Panel`, `TabBar`/`TabPage`, `Section`, `SegmentedControl`, `PropertyGrid`, tooltips |
| **Script Editor** | `Panel`, `DropZone`, toolbar `Row`, `MultilineText` |
| **Entity Presets** | three-column `Row` of `Card`s, `ListBox`, wizard `PropertyGrid` |
| **Effect Editor** | create `Card`, effect list, kind-specific `PropertyGrid` |
| **Image Manager** | library `ListBox`, toolbar `Row`, interactive zoom/pan `Canvas` |
| **Dryad Nodes** | `Panel`, `SearchField`, `TreeView`/`TreeItem`, drag payload |
| **Command Palette** | `Dialog`, `SearchField`, `ListBox`, keyboard navigation |
| **Hierarchy** | `Panel`, `SearchField`, `Toolbar`, `TreeView`, entity drag/drop, context menus, rename/prefab dialogs |
| **Settings** | nav `ListBox`, `Card`/`PropertyGrid`, layout profiles, shortcut `TreeView` |
| **Entity Debugger** | `Panel`, `Card`, `PropertyGrid`, component `TreeView`, hex dump, system queries |
| **HUD Editor** | create `Row`s, widget `ListBox`, interactive `Canvas` |
| **Tilemap Editor** | toolbar, map `Canvas`, layer `ListBox`, palette `Grid`/`ImageButton` |
| **Build Dialog** | `Card`/`PropertyGrid`, `ProgressBar`, scrolling log |
| **Plugin Manager** | `Panel`, plugin `Card`s, status labels, Enable/Disable/Reload |
| **File Picker** | `Dialog`, path bar, folder/file `ListBox`, search, Save/PickFolder actions |
| **Project Startup** | `Panel`/`TabBar`/`TabPage`, `Card`/`PropertyGrid`, recent/browse `ListBox`, toast overlay |
| **Material Editor** | menu `Panel`, two-column `ScrollArea`, `Section`/`PropertyGrid`, `PathProperty`, `Canvas` preview |
| **Console** | `Panel`, `Row` toolbar, `Combo` filter, `ScrollArea` log, submit-capable input |
| **Stats overlay** | auto-resizing `Panel`, `Label`/`KeyValue` rows |
| **Profiler** | toolbar `Row`s, frame-time `Canvas`, hardware `KeyValue`s, sortable/expandable `DataTable` |
| **Gameplay Preview** | `Panel` with hidden header, viewport `CustomElement` |
| **Camera Preview** | `Panel` with hidden header, viewport `CustomElement` |
| **Curve widget** | `CurveEditor`, mode `Combo`, preset `Row` |
| **Dryad Node Script** | toolbar `Row`s, status labels, `CustomElement` graph host |
| **Toolbox** | `Section`s, segmented tool rows, viewport/terrain `PropertyGrid` |
| **UI Gallery** | living catalogue, debug tree and hit-rect overlay |
| **Natural Fluids plugin** | full panel migrated; previews via `Canvas` |

New kit pieces added for these migrations:

- `TreeView` / `TreeItem` (selection, drag payload, drop target, context menu, force-open, scroll, text color, open-on-arrow)
- `Dialog` (modal body tree + close lifecycle)
- `ImageButton` for tile/asset palettes
- `AssetCard` thumbnail/icon tiles with selection, drag, drop and context menu
- `DataTable` / `DataRow` (columns, header sort callback, selection, indent/expand arrows, drag/drop)
- `CurveEditor` (Bezier handles, sampler callback, change callback)
- `PathField` / `PathProperty` (Browse, Clear, ASSET_PATH drop)
- `Vec2Property` / `Vec3Property` (`DragVec2` / `DragVec3`)
- `Color3FieldProperty` plus `ColorPicker` change callbacks
- `ScrollArea::setReserveBottom` for footer input/action rows
- `TextInput::setOnSubmit` for Enter-to-submit fields
- `ListItem` / `ListBox`
- `BufferProperty` and typed-row `setOnChange`
- `MultilineText`
- `DropZone`
- `Panel::setOnBegin` / `Panel::setInitialSize` / `Panel::setInitialPosition` (with pivot) / `setHeaderVisible`
- `Canvas::setInteractive` (invisible input item for zoom/pan)
- `PropertyRow::setHint`, `Label`/`ListItem` tooltips, `BoolProperty::setOnChange`

## Migration protocol

1. Pick the first 🧱 blocked panel whose missing element is next in the backlog,
   or the next ⏳ panel whose elements already exist.
2. Add the missing element to `include/caffeine/ui/` (header-only), with a
   construction/describe test and a gallery entry.
3. Migrate one panel. Keep custom drawing inside `Elements::Canvas`.
4. Build `doppio`, `CaffeineTest`, `DoppioTest`; run `[ui]` and
   `[editor][ui][elements]`.
5. Open the panel and check theme, density, scrolling, IDs (debug overlay) and
   focus behaviour.
6. Update this table from ⏳/🧱 to ✅ and commit the panel + map update.

## Known migration traps

- `Caffeine::UI` is the **game UI** namespace. The kit is
  `Caffeine::UIKit::Elements`; never introduce a `Caffeine::UIKit::UI` alias.
- Doppio panels own their ImGui window (`Elements::Panel`); plugin panels receive
  a host window, so their root is a `Column`/`Card`, not a second `Panel`.
- Containers must pair `beginBody`/`endBody` even when `Begin*` returns false
  (child windows, tables, tab bars).
- Dynamic lists need explicit ids; `Container::add` auto-ids are stable only for
  static trees.
- `PropertyRow` owns its value element; keep the pointed-to state alive through
  `UIContext::draw()` (locals are fine when the tree is rebuilt each frame).
- A panel that rebuilds its tree each frame may use locals; a retained tree may
  store pointers to stable members only.
