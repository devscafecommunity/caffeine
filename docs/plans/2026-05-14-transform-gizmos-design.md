# Transform Gizmos Design

**Date:** 2026-05-14
**Issue:** #90
**Namespace:** `Caffeine::Editor`
**Status:** Approved

## Overview

Transform Gizmos provide visual manipulation tools for entities in the Viewport. Users can directly modify position, rotation, and scale without manual numeric input, with support for axis-constrained manipulation and snapping.

## Architecture

```
SceneViewport
  ├── owns ──► TransformGizmo
  ├── reads ──► EditorContext (selectedEntity, gizmoMode, pan/zoom)
  └── uses ──► InputManager (key input)
```

### Core Types

```cpp
namespace Caffeine::Editor {

    enum class GizmoMode {
        None,
        Translate,  // W key
        Rotate,     // E key
        Scale       // R key
    };

    enum class GizmoAxis {
        None,
        X,          // Red axis
        Y,          // Green axis
        Z,          // Blue axis (3D only)
        Center      // All axes / free move
    };

    enum class GizmoSpace {
        Local,      // Rotate around object axes
        World       // Rotate around world axes
    };
}
```

## TransformGizmo Class

**Header:** `src/editor/TransformGizmo.hpp`
**Source:** `src/editor/TransformGizmo.cpp`

```cpp
namespace Caffeine::Editor {

class TransformGizmo {
public:
    TransformGizmo(InputManager& inputManager);
    ~TransformGizmo() = default;

    void OnImGuiRender(Scene& scene, Entity selectedEntity, EditorContext& ctx);

private:
    // Input
    void HandleInput(EditorContext& ctx);
    bool IsKeyPressed(Key key) const;

    // Rendering
    void RenderTranslate(const glm::vec3& position, const glm::vec2& screenPos);
    void RenderRotate(const glm::vec3& position, const glm::vec2& screenPos);
    void RenderScale(const glm::vec3& position, const glm::vec2& screenPos);

    // Screen-space conversion
    glm::vec2 WorldToScreen(const glm::vec3& worldPos, const EditorContext& ctx);
    glm::vec3 ScreenDeltaToWorld(const glm::vec2& screenDelta, const EditorContext& ctx);

    // Intersection
    bool IntersectAxis(GizmoAxis axis, const glm::vec2& mouseScreenPos, 
                      const glm::vec2& centerScreenPos, const EditorContext& ctx);

    // Transform application
    void ApplyTranslate(Entity entity, const glm::vec3& worldDelta, EditorContext& ctx);
    void ApplyRotate(Entity entity, const glm::vec3& worldDelta);
    void ApplyScale(Entity entity, const glm::vec3& worldDelta);

    // Snapping
    float ApplySnap(float value, float snapInterval, bool snapEnabled);

    // Members
    InputManager&      m_InputManager;
    GizmoMode          m_CurrentMode = GizmoMode::Translate;
    GizmoAxis          m_ActiveAxis = GizmoAxis::None;
    GizmoAxis          m_HoveredAxis = GizmoAxis::None;
    bool               m_IsDragging = false;
    glm::vec2          m_DragStartMouse = {0.f, 0.f};
    glm::vec3          m_TransformStart = {0.f, 0.f, 0.f};

    // Snap values (screen-space)
    float              m_SnapValueTranslate = 16.0f;  // pixels
    float              m_SnapValueRotate = 45.0f;      // degrees
    float              m_SnapValueScale = 0.5f;        // units

    // Visual constants
    static constexpr float AXIS_LINE_LENGTH = 60.0f;
    static constexpr float AXIS_LINE_WIDTH = 3.0f;
    static constexpr float HANDLE_SIZE = 8.0f;
    static constexpr float HOVER_THRESHOLD = 10.0f;
};
}
```

## 2D Gizmo Rendering

### Translate (W)
Two perpendicular lines with arrowheads at ends.
```
         Y (green)
          ↑
          │
          └──────► X (red)
```

### Rotate (E)
Two concentric circles/arcs around center.
```
      ╭───────╮
     ╱    ◯    ╲
    │          │
     ╲        ╱
      ╰───────╯
```

### Scale (R)
Two lines with squares at ends.
```
         ■ Y (green)
         │
         └────■ X (red)
```

### 3D Gizmo Rendering

### Translate
Three lines from center with arrowheads (X, Y, Z).

### Rotate
Three concentric circular rings in X/Y/Z planes.

### Scale
Three lines with cubes at ends.

### Colors
- X axis: Red `{1.0f, 0.2f, 0.2f}`
- Y axis: Green `{0.2f, 1.0f, 0.2f}`
- Z axis: Blue `{0.2f, 0.5f, 1.0f}`
- Hovered: Yellow `{1.0f, 1.0f, 0.0f}`
- Dragging: White highlight

## Intersection Testing

Screen-space math using point-to-line distance:

```cpp
bool TransformGizmo::IntersectAxis(GizmoAxis axis, const glm::vec2& mousePos,
                                    const glm::vec2& centerScreenPos,
                                    const EditorContext& ctx) {
    if (axis == GizmoAxis::Center) {
        float dist = glm::distance(mousePos, centerScreenPos);
        return dist < HOVER_THRESHOLD;
    }

    // Calculate axis endpoint in screen space
    glm::vec2 axisDir;
    switch (axis) {
        case GizmoAxis::X: axisDir = {AXIS_LINE_LENGTH, 0.f}; break;
        case GizmoAxis::Y: axisDir = {0.f, -AXIS_LINE_LENGTH}; break;
        case GizmoAxis::Z: axisDir = {AXIS_LINE_LENGTH * 0.7f, -AXIS_LINE_LENGTH * 0.7f}; break;
    }

    glm::vec2 axisEndScreen = centerScreenPos + axisDir;

    // Point-to-line distance
    float dist = PointToLineDistance(mousePos, centerScreenPos, axisEndScreen);
    return dist < HOVER_THRESHOLD;
}
```

## Snapping Logic

```cpp
float TransformGizmo::ApplySnap(float value, float snapInterval, bool snapEnabled) {
    if (!snapEnabled || snapInterval <= 0.0f) {
        return value;
    }
    return std::round(value / snapInterval) * snapInterval;
}

// Usage:
bool snapEnabled = IsKeyPressed(Key::LShift) || IsKeyPressed(Key::RShift);
if (snapEnabled) {
    float snapWorld = m_SnapValueTranslate / pixelsPerUnit;
    delta.x = ApplySnap(delta.x, snapWorld, m_ActiveAxis == GizmoAxis::X || m_ActiveAxis == GizmoAxis::Center);
    delta.y = ApplySnap(delta.y, snapWorld, m_ActiveAxis == GizmoAxis::Y || m_ActiveAxis == GizmoAxis::Center);
}
```

## File Structure

```
src/editor/
  ├── TransformGizmo.hpp    (new)
  └── TransformGizmo.cpp    (new)

Modified:
  ├── SceneViewport.hpp     (add TransformGizmo member)
  ├── SceneViewport.cpp     (integrate gizmo call)
  └── CMakeLists.txt        (add new source files)
```

## Integration

**SceneViewport.h:**
```cpp
#include "TransformGizmo.hpp"

class SceneViewport {
    // ... existing
private:
    Editor::TransformGizmo m_Gizmo;
};
```

**SceneViewport.cpp:**
```cpp
void SceneViewport::render(Render::Camera2D& editorCamera) {
    // ... existing code ...

    if (ctx.selectedEntity != entt::null) {
        Entity entity = { ctx.selectedEntity, m_Scene.get() };
        m_Gizmo.OnImGuiRender(*m_Scene, entity, ctx);
    }
}
```

## Acceptance Criteria

- [ ] Press W/E/R alternates between translate/rotate/scale modes
- [ ] Hovered axis highlights in yellow
- [ ] Dragging an axis constrains movement to that axis
- [ ] Dragging center allows free/uniform movement
- [ ] Shift key enables snapping (16px for translate, 45° for rotate)
- [ ] Entity transform updates in real-time during drag
- [ ] Works with both 2D (Position2D, Scale2D) and 3D (Position3D, Rotation3D, Scale3D) components

## Dependencies

- **Upstream:** `Caffeine::Core::Position3D/Rotation3D/Scale3D`, `Caffeine::Editor::Viewport`
- **Downstream:** `Caffeine::Editor::SceneHierarchy` (selection integration)