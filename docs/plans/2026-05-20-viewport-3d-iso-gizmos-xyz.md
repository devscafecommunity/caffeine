# Viewport 3D/2D/Iso + Gizmos XYZ Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add 3D/2D/Iso view modes to the Scene Viewport with orbit/pan camera navigation, and upgrade all gizmos to show X/Y/Z axes (Z dimmed in 2D mode).

**Architecture:** Add `ViewMode` enum + camera state (`yaw`, `pitch`, `camPos`) to `EditorContext`. Centralise a `worldToScreen(Vec3) → ImVec2` function in `SceneViewport` that branches per `ViewMode`. `TransformGizmo` always renders the 3D gizmo (XYZ), dimming Z in `Mode2D`.

**Tech Stack:** C++20, ImGui (`ImDrawList`, mouse API), existing `Math::Vec3`, `Math::Mat4` (if available) or manual trig.

---

## Task 1: Add ViewMode + camera state to EditorContext

**Files:**
- Modify: `src/editor/EditorContext.hpp` (viewport state section ~line 111)

**Step 1:** Add `ViewMode` enum and camera fields after the existing viewport state:

```cpp
// ── Viewport camera ────────────────────────────────────────────────
enum class ViewMode : u8 { Mode2D, Mode3D, Isometric };

ViewMode viewMode   = ViewMode::Mode2D;
f32 camYaw          = 0.0f;   // radians, horizontal orbit (Mode3D)
f32 camPitch        = 0.3f;   // radians, vertical orbit (Mode3D / Iso)
Math::Vec3 camFocus = {0.0f, 0.0f, 0.0f};  // orbit target / iso origin
f32 camDistance     = 10.0f;  // orbit distance (Mode3D)
```

Keep existing `viewportPanX`, `viewportPanY`, `viewportZoom` — they remain the canonical pan/zoom for Mode2D.

**Step 2:** Build — `cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio`  
Expected: clean build (new fields, no breakage).

**Step 3:** Commit — `git commit -m "feat(editor): add ViewMode + camera orbit state to EditorContext"`

---

## Task 2: Centralise worldToScreen in SceneViewport

**Files:**
- Modify: `src/editor/SceneViewport.hpp` — add private helper declaration
- Modify: `src/editor/SceneViewport.cpp` — implement helper, replace inline formulas

**Context:** Every draw function (`drawSprites`, `drawEmptyEntities`, `drawPhysicsDebug`, `drawCameraFrustums`, `drawGizmo`) currently computes screen position inline as:
```cpp
f32 worldToScreen = ctx.viewportZoom * 50.0f;
ImVec2 screenPos(
    origin.x + viewportSize.x * 0.5f + (pos.x + ctx.viewportPanX / worldToScreen) * worldToScreen,
    origin.y + viewportSize.y * 0.5f + (-pos.y + ctx.viewportPanY / worldToScreen) * worldToScreen
);
```

**Step 1:** Add to `SceneViewport.hpp` private section:
```cpp
static ImVec2 projectToScreen(Math::Vec3 worldPos, ImVec2 origin, ImVec2 viewportSize,
                               const EditorContext& ctx);
```

**Step 2:** Implement `projectToScreen` in `SceneViewport.cpp`:

```cpp
ImVec2 SceneViewport::projectToScreen(Math::Vec3 p, ImVec2 origin, ImVec2 viewportSize,
                                       const EditorContext& ctx) {
    f32 cx = origin.x + viewportSize.x * 0.5f;
    f32 cy = origin.y + viewportSize.y * 0.5f;

    switch (ctx.viewMode) {
        case EditorContext::ViewMode::Mode2D: {
            f32 s = ctx.viewportZoom * 50.0f;
            return ImVec2(cx + (p.x + ctx.viewportPanX / s) * s,
                          cy + (-p.y + ctx.viewportPanY / s) * s);
        }
        case EditorContext::ViewMode::Mode3D: {
            // Orbit camera: position = focus + spherical(yaw, pitch, distance)
            f32 s = ctx.viewportZoom * 50.0f;
            f32 sinY = std::sin(ctx.camYaw),   cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch),  cosP = std::cos(ctx.camPitch);
            // Camera right, up, forward in world space
            // Translate world point relative to camera focus
            f32 rx = p.x - ctx.camFocus.x;
            f32 ry = p.y - ctx.camFocus.y;
            f32 rz = p.z - ctx.camFocus.z;
            // Rotate by -yaw around world Y
            f32 vx =  cosY * rx + sinY * rz;
            f32 vy =  ry;
            f32 vz = -sinY * rx + cosY * rz;
            // Rotate by -pitch around view X
            f32 vx2 = vx;
            f32 vy2 =  cosP * vy + sinP * vz;
            f32 vz2 = -sinP * vy + cosP * vz;
            // Perspective divide (focal length = distance)
            f32 dist = std::max(ctx.camDistance, 0.1f);
            f32 fovScale = s * dist / std::max(dist + vz2, 0.01f);
            return ImVec2(cx + vx2 * fovScale + ctx.viewportPanX,
                          cy - vy2 * fovScale + ctx.viewportPanY);
        }
        case EditorContext::ViewMode::Isometric: {
            // Standard iso: x goes right-down, y goes left-down, z goes up
            f32 s = ctx.viewportZoom * 50.0f;
            f32 iso_x = (p.x - p.y) * std::cos(0.5236f) * s; // cos(30°)
            f32 iso_y = (p.x + p.y) * std::sin(0.5236f) * s  // sin(30°)
                        - p.z * s * 0.866f;
            return ImVec2(cx + iso_x + ctx.viewportPanX,
                          cy - iso_y + ctx.viewportPanY);
        }
    }
    return ImVec2(cx, cy);
}
```

**Step 3:** Replace every inline worldToScreen calculation in the draw functions with `projectToScreen({pos.x, pos.y, pos.z}, origin, viewportSize, ctx)`. Files/locations:
- `drawSprites` ~line 231-234: replace `screenPos` computation
- `drawEmptyEntities` ~line 356-357
- `drawPhysicsDebug` ~lines 479-481
- `drawCameraFrustums` — find and replace
- `drawGizmo` ~line 387-390
- `TransformGizmo.cpp` ~line 31-32 and 41-43 — replace with `projectToScreen` call (requires passing `origin` and `viewportSize`)

**Step 4:** Build. Fix any compilation errors.

**Step 5:** Commit — `git commit -m "refactor(viewport): centralise worldToScreen into projectToScreen(Vec3)"`

---

## Task 3: Add 3D/2D/Iso buttons to SceneViewport top-right

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — in the `onImGuiRender` button rendering block (~line 160-187)

**Step 1:** After the existing Physics/Snap buttons block (after `ImGui::PopStyleVar()` at ~line 186), add view-mode buttons in the **top-right** of the viewport:

```cpp
// View mode buttons (top-right)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    f32 btnW = 32.0f;
    f32 margin = 8.0f;
    ImVec2 btnPos(origin.x + viewportSize.x - margin - btnW * 3.0f - 4.0f, origin.y + 8.0f);

    auto viewBtn = [&](const char* label, EditorContext::ViewMode mode) {
        bool active = ctx.viewMode == mode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.9f));
        else        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.75f));
        ImGui::SetCursorScreenPos(btnPos);
        if (ImGui::Button(label, ImVec2(btnW, 22.0f))) ctx.viewMode = mode;
        ImGui::PopStyleColor();
        btnPos.x += btnW + 2.0f;
    };

    viewBtn("2D",  EditorContext::ViewMode::Mode2D);
    viewBtn("3D",  EditorContext::ViewMode::Mode3D);
    viewBtn("Iso", EditorContext::ViewMode::Isometric);
    ImGui::PopStyleVar();
}
```

**Step 2:** Build + visual check that buttons appear.

**Step 3:** Commit — `git commit -m "feat(viewport): add 3D/2D/Iso view mode toggle buttons"`

---

## Task 4: Update input handling — middle mouse pan, right mouse orbit

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — input section (~line 201-215)

**Context:** Currently only middle-mouse pan and scroll zoom exist. Need to add right-mouse orbit for Mode3D/Iso.

**Step 1:** Replace the current input block (~lines 201-215) with:

```cpp
if (hovered) {
    // Middle mouse → pan (all modes)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        ctx.viewportPanX += delta.x;
        ctx.viewportPanY += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    // Right mouse → orbit (Mode3D) or horizontal pan (Mode2D/Iso)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
            ctx.camYaw   += delta.x * 0.005f;
            ctx.camPitch += delta.y * 0.005f;
            ctx.camPitch  = std::max(-1.5f, std::min(1.5f, ctx.camPitch)); // clamp ±85°
        } else if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
            ctx.camYaw += delta.x * 0.005f; // rotate iso azimuth
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    }

    // Scroll → zoom
    f32 scroll = ImGui::GetIO().MouseWheel;
    if (scroll != 0) {
        ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
        ctx.viewportZoom = std::max(0.1f, std::min(10.0f, ctx.viewportZoom));
    }
}
```

**Step 2:** Build.

**Step 3:** Commit — `git commit -m "feat(viewport): right-mouse orbit for 3D/Iso, middle-mouse pan"`

---

## Task 5: Upgrade gizmos — always XYZ, Z dimmed in Mode2D

**Files:**
- Modify: `src/editor/TransformGizmo.cpp` — `onImGuiRender` (~line 28-76)

**Context:** Currently `onImGuiRender` checks if entity has `ECS::Position3D` to decide whether to use 2D or 3D gizmo. Now all entities use `ECS::Transform` (unified). The rule is:
- **Always** use the 3D variants (`renderTranslate3D`, `renderRotate3D`, `renderScale3D`) — they show X+Y+Z
- In `Mode2D`: render Z axis with `COLOR_Z_AXIS_DIM` (alpha ~100 instead of 255)
- In `Mode3D`/`Isometric`: Z fully active

**Step 1:** Add dimmed Z color constant to `TransformGizmo.hpp`:
```cpp
#ifdef CF_HAS_IMGUI
static constexpr u32 COLOR_Z_AXIS_DIM = IM_COL32(50, 100, 255, 80);
#else
static constexpr u32 COLOR_Z_AXIS_DIM = 0;
#endif
```

**Step 2:** Add a `bool zDimmed` parameter to `renderTranslate3D`, `renderRotate3D`, `renderScale3D` — when true, replace `COLOR_Z_AXIS` with `COLOR_Z_AXIS_DIM` and skip Z hit testing.

Signature changes in `TransformGizmo.hpp`:
```cpp
void renderTranslate3D(const Vec2& screenPos, float handleLen, float rotation, bool zDimmed);
void renderRotate3D(const Vec2& screenPos, float handleLen, bool zDimmed);
void renderScale3D(const Vec2& screenPos, float handleLen, bool zDimmed);
```

**Step 3:** In each render function body, replace the Z color:
```cpp
u32 zColor = zDimmed
    ? ((m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS_DIM)
    : ((m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS);
```

**Step 4:** In `onImGuiRender`, replace the entire `is3D` branch logic:

```cpp
// Always use 3D gizmo (XYZ), but dim Z in Mode2D
bool zDimmed = (ctx.viewMode == EditorContext::ViewMode::Mode2D);

// Screen position from unified Transform
auto* transform = world.get<ECS::Transform>(entity);
if (!transform) return;

// Use projectToScreen for correct per-mode projection
ImVec2 vpOrigin  = ImGui::GetItemRectMin();
ImVec2 vpSz      = ImGui::GetContentRegionAvail();
Math::Vec3 wp    = transform->position;
ImVec2 sp2       = SceneViewport::projectToScreen(wp, vpOrigin, vpSz, ctx);
screenPos        = Vec2(sp2.x, sp2.y);
entityRotation   = transform->rotation.z;

switch (ctx.gizmoMode) {
    case EditorContext::GizmoMode::Translate:
        renderTranslate3D(screenPos, handleLen, entityRotation, zDimmed); break;
    case EditorContext::GizmoMode::Rotate:
        renderRotate3D(screenPos, handleLen, zDimmed); break;
    case EditorContext::GizmoMode::Scale:
        renderScale3D(screenPos, handleLen, zDimmed); break;
    default: break;
}
```

Note: `SceneViewport::projectToScreen` needs to be `static` (it already will be) and accessible from `TransformGizmo.cpp` — add `#include "editor/SceneViewport.hpp"` to `TransformGizmo.cpp`.

**Step 5:** In `intersectTest`, disable Z axis hit if `zDimmed`:
- Pass `bool zDimmed` parameter to `intersectTest`
- Skip Z line test when `zDimmed == true`

**Step 6:** In `applyTranslate`, add Z axis handling on `ECS::Transform`:
```cpp
if (axis == GizmoAxis::Z) {
    float delta = worldDeltaX * 0.5f;
    transform->position.z += delta;
    if (snapEnabled) transform->position.z = applySnap(transform->position.z, m_snapTranslate / pixelsPerUnit);
}
```

**Step 7:** Build.

**Step 8:** Commit — `git commit -m "feat(gizmo): XYZ gizmo always, Z dimmed in Mode2D"`

---

## Task 6: Update grid rendering for 3D/Iso modes

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — `drawGrid` (~line 496)

**Context:** The grid currently draws horizontal/vertical lines in 2D ortho space. In Mode3D and Iso, the grid should render in the XZ or XY plane using `projectToScreen`.

**Step 1:** In `drawGrid`, add a branch at the start:

```cpp
void SceneViewport::drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    if (!m_config.grid) return;

    if (ctx.viewMode != EditorContext::ViewMode::Mode2D) {
        drawGrid3D(drawList, origin, viewportSize, ctx);
        return;
    }
    // ... existing 2D grid code ...
}
```

**Step 2:** Implement `drawGrid3D` — draws a ground plane grid (XZ plane, y=0) in 3D/Iso mode:

```cpp
void SceneViewport::drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    int halfLines = 10;
    f32 spacing = 1.0f;
    ImU32 gridColor = IM_COL32(100, 100, 120, 60);
    ImU32 axisColorX = IM_COL32(200, 80, 80, 120);
    ImU32 axisColorZ = IM_COL32(80, 80, 200, 120);

    for (int i = -halfLines; i <= halfLines; ++i) {
        // Lines parallel to X axis (vary z, fixed x=i)
        ImVec2 a = projectToScreen({(f32)(i * spacing), 0, (f32)(-halfLines * spacing)}, origin, viewportSize, ctx);
        ImVec2 b = projectToScreen({(f32)(i * spacing), 0, (f32)( halfLines * spacing)}, origin, viewportSize, ctx);
        ImU32 col = (i == 0) ? axisColorX : gridColor;
        dl->AddLine(a, b, col, (i == 0) ? 1.5f : 0.5f);

        // Lines parallel to Z axis (vary x, fixed z=i)
        ImVec2 c = projectToScreen({(f32)(-halfLines * spacing), 0, (f32)(i * spacing)}, origin, viewportSize, ctx);
        ImVec2 d = projectToScreen({(f32)( halfLines * spacing), 0, (f32)(i * spacing)}, origin, viewportSize, ctx);
        ImU32 colZ = (i == 0) ? axisColorZ : gridColor;
        dl->AddLine(c, d, colZ, (i == 0) ? 1.5f : 0.5f);
    }
}
```

Add declaration to `SceneViewport.hpp`:
```cpp
void drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx);
```

**Step 3:** Build.

**Step 4:** Commit — `git commit -m "feat(viewport): 3D/Iso ground plane grid"`

---

## Task 7: Iso mode grid uses correct XY plane (2D isometric games)

**Note:** For isometric 2D-style games (Mario RPG, Stardew Valley-style), the "ground" is the XY plane (z=0), not the XZ plane. The grid in Task 6 uses XZ plane for 3D FPS-style. For Iso mode, the grid should be XY with z varying for height.

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — `drawGrid3D`

**Step 1:** Branch `drawGrid3D` by mode:
```cpp
void SceneViewport::drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    int halfLines = 10;
    f32 spacing = 1.0f;
    ImU32 gridColor = IM_COL32(100, 100, 120, 60);

    if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
        // XY plane grid for isometric 2D
        for (int i = -halfLines; i <= halfLines; ++i) {
            ImVec2 a = projectToScreen({(f32)(i * spacing), (f32)(-halfLines * spacing), 0}, origin, viewportSize, ctx);
            ImVec2 b = projectToScreen({(f32)(i * spacing), (f32)( halfLines * spacing), 0}, origin, viewportSize, ctx);
            dl->AddLine(a, b, gridColor, 0.5f);

            ImVec2 c = projectToScreen({(f32)(-halfLines * spacing), (f32)(i * spacing), 0}, origin, viewportSize, ctx);
            ImVec2 d = projectToScreen({(f32)( halfLines * spacing), (f32)(i * spacing), 0}, origin, viewportSize, ctx);
            dl->AddLine(c, d, gridColor, 0.5f);
        }
    } else {
        // XZ plane grid for 3D perspective
        // ... (existing code from Task 6) ...
    }
}
```

**Step 2:** Build + visual check in both modes.

**Step 3:** Commit — `git commit -m "feat(viewport): separate grid planes for 3D vs Iso mode"`

---

## Task 8: Final verification + push

**Step 1:** Full build: `cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio`

**Step 2:** Verify:
- [ ] 3D/2D/Iso buttons appear in top-right of Scene Viewport
- [ ] Clicking 2D/3D/Iso changes view mode
- [ ] Middle mouse pans in all modes
- [ ] Right mouse orbits in 3D mode (pitch/yaw change)
- [ ] Right mouse rotates azimuth in Iso mode
- [ ] Gizmos always show X (red), Y (green), Z (blue)
- [ ] In Mode2D: Z gizmo axis is visibly dimmed
- [ ] In Mode3D/Iso: Z gizmo axis is fully bright
- [ ] Grid in 3D mode renders ground plane
- [ ] Grid in Iso mode renders XY plane
- [ ] Build is clean (no warnings converted to errors)

**Step 3:** `git push`

---

## Notes & Constraints

- **No `as any`, no suppressed errors** — if something doesn't compile, fix the root cause
- **No comments unless strictly necessary** (complex math is the exception — coordinate transforms qualify)
- **`projectToScreen` is the single source of truth** — all draw functions must use it, no inline formulas
- **No refactor scope creep** — only touch files listed above
- **Math:** `std::sin`, `std::cos`, `std::max`, `std::min` — no new math dependencies
- **Build command:** `cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio`
