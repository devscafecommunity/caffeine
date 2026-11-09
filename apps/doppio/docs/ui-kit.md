# Doppio UI kit — composable elements

Doppio UI is a **Lego kit**, not a pile of ad-hoc ImGui calls. Every visible piece
is an `Element`, and larger pieces are simply elements that own smaller ones:

```
Panel  (a docked window)
  └─ Card  (a raised surface)
       ├─ Toolbar
       │    ├─ Button
       │    └─ IconButton
       ├─ Row
       │    ├─ Card
       │    │    ├─ Label
       │    │    ├─ SliderFloat
       │    │    └─ Checkbox
       │    └─ Card
       │         └─ Canvas
       ├─ PropertyGrid
       │    ├─ PropertyRow → TextInput
       │    └─ PropertyRow → ColorPicker
       └─ NodeCanvasView
            └─ NodeElement
                 ├─ InputPin
                 ├─ SliderFloat
                 └─ OutputPin
```

The model lives in the public header umbrella:

```cpp
#include "caffeine/ui/UI.hpp"
using namespace Caffeine::UIKit;
using namespace Caffeine::UIKit::Elements;
```

It is header-only on purpose: a Doppio plugin can compose the same elements
without linking the IDE. The existing free functions in `Widgets.hpp` remain as
the low-level vocabulary and for legacy panels; new code should use elements.

## Why a retained tree over immediate mode?

ImGui is immediate-mode. The element tree is **retained for structure** but draws
immediately every frame:

- Stable ids and ownership make panels easy to debug (`UIContext::debugDump()`).
- Composition replaces copy-pasted `ImGui::BeginChild` / `SameLine` code.
- A plugin gets the same theme, spacing, ids and interaction semantics as Doppio.
- The host can inspect the tree, highlight bounds and migrate panels gradually.

The kit does **not** replace ImGui: it standardizes how Doppio and plugins use it.

## Element hierarchy

| Layer | Examples | Owns children? |
|-------|----------|----------------|
| **Objects** | `Button`, `Label`, `SliderFloat`, `InputPin`, `Canvas` | no |
| **Cards** | `Card`, `Row`, `Column`, `Grid`, `Section`, `Toolbar`, `ScrollArea` | yes |
| **Panels** | `Panel`, `TabPage`, `NodeCanvasView`, `PropertyGrid` | yes |
| **App shells** | `PanelRegistry`, `Workspace`, `PluginManager` | host |

The rule is simple: **a leaf draws one thing; a container decides layout**. If a
leaf needs a different arrangement, it is usually a container in disguise.

## Core pieces

| Element | Purpose |
|---------|---------|
| `Element` | base object: id, visibility, enabled, size, margin, padding, tooltip, state |
| `UIContext` | draw traversal, id stack, debug snapshot, node-canvas bridge |
| `Container` / `Box` | generic vertical or horizontal container |
| `Column` / `Row` | vertical stack / horizontal row with optional grow columns |
| `Card` | raised child surface with rounded border and padding |
| `Panel` | docked window with title, subtitle and icon |
| `Toolbar` | horizontal strip with the toolbar height |
| `Section` | collapsible section with indented children |
| `ScrollArea` | vertical scrollable region |
| `Grid` | table-backed grid |
| `TreeView` / `TreeItem` | scrollable tree with selection, drag payloads, drop targets and context menus |
| `Dialog` | modal popup with a body element tree and close lifecycle |
| `TabBar` / `TabPage` | tabbed pages |
| `Label` / `Text` | normal, muted, disabled, accent and semantic text |
| `Paragraph` | word-wrapped muted copy |
| `Button` | default, primary, danger, toolbar, segmented, ghost, icon-text |
| `IconButton` | icon-only action with tooltip |
| `Checkbox` | boolean toggle with change callback |
| `SliderFloat` / `SliderInt` | sliders with format/change callback |
| `DragFloat` / `DragInt` | drag editors |
| `Combo` | dropdown |
| `TextInput` / `SearchField` | text and search fields |
| `MultilineText` | multiline text editor |
| `DropZone` | button-sized asset drop target |
| `ListItem` / `ListBox` | selectable rows and scrollable lists |
| `ColorPicker` | RGBA color editor |
| `SegmentedControl` | mutually exclusive button strip |
| `Badge` / `StatusDot` / `ProgressBar` | feedback |
| `Separator` / `Spacer` / `Image` | layout and media |
| `EmptyState` / `KeyValue` | empty views and label/value rows |
| `PropertyGrid` / `PropertyRow` | two-column property editor |
| `FloatProperty`, `IntProperty`, `BoolProperty`, `ChoiceProperty`, `ColorFieldProperty`, `TextFieldProperty` | typed property sugar |
| `Canvas` | custom 2D drawing surface (optional interactive input) |
| `NodeCanvasView` | embedded `NodeCanvas` with element children |
| `NodeElement` | node title/body; add `InputPin` / `OutputPin` and controls |
| `DebugPanel` | previous-frame element tree |
| `UIContext::drawDebugOverlay()` | hit-rectangle overlay |

## Composition examples

### A panel with a card and a row

```cpp
Panel panel("My Tool", "Short description", "effects");
auto& card = panel.emplace<Card>("General");
card.emplace<Label>("Options", TextTone::Accent).setHeading(true);

auto& row = card.emplace<Row>("Actions");
row.emplace<Button>("Apply", ButtonVariant::Primary)
    .setOnClick([&] { apply(); });
row.emplace<Button>("Cancel");

UIContext ctx;
ctx.draw(panel);
```

`Panel` begins/ends the ImGui window. The host calls `ctx.draw(panel)` once per
frame. `Card` auto-sizes to its content; `Row` gives grow columns equal space.

### A property grid

```cpp
auto& grid = card.emplace<PropertyGrid>("Settings");
grid.typedRow<FloatProperty>("Wind", wind, 0.0f, 60.0f, 0.1f, "%.1f");
grid.typedRow<BoolProperty>("Enabled", enabled);
grid.typedRow<ChoiceProperty>("Mode", mode, {"Calm", "Storm"});
grid.typedRow<ColorFieldProperty>("Tint", color);

// Or wrap a custom element:
grid.row("Custom", std::make_unique<MyElement>());
```

### A canvas

```cpp
auto& canvas = card.emplace<Canvas>("Preview");
canvas.setHeight(180.0f);
canvas.setOnDraw([](UIContext& ctx, ImDrawList* dl, const ImVec2& p, const ImVec2& s) {
    dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(20, 24, 32, 255));
    dl->AddLine(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(220, 90, 140, 255), 2.0f);
});
```

### A node canvas

```cpp
auto& nodes = card.emplace<NodeCanvasView>("Graph");
nodes.setHeight(240.0f);

auto& start = nodes.emplace<NodeElement>(1, "On Update", "event");
start.emplace<OutputPin>(1, "Out");

auto& set = nodes.emplace<NodeElement>(2, "Set Position", "transform");
set.emplace<InputPin>(2, "In");
set.emplace<InputPin>(3, "X");
set.emplace<SliderFloat>("", &value, 0.0f, 1.0f, "%.2f");
set.emplace<OutputPin>(4, "Out");
```

`NodeElement` owns its body. Links and graph validation stay in the host tool.

## Writing a plugin panel

1. Add `#include "caffeine/ui/UI.hpp"` to the plugin source.
2. The plugin host already provides the ImGui library; do not create an ImGui
   context.
3. The host creates the panel window for plugin panels. Compose a `Column` or
   `Card` and call `UIContext::draw(root)` from the panel callback.
4. For `NodeCanvasView`, add the engine's `imnodes` include directory. The
   `doppio_add_plugin()` CMake helper now sets `IMNODES_DIR` automatically.
5. Keep the root tree as a member if it owns state; rebuild it only when the
   structure changes. Leaf values are read every frame.

Minimal plugin panel body:

```cpp
void render(void* userData) {
    auto* self = static_cast<MyPlugin*>(userData);
    if (!self->root) {
        self->root = std::make_unique<Card>("My Plugin");
        self->root->emplace<Label>("Hello from a composable plugin");
        self->root->emplace<Button>("Apply").setOnClick([self] { self->apply(); });
    }
    UIContext ctx;
    ctx.draw(*self->root);
}
```

The legacy `PanelHeader` / `PrimaryButton` helpers still work; migrate a piece at
a time.

## Debugging

- `UIContext::debugEntries()` contains every element drawn this frame: path, type,
  id, depth, bounds and interaction state.
- `UIContext::debugDump()` returns a readable indentated tree.
- `DebugPanel` shows the previous frame in the UI itself.
- `UIContext::drawDebugOverlay(ImGui::GetForegroundDrawList())` draws hit
  rectangles and type names on top of the editor.
- **View → Panels → UI Gallery** (or the `UI Gallery` command) is a living
  catalogue of every element. Enable *Draw hit rectangles* in its Debug card.

## Id rules

- Ids are scoped by their parent element; two `Button`s can share a label in
  different cards.
- If an element has no id, `Container::add()` assigns `<Type>#<index>`. This is
  stable for static trees; give dynamic lists explicit ids.
- `Element::find("id")` searches the subtree.
- Do not reuse an id among siblings unless you want ImGui state to be shared.

## Adding a new element

1. Derive from `Element` (or `Container` if it owns children).
2. Return a unique `type()` string; it powers debug dumps.
3. Implement `draw(UIContext&)`. Use theme tokens, not hard-coded colors.
4. Set `setHovered` / `setPressed` / `setChanged` / `setFocused` when relevant.
5. Add it to the umbrella `caffeine/ui/UI.hpp` if it is public.
6. Add a construction/describe test and, if it renders, add it to the UI Gallery.

Keep layout in containers. A leaf should not call `SameLine` just to look right;
wrap it in a `Row` instead.

## Migration map

| Old helper | Element |
|------------|---------|
| `BeginPanel` / `EndPanel` | `Panel` |
| `PanelHeader` | `Panel` subtitle/icon |
| `BeginCard` / `EndCard` | `Card` |
| `BeginToolbar` / `EndToolbar` | `Toolbar` |
| `PrimaryButton` | `Button(..., ButtonVariant::Primary)` |
| `DangerButton` | `Button(..., ButtonVariant::Danger)` |
| `ToolbarButton` | `Button(..., ButtonVariant::Toolbar)` |
| `SegmentedButton` | `Button(..., ButtonVariant::Segmented)` or `SegmentedControl` |
| `Toggle` | `Checkbox` |
| `SearchField` | `SearchField` |
| `BeginPropertyGrid` / `...Property` | `PropertyGrid` + typed rows |
| `EmptyState` | `EmptyState` |
| `Badge` / `StatusDot` | `Badge` / `StatusDot` |
| custom `ImDrawList` block | `Canvas` |

The free functions remain the implementation detail behind the elements, so a
partial migration is safe.
