# Scene Viewport Fix & Entity Type System Expansion

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix empty Scene Viewport by adding grid rendering and expand entity type system with Camera, Light, and Mesh components for professional game editor.

**Architecture:** 
1. Add grid drawing to SceneViewport using ImGui drawing lists
2. Create camera component system (Camera2D, Camera3D) 
3. Create light component system (Directional, Point, Spot)
4. Create mesh renderer component
5. Add entity type selector in Hierarchy panel context menu
6. Maintain compatibility with existing 2D entity system

**Tech Stack:** C++20, ECS (caffeine-core), ImGui, RHI (SDL3 optional)

---

## Task 1: Add Grid Drawing to Scene Viewport

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (add grid drawing function)
- Modify: `src/editor/SceneViewport.hpp` (add grid drawing method)

**Step 1: Add grid drawing method declaration**

In `src/editor/SceneViewport.hpp`, add to private methods:

```cpp
#ifdef CF_HAS_IMGUI
    void drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx);
#endif
```

**Step 2: Implement grid drawing function**

In `src/editor/SceneViewport.cpp`, after the `drawGizmo` function, add:

```cpp
void SceneViewport::drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    if (!m_config.grid) return;
    
    f32 gridSpacing = m_config.gridSpacing;
    f32 scale = ctx.viewportZoom * 50.0f;
    f32 scaledSpacing = gridSpacing * scale;
    
    f32 centerX = origin.x + viewportSize.x * 0.5f;
    f32 centerY = origin.y + viewportSize.y * 0.5f;
    f32 offsetX = ctx.viewportPanX * scale;
    f32 offsetY = ctx.viewportPanY * scale;
    
    ImU32 gridColor = IM_COL32(100, 100, 120, 80);
    ImU32 axisColor = IM_COL32(200, 100, 100, 150);
    
    if (scaledSpacing < 2.0f) return;
    
    f32 startX = centerX - fmod(centerX - offsetX - origin.x, scaledSpacing) - scaledSpacing;
    f32 startY = centerY - fmod(centerY - offsetY - origin.y, scaledSpacing) - scaledSpacing;
    
    for (f32 x = startX; x < origin.x + viewportSize.x + scaledSpacing * 2; x += scaledSpacing) {
        if (fabs(x - centerX) < 2.0f) {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), gridColor, 0.5f);
        }
    }
    
    for (f32 y = startY; y < origin.y + viewportSize.y + scaledSpacing * 2; y += scaledSpacing) {
        if (fabs(y - centerY) < 2.0f) {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), gridColor, 0.5f);
        }
    }
    
    drawList->AddCircle(ImVec2(centerX, centerY), 8.0f, IM_COL32(255, 200, 0, 200), 12, 2.0f);
}
```

**Step 3: Call grid drawing in render method**

In `src/editor/SceneViewport.cpp`, in the `render()` function after line 142 where gizmo text is drawn, add:

```cpp
drawGrid(drawList, origin, viewportSize, ctx);
```

Full context (around line 142):
```cpp
if (m_config.grid) {
    char modeStr[16];
    // ... existing code ...
    drawList->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(200, 200, 200, 200), buf);
}

drawGrid(drawList, origin, viewportSize, ctx);  // ADD THIS LINE

if (ctx.selectedEntity.isValid() && hovered) {
    drawGizmo(world, ctx, origin, viewportSize);
}
```

**Step 4: Verify compilation**

Run:
```bash
cd /home/pedro/repo/caffeine/build && cmake .. && make -j4 2>&1 | grep -E "error:|Built target doppio"
```

Expected: No errors, "Built target doppio" appears

**Step 5: Commit**

```bash
cd /home/pedro/repo/caffeine && git add src/editor/SceneViewport.hpp src/editor/SceneViewport.cpp && git commit -m "feat: add grid rendering to Scene Viewport

- Implement drawGrid() method for ImGui grid overlay
- Grid respects zoom and pan transformations
- Center axis highlighted in different color
- Grid spacing configurable via SceneViewport::Config"
```

---

## Task 2: Create Camera Component System

**Files:**
- Create: `src/ecs/CameraComponents.hpp`
- Modify: `src/ecs/Components.hpp` (add include)

**Step 1: Create Camera components header**

Create `src/ecs/CameraComponents.hpp`:

```cpp
#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::ECS {
using namespace Caffeine;

struct Camera2D {
    f32 zoom = 1.0f;
    f32 nearClip = 0.1f;
    f32 farClip = 1000.0f;
};

struct Camera3D {
    f32 fov = 60.0f;
    f32 nearClip = 0.1f;
    f32 farClip = 1000.0f;
    f32 aspectRatio = 16.0f / 9.0f;
};

struct CameraActive {
    bool is2D = true;
};

}  // namespace Caffeine::ECS
```

**Step 2: Include in Components.hpp**

At the end of `src/ecs/Components.hpp` before the closing namespace, add:

```cpp
#include "ecs/CameraComponents.hpp"
```

**Step 3: Verify it compiles**

Run:
```bash
cd /home/pedro/repo/caffeine/build && make -j4 2>&1 | grep -E "error:|CameraComponents"
```

Expected: No errors

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine && git add src/ecs/CameraComponents.hpp src/ecs/Components.hpp && git commit -m "feat: add Camera2D and Camera3D components

- Camera2D: zoom-based orthographic camera
- Camera3D: FOV-based perspective camera
- CameraActive: marker for active camera
- Both include near/far clip planes"
```

---

## Task 3: Create Light Component System

**Files:**
- Create: `src/ecs/LightComponents.hpp`
- Modify: `src/ecs/Components.hpp` (add include)

**Step 1: Create Light components header**

Create `src/ecs/LightComponents.hpp`:

```cpp
#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::ECS {
using namespace Caffeine;

struct Light {
    Vec4 color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    f32 intensity = 1.0f;
};

struct DirectionalLight {
    f32 shadowDistance = 100.0f;
    bool castShadows = true;
};

struct PointLight {
    f32 radius = 10.0f;
    bool castShadows = false;
};

struct SpotLight {
    f32 radius = 10.0f;
    f32 angle = 45.0f;
    bool castShadows = false;
};

}  // namespace Caffeine::ECS
```

**Step 2: Include in Components.hpp**

At the end of `src/ecs/Components.hpp`, add:

```cpp
#include "ecs/LightComponents.hpp"
```

**Step 3: Verify compilation**

Run:
```bash
cd /home/pedro/repo/caffeine/build && make -j4 2>&1 | grep -E "error:|LightComponents"
```

Expected: No errors

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine && git add src/ecs/LightComponents.hpp src/ecs/Components.hpp && git commit -m "feat: add Light components (Directional, Point, Spot)

- Light base component with color and intensity
- DirectionalLight: shadow distance and casting control
- PointLight: radius-based attenuation
- SpotLight: angle and radius for cone lighting"
```

---

## Task 4: Create Mesh Renderer Component

**Files:**
- Create: `src/ecs/MeshComponents.hpp`
- Modify: `src/ecs/Components.hpp` (add include)

**Step 1: Create Mesh components header**

Create `src/ecs/MeshComponents.hpp`:

```cpp
#pragma once
#include "core/Types.hpp"
#include <string>

namespace Caffeine::ECS {
using namespace Caffeine;

struct MeshRenderer {
    std::string meshPath;
    std::string materialPath;
    bool castShadows = true;
    bool receiveShadows = true;
};

struct SkinnedMeshRenderer {
    std::string meshPath;
    std::string materialPath;
    std::string skeletonPath;
    bool castShadows = true;
    bool receiveShadows = true;
};

}  // namespace Caffeine::ECS
```

**Step 2: Include in Components.hpp**

At the end of `src/ecs/Components.hpp`, add:

```cpp
#include "ecs/MeshComponents.hpp"
```

**Step 3: Verify compilation**

Run:
```bash
cd /home/pedro/repo/caffeine/build && make -j4 2>&1 | grep -E "error:|MeshComponents"
```

Expected: No errors

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine && git add src/ecs/MeshComponents.hpp src/ecs/Components.hpp && git commit -m "feat: add MeshRenderer components

- MeshRenderer: static mesh with material
- SkinnedMeshRenderer: animated mesh with skeleton
- Both support shadow casting and receiving"
```

---

## Task 5: Add Entity Type Selector to Hierarchy Panel

**Files:**
- Modify: `src/editor/HierarchyPanel.cpp` (expand entity creation menu)
- Modify: `src/editor/HierarchyPanel.hpp` (add helper methods)

**Step 1: Add helper methods to header**

In `src/editor/HierarchyPanel.hpp`, add to private section:

```cpp
private:
    void createEntityWithType(ECS::World& world, const char* name, const char* componentType);
```

**Step 2: Implement helper in cpp**

In `src/editor/HierarchyPanel.cpp`, add before the `onImGuiRender()` function:

```cpp
void HierarchyPanel::createEntityWithType(ECS::World& world, const char* name, const char* componentType) {
    m_context->beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity e = world.create();
    setEntityName(world, e, name);
    
    if (strcmp(componentType, "Camera2D") == 0) {
        world.add<ECS::Camera2D>(e);
    } else if (strcmp(componentType, "Camera3D") == 0) {
        world.add<ECS::Camera3D>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    } else if (strcmp(componentType, "DirectionalLight") == 0) {
        world.add<ECS::Light>(e);
        world.add<ECS::DirectionalLight>(e);
    } else if (strcmp(componentType, "PointLight") == 0) {
        world.add<ECS::Light>(e);
        world.add<ECS::PointLight>(e);
        world.add<ECS::Position3D>(e);
    } else if (strcmp(componentType, "SpotLight") == 0) {
        world.add<ECS::Light>(e);
        world.add<ECS::SpotLight>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
    } else if (strcmp(componentType, "MeshRenderer") == 0) {
        world.add<ECS::MeshRenderer>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    }
    
    m_context->selectedEntity = e;
    m_context->endUndo(world);
}
```

**Step 3: Update renderEmptyContextMenu**

In `renderEmptyContextMenu()`, replace the simple menu with:

```cpp
void HierarchyPanel::renderEmptyContextMenu() {
    if (ImGui::BeginPopupContextWindow("context_menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::BeginMenu("Create Entity")) {
            if (ImGui::MenuItem("Empty Entity")) {
                m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
                ECS::Entity e = m_world->create();
                setEntityName(*m_world, e, "New Entity");
                m_context->selectedEntity = e;
                m_context->endUndo(*m_world);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Camera");
            if (ImGui::MenuItem("Camera 2D")) {
                createEntityWithType(*m_world, "Camera 2D", "Camera2D");
            }
            if (ImGui::MenuItem("Camera 3D")) {
                createEntityWithType(*m_world, "Camera 3D", "Camera3D");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Lights");
            if (ImGui::MenuItem("Directional Light")) {
                createEntityWithType(*m_world, "Directional Light", "DirectionalLight");
            }
            if (ImGui::MenuItem("Point Light")) {
                createEntityWithType(*m_world, "Point Light", "PointLight");
            }
            if (ImGui::MenuItem("Spot Light")) {
                createEntityWithType(*m_world, "Spot Light", "SpotLight");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Rendering");
            if (ImGui::MenuItem("Mesh Renderer")) {
                createEntityWithType(*m_world, "Mesh Renderer", "MeshRenderer");
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}
```

**Step 4: Verify compilation**

Run:
```bash
cd /home/pedro/repo/caffeine/build && cmake .. && make -j4 2>&1 | grep -E "error:|undefined reference" | head -20
```

Expected: No errors

**Step 5: Commit**

```bash
cd /home/pedro/repo/caffeine && git add src/editor/HierarchyPanel.hpp src/editor/HierarchyPanel.cpp && git commit -m "feat: add entity type selector to Hierarchy panel

- Context menu now shows 'Create Entity' submenu
- Options: Empty, Camera 2D/3D, Directional/Point/Spot Light, Mesh Renderer
- Each type auto-populates required components
- 3D entities get Position3D, Rotation3D, Scale3D
- Lights get Light component plus type-specific components"
```

---

## Task 6: Final Verification and Testing

**Files:**
- No new files

**Step 1: Full rebuild**

Run:
```bash
cd /home/pedro/repo/caffeine/build && cmake .. && make clean && make -j4 2>&1 | tail -20
```

Expected: All targets build successfully, zero errors

**Step 2: Launch doppio and verify viewport**

Run:
```bash
cd /home/pedro/repo/caffeine/build && ./doppio
```

Expected:
- Scene Viewport shows grid with axis lines
- Grid responds to zoom (mouse wheel)
- Grid responds to pan (middle mouse drag)
- Right-click in Hierarchy → "Create Entity" menu appears
- Can create Camera 2D, Camera 3D, Lights, Mesh Renderer

**Step 3: Test entity creation**

In doppio:
1. Right-click in Hierarchy panel
2. Hover "Create Entity"
3. Click "Camera 3D"
4. Verify new entity appears in Hierarchy with "Camera 3D" name
5. Select it → Inspector should show Camera3D and 3D transform components

**Step 4: Commit verification**

```bash
cd /home/pedro/repo/caffeine && git log --oneline | head -10
```

Expected: All 6 commits visible

**Step 5: Final comprehensive test**

Run:
```bash
cd /home/pedro/repo/caffeine/build && make -j4 2>&1 | grep -c "Built target"
```

Expected: All targets built (doppio, caffeine-core, CaffeineTest, DoppioTest)

---

## Verification Checklist

- [ ] Grid visible in Scene Viewport
- [ ] Grid zooms with mouse wheel
- [ ] Grid pans with middle mouse drag
- [ ] Entity creation menu accessible via right-click in Hierarchy
- [ ] Can create Camera 2D entity
- [ ] Can create Camera 3D entity (with 3D transforms)
- [ ] Can create Directional Light
- [ ] Can create Point Light
- [ ] Can create Spot Light
- [ ] Can create Mesh Renderer
- [ ] All new components appear in Inspector when entity selected
- [ ] No compilation errors
- [ ] No linker errors
- [ ] doppio executable runs without crashes

---

## Execution Strategy

This plan has 6 tasks, each taking 2-5 minutes:
1. Grid rendering (SceneViewport)
2. Camera components
3. Light components
4. Mesh components
5. Entity type selector UI
6. Verification

**Estimated total time:** 30-45 minutes

All tasks are independent compilation-wise (no circular dependencies) and should be committed individually.
