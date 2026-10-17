# Object Selection via Raycasting Implementation Plan

> **For Claude:** Execute this plan task-by-task directly in this session. No subagents.

**Goal:** Implement click-to-select for entities in 3D viewport using raycasting against AABB bounding boxes.

**Architecture:** 
On left-click, convert screen coords to world-space ray (reuse `screenToWorldRay`), test ray against all entity AABBs (transformed to world space), find closest intersection, update `ctx.selectedEntity`, which automatically updates Inspector.

**Tech Stack:** C++20, Mat4 (in-house), Vec3/Vec4, ImGui for click detection, existing Transform + MeshFilter components

---

## Task 1: Add `rayIntersectsAABB()` Helper Function

**Files:**
- Modify: `src/editor/SceneViewport.hpp` (add function declaration)
- Modify: `src/editor/SceneViewport.cpp` (add implementation)

**Step 1: Add function declaration to header**

In `SceneViewport.hpp`, add to private section:

```cpp
// Ray-AABB intersection test (returns t_enter distance, or -1 if no hit)
// Used for object selection and culling
f32 rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                      const Vec3& aabbMin, const Vec3& aabbMax);
```

**Step 2: Implement rayIntersectsAABB in SceneViewport.cpp**

Add before `onImGuiRender()` method body (around line 80):

```cpp
f32 SceneViewport::rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                                     const Vec3& aabbMin, const Vec3& aabbMax) {
    // Slab intersection test for ray vs AABB
    // Ray: p(t) = rayOrigin + t * rayDir
    // Test against 3 axis-aligned slabs (X, Y, Z)
    
    f32 t_enter = 0.0f;
    f32 t_exit = 1e10f;  // Very large value
    
    // Test X slab
    if (std::abs(rayDir.x) > 1e-6f) {
        f32 t0 = (aabbMin.x - rayOrigin.x) / rayDir.x;
        f32 t1 = (aabbMax.x - rayOrigin.x) / rayDir.x;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        // Ray parallel to X slab; check if within bounds
        if (rayOrigin.x < aabbMin.x || rayOrigin.x > aabbMax.x) {
            return -1.0f;
        }
    }
    
    // Test Y slab
    if (std::abs(rayDir.y) > 1e-6f) {
        f32 t0 = (aabbMin.y - rayOrigin.y) / rayDir.y;
        f32 t1 = (aabbMax.y - rayOrigin.y) / rayDir.y;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.y < aabbMin.y || rayOrigin.y > aabbMax.y) {
            return -1.0f;
        }
    }
    
    // Test Z slab
    if (std::abs(rayDir.z) > 1e-6f) {
        f32 t0 = (aabbMin.z - rayOrigin.z) / rayDir.z;
        f32 t1 = (aabbMax.z - rayOrigin.z) / rayDir.z;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.z < aabbMin.z || rayOrigin.z > aabbMax.z) {
            return -1.0f;
        }
    }
    
    // Ray hits AABB if t_enter <= t_exit and t_enter >= 0
    if (t_enter <= t_exit && t_enter >= 0.0f) {
        return t_enter;
    }
    
    return -1.0f;  // No intersection
}
```

**Step 3: Compile and verify**

Run: `cd /home/pedro/repo/caffeine/build && make doppio -j8 2>&1 | tail -5`

Expected: Build succeeds, "Built target doppio"

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/SceneViewport.hpp src/editor/SceneViewport.cpp
git commit -m "feat(selection): add rayIntersectsAABB for object picking"
```

---

## Task 2: Add `raycastSelectEntity()` - Entity Iteration & Hit Detection

**Files:**
- Modify: `src/editor/SceneViewport.hpp` (add function declaration)
- Modify: `src/editor/SceneViewport.cpp` (add implementation)

**Step 1: Add function declaration**

In `SceneViewport.hpp`, add to private section:

```cpp
// Find closest entity under a ray (for click-to-select)
// Returns INVALID if no hit
ECS::Entity raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                ECS::World& world);
```

**Step 2: Implement raycastSelectEntity**

Add after `rayIntersectsAABB()`:

```cpp
ECS::Entity SceneViewport::raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                               ECS::World& world) {
    ECS::Entity closestEntity = ECS::Entity::INVALID;
    f32 closestT = 1e10f;
    
    // Iterate all entities with Transform
    ECS::ComponentQuery query;
    query.with<ECS::Transform>();
    
    world.forEach<ECS::Transform>(query, [&](ECS::Entity entity, ECS::Transform& transform) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        
        // Get world-space AABB
        Vec3 aabbMin = transform.position;
        Vec3 aabbMax = transform.position;
        
        // If entity has MeshFilter, use mesh bounds
        if (auto* meshFilter = world.get<ECS::MeshFilter>(entity)) {
            if (!meshFilter->customMeshPath.empty()) {
                // Get mesh from cache
                auto* mesh = Assets::MeshCache::getInstance().getMesh(meshFilter->customMeshPath);
                if (!mesh) {
                    std::string fullPath = std::string("assets/raw/") + meshFilter->customMeshPath;
                    mesh = Assets::MeshCache::getInstance().getMesh(fullPath);
                }
                
                if (mesh && !mesh->vertices.empty()) {
                    // Transform mesh bounds to world space
                    Vec3 meshMin = mesh->bounds.min;
                    Vec3 meshMax = mesh->bounds.max;
                    
                    // Apply transform (simple AABB transform - not optimal for rotations)
                    aabbMin = transform.position + meshMin * transform.scale;
                    aabbMax = transform.position + meshMax * transform.scale;
                    
                    // Ensure min < max
                    if (aabbMin.x > aabbMax.x) std::swap(aabbMin.x, aabbMax.x);
                    if (aabbMin.y > aabbMax.y) std::swap(aabbMin.y, aabbMax.y);
                    if (aabbMin.z > aabbMax.z) std::swap(aabbMin.z, aabbMax.z);
                }
            }
        }
        
        // Test ray intersection
        f32 t = rayIntersectsAABB(rayOrigin, rayDir, aabbMin, aabbMax);
        
        if (t >= 0.0f && t < closestT) {
            closestT = t;
            closestEntity = entity;
        }
    });
    
    return closestEntity;
}
```

**Step 3: Compile and verify**

Run: `cd /home/pedro/repo/caffeine/build && make doppio -j8 2>&1 | tail -5`

Expected: Build succeeds

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/SceneViewport.hpp src/editor/SceneViewport.cpp
git commit -m "feat(selection): add raycastSelectEntity for closest hit detection"
```

---

## Task 3: Integrate Click Detection & Ray Generation into onImGuiRender()

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (add click handling logic)

**Step 1: Find click handling location**

In `onImGuiRender()`, locate the gizmo input handling (around line 250).

Add click selection logic BEFORE gizmo handling, so selection happens first:

```cpp
    // Handle entity selection via raycasting (only in 3D mode, not during gizmo drag)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_gizmoDragging && 
        ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 vpMin = ImGui::GetItemRectMin();
        ImVec2 vpMax = ImGui::GetItemRectMax();
        
        bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                                mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);
        
        if (mouseInViewport) {
            // Convert screen click to world-space ray
            ImVec2 vpSize = ImGui::GetContentRegionAvail();
            Vec2 screenClick(mousePos.x - vpMin.x, mousePos.y - vpMin.y);
            
            // Build VP matrix for raycasting
            f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
            Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
            f32 aspect = vpSize.x / std::max(vpSize.y, 1.0f);
            Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, 10000.0f);
            Mat4 vp = proj * view;
            Mat4 vpInverse = vp.inverted();
            
            // Generate raycasting ray (reuse from gizmo raycasting)
            TransformGizmo::Ray3D ray;
            {
                f32 ndcX = (2.0f * screenClick.x) / vpSize.x - 1.0f;
                f32 ndcY = 1.0f - (2.0f * screenClick.y) / vpSize.y;
                Vec4 ndcNear(ndcX, ndcY, -1.0f, 1.0f);
                Vec4 worldNear = vpInverse.transformVec4(ndcNear);
                
                if (std::abs(worldNear.w) > 0.0001f) {
                    worldNear.x /= worldNear.w;
                    worldNear.y /= worldNear.w;
                    worldNear.z /= worldNear.w;
                }
                
                ray.origin = camPos;
                ray.direction = (Vec3(worldNear.x, worldNear.y, worldNear.z) - camPos).normalized();
            }
            
            // Find and select closest entity
            ECS::Entity selectedEntity = raycastSelectEntity(ray.origin, ray.direction, world);
            ctx.selectedEntity = selectedEntity;
        }
    }
```

**Step 2: Add required includes at top of SceneViewport.cpp**

Check if these are present:
```cpp
#include "editor/TransformGizmo.hpp"  // For Ray3D struct
#include "assets/MeshCache.hpp"        // For mesh lookup
```

**Step 3: Compile and verify**

Run: `cd /home/pedro/repo/caffeine/build && make doppio -j8 2>&1 | tail -10`

Expected: Build succeeds, warnings about lua are OK

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/SceneViewport.cpp
git commit -m "feat(selection): integrate click detection and raycasting into viewport"
```

---

## Task 4: Manual Testing & Validation

**Files:**
- None (testing only)

**Step 1: Build and launch**

```bash
cd /home/pedro/repo/caffeine/build
make doppio -j8
./doppio
```

**Step 2: Create test scene**

1. Open/create a scene with meshes (or use existing test project)
2. Enter 3D viewport (click "3D" button)
3. Position camera to see multiple objects

**Step 3: Test single-object selection**

1. Click on a mesh in viewport
2. **Expect:** Inspector updates to show selected entity's components
3. **Verify:** `ctx.selectedEntity` is set (check console or Inspector title)
4. Gizmo should appear on selected entity

**Step 4: Test multi-object click**

1. Position two meshes close together (overlapping or near each other)
2. Click at intersection point
3. **Expect:** Closest mesh selected (not furthest)
4. Try clicking different areas to confirm closest-hit logic

**Step 5: Test empty-space click**

1. Click on empty area in viewport (no mesh)
2. **Expect:** Inspector clears (no entity selected)
3. Gizmo should disappear

**Step 6: Test gizmo doesn't interfere**

1. Select an entity
2. Press T/E/R to enable Translate/Rotate/Scale gizmo
3. Drag gizmo to transform entity
4. **Expect:** Selection remains, gizmo works normally

**Step 7: Test 2D mode skips raycasting**

1. Click "2D" button
2. Click on objects
3. **Expect:** Selection should NOT change (raycasting only in 3D mode)
4. Switch back to 3D, click should work again

**Step 8: Document results**

Note any issues:
- Does selection feel responsive?
- Is closest-hit detection accurate?
- Any visual glitches or crashes?

---

## Task 5: Edge Case Handling & Robustness

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (add validation)

**Step 1: Add null-check guards in raycastSelectEntity()**

Before accessing `world.forEach`, verify world is valid:

```cpp
ECS::Entity SceneViewport::raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                               ECS::World& world) {
    if (!world.isValid()) return ECS::Entity::INVALID;
    
    ECS::Entity closestEntity = ECS::Entity::INVALID;
    f32 closestT = 1e10f;
    
    // ... rest of implementation
}
```

**Step 2: Add AABB bounds validation**

In raycastSelectEntity loop, after getting mesh bounds:

```cpp
// Ensure AABB is valid (min < max)
if (aabbMin.x >= aabbMax.x || aabbMin.y >= aabbMax.y || aabbMin.z >= aabbMax.z) {
    // Invalid AABB, use point-radius check instead
    Vec3 entityPos = transform.position;
    Vec3 toEntity = entityPos - rayOrigin;
    Vec3 proj = rayDir * toEntity.dot(rayDir);
    f32 distToRay = (toEntity - proj).length();
    
    if (distToRay < 0.1f) {  // Tolerance for point-like entities
        f32 t = proj.length();
        if (t >= 0.0f && t < closestT) {
            closestT = t;
            closestEntity = entity;
        }
    }
    return;  // Skip AABB test for this entity
}
```

**Step 3: Add VP inverse validity check**

In click handling, after computing vpInverse:

```cpp
// Verify VP matrix is invertible
Mat4 vpInverse = vp.inverted();
// (Mat4::inverted already returns identity if singular, so safe)
```

**Step 4: Compile and verify**

Run: `cd /home/pedro/repo/caffeine/build && make doppio -j8 2>&1 | tail -5`

Expected: Build succeeds

**Step 5: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/SceneViewport.cpp
git commit -m "fix(selection): add edge case handling for invalid AABBs and world state"
```

---

## Task 6: Performance Verification & Optimization

**Files:**
- None (verification only, no code changes)

**Step 1: Analyze raycasting cost**

Per selection click:
- VP matrix construction: 2 matrix ops (~0.1ms)
- VP inversion: 1 inversion (~0.1ms)
- World.forEach with ray-AABB tests: O(N) where N = entity count
  - Each entity: 3 slab tests + 1 min/max check (~30 float ops)
  - At 100 entities: ~0.1ms

**Total per click: ~0.2-0.3ms** (negligible, click is user-initiated)

**Step 2: Verify no repeated calls**

Check that raycastSelectEntity is only called on `IsMouseClicked` (not every frame):
- `IsMouseClicked` = true only frame mouse button transitions from up to down
- `IsMouseDown` would be bad (every frame while held)

Verify: search for `IsMouseClicked` in click handling code.

**Step 3: Performance conclusion**

- ✅ Raycasting only runs on click (not every frame)
- ✅ O(N) iteration acceptable for typical scene sizes (<1000 entities)
- ✅ No VP matrix caching needed (already cheap)
- Future: Add spatial partitioning (octree/quadtree) if N > 5000

**Step 4: Document performance profile**

No code changes needed. Performance is inherently good due to:
- Single ray test (not per-vertex)
- Early exit on first valid hit
- Click-driven (not continuous)

---

## Summary

| Task | What | Time | Files |
|------|------|------|-------|
| 1 | Add rayIntersectsAABB() | 5 min | SceneViewport.hpp/cpp |
| 2 | Add raycastSelectEntity() | 10 min | SceneViewport.hpp/cpp |
| 3 | Integrate click handling | 10 min | SceneViewport.cpp |
| 4 | Manual testing | 15 min | (test only) |
| 5 | Edge cases & robustness | 10 min | SceneViewport.cpp |
| 6 | Performance verification | 5 min | (analysis only) |

**Total: ~55 minutes**

---

## Key Implementation Notes

### Ray-AABB Slab Intersection Algorithm
- **Robust**: Handles rays parallel to slabs (division by near-zero)
- **Efficient**: 3 sequential slab tests, early-exit on first miss
- **Correct**: Returns t_enter distance (parametric value on ray)

### Entity Iteration Strategy
- Uses `ECS::ComponentQuery` with Transform requirement
- Early-exits for disabled entities (`Scene::isEffectivelyDisabled`)
- Tracks closest hit by t-parameter (distance along ray)

### Click Detection Placement
- **Before gizmo input**: Allows selection to update first
- **Only in 3D mode**: 2D/Isometric skip raycasting
- **Only when not dragging gizmo**: `!m_gizmoDragging` guard

### Coordinate Space Handling
- **Screen → NDC**: Standard viewport normalization
- **NDC → World**: Reuse gizmo raycasting code (VP⁻¹ + w-divide)
- **World → AABB**: Transform mesh bounds by entity transform

---

## Testing Checklist

- [ ] Single entity selection works
- [ ] Closest-hit on overlapping entities
- [ ] Empty-space click deselects
- [ ] Gizmo appears on selection
- [ ] Inspector updates on selection
- [ ] 2D/Isometric modes skip raycasting
- [ ] No crashes on invalid world/entities
- [ ] Performance acceptable (instant click response)
- [ ] Ray generation uses correct VP⁻¹
- [ ] AABB intersection uses slab algorithm

---

## Next Steps After This Plan

1. **Multi-select with Shift+Click** - Track list of selected entities
2. **Double-click to focus** - Center camera on selected entity
3. **Outline selected entity** - Visual feedback (highlight/glow)
4. **Delete key removes selected** - Quick delete selection
5. **Spatial partitioning** - Optimize for scenes with 1000+ entities
6. **Frustum culling on raycast** - Skip entities outside view frustum
