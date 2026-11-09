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
| **Gameplay Preview** | `GameplayPreviewPanel.cpp` | 336 | 🧱 blocked | needs `Canvas` texture view / viewport element |
| **Camera Preview** | `CameraPreviewPanel.cpp` | 652 | 🧱 blocked | same viewport/texture element + `PropertyGrid` |
| **Material Editor** | `MaterialEditorPanel.cpp` | 541 | 🟡 partial | `ValueEditor` can be a `PropertyGrid` + `Canvas`; preview texture |
| **Settings** | `SettingsPanel.cpp` | 530 | ⏳ pending | `TabBar`/`TabPage`, `PropertyGrid`, `ColorPicker`, `Dialog` for reset |
| **Entity Debugger** | `EntityDebugger.cpp` | 402 | ⏳ pending | `SearchField`, `ListBox`, `PropertyGrid`, `Canvas` for stats |
| **HUD Editor** | `HudEditorPanel.cpp` | 377 | ⏳ pending | `Canvas` + `PropertyGrid`, `ListBox` for widgets |
| **Tilemap Editor** | `TilemapEditor.cpp` | 403 | ⏳ pending | `Canvas` palette/preview, `Grid` of tile buttons |
| **Texture / material pickers** | `FilePicker.cpp` | 571 | ⏳ pending | `Dialog` now available; needs `ListBox`/`Grid` layout |
| **Build dialog** | `BuildDialog.cpp` | 267 | ⏳ pending | `Dialog` + `ProgressBar` + `ListBox` are available |
| **Project startup** | `ProjectStartupDialog.cpp` | 591 | ⏳ pending | `Dialog` available; needs template cards + recent-project list |

## Tier 2 — trees, drag-and-drop and large lists

| Panel | File | Size | Status | Notes / next element |
|-------|------|------|--------|----------------------|
| **Hierarchy** | `HierarchyPanel.cpp` | 838 | ✅ done | `Panel`, `SearchField`, `TreeView`/`TreeItem`, selection, entity drag/drop, context menus, rename and prefab dialogs |
| **Asset Browser** | `AssetBrowser.cpp` | 1869 | 🧱 blocked | folder tree available via `TreeView`; still needs `AssetCard`/`Thumbnail` and grid/table |
| **Dryad Nodes** | `DryadNodesPanel.cpp` | 100 | ✅ done | `Panel`, `SearchField`, `TreeView`/`TreeItem`, `DRYAD_NODE` drag payload |
| **Plugin Manager** | `PluginSystem.cpp` | 1046 | 🟡 partial | `Panel`, `ListBox`, `Card`; plugin row table |
| **Command Palette** | `CommandPalette.cpp` | 266 | ✅ done | `Dialog`, `SearchField`, `ListBox`/`ListItem`, keyboard navigation |
| **Console** | `ConsoleWindow.hpp` | 101 | ⏳ pending | `Panel`, `Toolbar`, `ListBox`, filters |
| **Profiler** | `ProfilerWindow.hpp` | 264 | 🟡 partial | `Panel`, `Canvas` graph, `ListBox` |
| **Stats overlay** | `StatsOverlay.hpp` | 51 | ⏳ pending | `Canvas` + `Label` |

## Tier 3 — custom canvases and graphs

These are mostly a drawing surface plus side panels. `Elements::Canvas` unlocks
the drawing; graph semantics stay in the host.

| Panel | File | Size | Status | Notes / next element |
|-------|------|------|--------|----------------------|
| **Scene Viewport** | `SceneViewport.cpp` | 4515 | 🧱 blocked | viewport texture/`Canvas` element, toolbar, overlay host |
| **Animation Timeline** | `AnimationTimeline.cpp` | 2217 | 🧱 blocked | `TreeView` tracks available; still needs `Canvas` timeline + `CurveEditor` |
| **Animator Controller** | `AnimatorController.cpp` | 1832 | 🧱 blocked | `Canvas` graph + node/transition elements |
| **Dryad Node Script** | `DryadNodeScriptPanel.cpp` | 1332 | 🟡 partial | already uses `NodeCanvas`; migrate chrome to `Panel`/`Toolbar`/`PropertyGrid` |
| **Shader Graph** | `ShaderGraph.cpp` | 333 | 🧱 blocked | `Canvas` graph, node elements |
| **Curve widget** | `CurveWidget.hpp` | 155 | 🧱 blocked | reusable `CurveEditor` element |
| **Transform gizmo** | `TransformGizmo.cpp` | 639 | n/a | drawing/input, not panel chrome |

## Tier 4 — host shell and shared widgets

| Item | File | Status | Notes |
|------|------|--------|-------|
| **UI Gallery** | `UiGalleryPanel.cpp` | ✅ done | Living catalogue and visual test |
| **Scene Editor shell** | `SceneEditor.cpp` | 🟡 partial | menu bar/dockspace/status bar stay host-specific; panels use elements |
| **Toolbox** | `ToolboxPanel.hpp` | ⏳ pending | `Panel`, `Section`, `SegmentedControl`, `Button` |
| **Inspector widgets** | `InspectorWidgets.hpp` | 🧱 blocked | `PropertyGrid` and `TreeView` available; still needs `AssetCard`-style drawers |
| **Editor panel utils** | `EditorPanelUtils.hpp` | n/a | helpers, not UI |
| **Shader node** | `ShaderNode.cpp` | 🧱 blocked | node element/`Canvas` |
| **Editor spinner** | `EditorSpinner.cpp` | n/a | custom draw; can become `Spinner` element |

## Plugins

| Plugin | File | Status | Notes |
|--------|------|--------|-------|
| **Natural Fluids** | `plugins/natural-fluids/src/main.cpp` | ✅ done | `Card`/`PropertyGrid`/`Canvas`; live previews in `Canvas` |
| **Hello** | `plugins/hello-plugin/src/main.cpp` | ⏳ pending | `PanelHeader`/`Paragraph`/`Badge` → `Panel`/`Card`/`Label` |
| **Terrain Generator** | `plugins/terrain-generator/src/main.cpp` | ⏳ pending | `Card`, `PropertyGrid`, `Button`, `ProgressBar` |
| **Procedural Tools** | `plugins/procedural-tools/src/main.cpp` | ⏳ pending | same, plus `Canvas` for map preview |
| **Post Processing** | `plugins/post-processing/src/main.cpp` | ⏳ pending | `PropertyGrid`, `Combo`, `Checkbox` |
| **Git** | `plugins/git/src/main.cpp` | ⏳ pending | `ListBox`/`ListItem`, `Toolbar`, `Card` |
| **AI Development** | `plugins/ai-development/src/main.cpp` | ⏳ pending | `PropertyGrid`, `ListBox`, `Button` |
| **Volumetric Clouds** | `plugins/volumetric-clouds/src/main.cpp` | ⏳ pending | `PropertyGrid`, `Combo`, `Checkbox`, `Canvas` preview |
| **Hello template** | `templates/hello-plugin/src/main.cpp` | ⏳ pending | small; use as SDK example of elements |

## Missing elements backlog

Ordered by how many panels they unlock.

| Element | Unlocks | Notes |
|---------|---------|-------|
| `DataTable` / `TableRow` | Settings, Entity Debugger, Plugin Manager, Asset Browser details | columns, header, sort, selection |
| `AssetCard` / `Thumbnail` | Asset Browser, Material/Audio preview | preview texture/`Canvas`, label, selection, drag source |
| `DragSource` / `DropTarget` mixins | Asset Browser, Tilemap, generic Cards | `TreeItem`/`ListItem` already support payloads and drops; a generic mixin for `Card`/custom elements is still missing |
| `ContextMenu` / `Popup` element | Asset Browser, custom cards | `TreeItem` has a context-menu callback; a standalone popup element is still missing |
| `CurveEditor` / `GraphCanvas` | Animation Timeline, Animator Controller, Shader Graph | axes, zoom/pan, points/keys; use `Canvas` underneath |
| `MediaView` / `ViewportTexture` | Scene Viewport, Gameplay/Camera Preview | texture display + input forwarding |
| `Vec3Property` / `Vec2Property` | Inspector, Particles, Effect Editor, Transform rows | compact vector row instead of three float rows |
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
| **UI Gallery** | living catalogue, debug tree and hit-rect overlay |
| **Natural Fluids plugin** | full panel migrated; previews via `Canvas` |

New kit pieces added for these migrations:

- `TreeView` / `TreeItem` (selection, drag payload, drop target, context menu, force-open, scroll, text color, open-on-arrow)
- `Dialog` (modal body tree + close lifecycle)
- `ListItem` / `ListBox`
- `BufferProperty` and typed-row `setOnChange`
- `MultilineText`
- `DropZone`
- `Panel::setOnBegin` / `Panel::setInitialSize`
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
