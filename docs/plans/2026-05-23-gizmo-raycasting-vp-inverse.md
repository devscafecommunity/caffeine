# Transform Gizmo Raycasting with VP⁻¹ Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers/executing-plans to implement this plan task-by-task.

**Goal:** Upgrade gizmo axis picking from screen-space distance to 3D raycasting via VP⁻¹ inverse matrix, eliminating foreshortening-related picking errors across all viewing angles.

**Architecture:** 
1. Compute VP⁻¹ once per gizmo render frame
2. Convert mouse screen coords to 3D world-space ray via NDC + perspective divide
3. Test ray intersection against finite axis segments (with world-space threshold)
4. Replace `pointToLineDistance()` call in `intersectTest()` with proper ray-to-segment tests
5. Keep existing drag/transform logic (still screen-delta based, which works fine)

**Tech Stack:** C++20, Mat4 (in-house), Vec3/Vec4, ImGui for mouse input

**Critical Technical Notes:**
- **NDC Perspective Divide:** `worldCoords = vpInverse * ndc4`, then `worldCoords /= worldCoords.w`
- **Axis Segment Clipping:** Clamp closest point on ray to axis segment `[0, gizmoSize]`, not infinite line
- **World-Space Threshold:** Use ~0.05f units (not pixels). Future: multiply by camera distance for visual consistency

---

## Task 1: Add `screenToWorldRay()` Helper Function

**Files:**
- Modify: `src/editor/TransformGizmo.hpp` (add function declaration)
- Modify: `src/editor/TransformGizmo.cpp` (add function implementation)

**Step 1: Add function declaration to header**

In `TransformGizmo.hpp`, add to private section after line 56:

```cpp
struct Ray3D {
    Vec3 origin;
    Vec3 direction;  // normalized
};

// Convert screen coordinates to 3D world-space ray via VP⁻¹
Ray3D screenToWorldRay(const Vec2& screenPos, const Mat4& vpInverse, 
                       const ImVec2& viewportSize, const Vec3& camPos);
```

**Step 2: Implement in TransformGizmo.cpp**

Add this function after `pointToLineDistance()` (around line 510):

```cpp
TransformGizmo::Ray3D TransformGizmo::screenToWorldRay(
    const Vec2& screenPos, const Mat4& vpInverse, 
    const ImVec2& viewportSize, const Vec3& camPos) {
    
    // Convert screen coords to NDC [-1, 1]
    f32 ndcX = (2.0f * screenPos.x) / viewportSize.x - 1.0f;
    f32 ndcY = 1.0f - (2.0f * screenPos.y) / viewportSize.y;
    
    // Near plane point in NDC (z = -1 in OpenGL convention)
    Vec4 ndcNear(ndcX, ndcY, -1.0f, 1.0f);
    
    // Unproject to world via vpInverse
    Vec4 worldNear = vpInverse.transformVec4(ndcNear);
    
    // Perspective divide (CRITICAL: must normalize by w)
    if (std::abs(worldNear.w) > 0.0001f) {
        worldNear.x /= worldNear.w;
        worldNear.y /= worldNear.w;
        worldNear.z /= worldNear.w;
    }
    
    Vec3 rayOrigin = camPos;
    Vec3 rayTarget(worldNear.x, worldNear.y, worldNear.z);
    Vec3 rayDir = (rayTarget - rayOrigin).normalized();
    
    return Ray3D{rayOrigin, rayDir};
}
```

**Step 3: Verify function signature matches usage pattern**

Check: File compiles and Ray3D type is available in private scope.

Run: `cd /home/pedro/repo/caffeine/build && make doppio 2>&1 | grep -E "error|Ray3D"`

Expected: No errors mentioning Ray3D or screenToWorldRay.

**Step 4: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/TransformGizmo.hpp src/editor/TransformGizmo.cpp
git commit -m "feat(gizmo): add screenToWorldRay helper for VP-inverse raycasting"
```

---

## Task 2: Add `rayToAxisSegmentDistance()` Function

**Files:**
- Modify: `src/editor/TransformGizmo.cpp` (new function)

**Step 1: Understand the algorithm**

Ray-to-segment distance:
1. Ray defined by `rayOrigin + t * rayDir` (t ≥ 0)
2. Axis segment from `axisOrigin` to `axisOrigin + axisDir * axisLength`
3. Find closest point on ray to axis segment via minimizing: `distance² = |ray(t) - axisSegment(s)|²`
4. **CRITICAL:** Clamp `s` to `[0, axisLength]` (not infinite line)

**Step 2: Add function declaration to header**

In `TransformGizmo.hpp`, add after `screenToWorldRay`:

```cpp
// Compute closest distance from ray to finite axis segment
// Returns distance and clamped parameter s on axis [0, axisLength]
struct RayAxisTest {
    f32 distance;
    f32 t_ray;      // parameter on ray where closest point lies
    f32 s_axis;     // parameter on axis [0, axisLength]
};

RayAxisTest rayToAxisSegmentDistance(
    const Ray3D& ray, 
    const Vec3& axisOrigin, const Vec3& axisDir, f32 axisLength);
```

**Step 3: Implement in TransformGizmo.cpp**

Add after `screenToWorldRay()`:

```cpp
TransformGizmo::RayAxisTest TransformGizmo::rayToAxisSegmentDistance(
    const Ray3D& ray, 
    const Vec3& axisOrigin, const Vec3& axisDir, f32 axisLength) {
    
    // Vector from ray origin to axis origin
    Vec3 w = axisOrigin - ray.origin;
    
    // Solve for closest points:
    // ray(t) = rayOrigin + t * rayDir
    // axis(s) = axisOrigin + s * axisDir, s ∈ [0, axisLength]
    // Minimize: |ray(t) - axis(s)|²
    
    f32 a = ray.direction.dot(ray.direction);     // |rayDir|² = 1 (normalized)
    f32 b = ray.direction.dot(axisDir);
    f32 c = axisDir.dot(axisDir);
    f32 d = ray.direction.dot(w);
    f32 e = axisDir.dot(w);
    
    f32 denom = a * c - b * b;
    f32 t_ray = 0.0f;
    f32 s_axis = 0.0f;
    
    if (std::abs(denom) > 1e-6f) {
        t_ray = (b * e - c * d) / denom;
        s_axis = (a * e - b * d) / denom;
    } else {
        // Rays are parallel; use perpendicular distance
        s_axis = (std::abs(c) > 1e-6f) ? (e / c) : 0.0f;
    }
    
    // Clamp t_ray to positive direction (ray goes forward)
    t_ray = std::max(0.0f, t_ray);
    
    // CRITICAL: Clamp s_axis to axis segment bounds [0, axisLength]
    s_axis = std::max(0.0f, std::min(axisLength, s_axis));
    
    // Compute closest points
    Vec3 closestOnRay = ray.origin + ray.direction * t_ray;
    Vec3 closestOnAxis = axisOrigin + axisDir * s_axis;
    
    // Distance between them
    f32 distance = (closestOnRay - closestOnAxis).length();
    
    return RayAxisTest{distance, t_ray, s_axis};
}
```

**Step 4: Verify compilation**

Run: `cd /home/pedro/repo/caffeine/build && make doppio 2>&1 | grep -E "error|undefined"`

Expected: No errors about RayAxisTest or rayToAxisSegmentDistance.

**Step 5: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/TransformGizmo.hpp src/editor/TransformGizmo.cpp
git commit -m "feat(gizmo): add rayToAxisSegmentDistance for finite axis picking"
```

---

## Task 3: Refactor `intersectTest()` to Use Raycasting

**Files:**
- Modify: `src/editor/TransformGizmo.cpp` lines 389-416

**Step 1: Understand current `intersectTest()` signature**

Current call (line 122):
```cpp
m_hoveredAxis = intersectTest(mousePosGlm, screenPos, endX, endY, endZ, handleLen, ctx.gizmoMode, zDimmed, xCollapsed, yCollapsed, zCollapsed);
```

We need:
- Screen mouse position ✓
- Gizmo screen position ✓
- Projected axis endpoints (for visualization, not picking) - keep for reference
- Handle length in screen space - keep for visualization
- **NEW:** VP⁻¹ matrix, camera position, viewport size, world-space axis positions

**Step 2: Update `intersectTest()` signature**

Replace the function declaration at line 52-55 in `TransformGizmo.hpp`:

```cpp
GizmoAxis intersectTest(const Vec2& mousePos, 
                        const Mat4& vpInverse, const Vec3& camPos,
                        const ImVec2& viewportSize,
                        const Vec3& axisX, const Vec3& axisY, const Vec3& axisZ,
                        f32 axisLength,
                        EditorContext::GizmoMode mode, bool zDimmed,
                        bool xCollapsed, bool yCollapsed, bool zCollapsed);
```

**Step 3: Replace `intersectTest()` implementation**

In `TransformGizmo.cpp` at line 389, replace the entire function with:

```cpp
GizmoAxis TransformGizmo::intersectTest(
    const Vec2& mousePos, 
    const Mat4& vpInverse, const Vec3& camPos,
    const ImVec2& viewportSize,
    const Vec3& axisX, const Vec3& axisY, const Vec3& axisZ,
    f32 axisLength,
    EditorContext::GizmoMode mode, bool zDimmed,
    bool xCollapsed, bool yCollapsed, bool zCollapsed) {
    
    // Test center first (close proximity in world units ~0.2f)
    Vec3 gizmoOrigin = axisX; // All axes share same origin point
    Ray3D ray = screenToWorldRay(mousePos, vpInverse, viewportSize, camPos);
    
    Vec3 toOrigin = gizmoOrigin - ray.origin;
    f32 distToOrigin = (toOrigin - ray.direction * toOrigin.dot(ray.direction)).length();
    if (distToOrigin < 0.2f) {  // Center grab area (world units)
        return GizmoAxis::Center;
    }
    
    // World-space threshold for axis picking (0.05f units)
    const f32 AXIS_THRESHOLD = 0.05f;
    
    // Test X axis
    if (!xCollapsed) {
        Vec3 axisDir = axisX.normalized();  // Direction from origin
        RayAxisTest testX = rayToAxisSegmentDistance(ray, axisX, axisDir, axisLength);
        if (testX.distance < AXIS_THRESHOLD) {
            return GizmoAxis::X;
        }
    }
    
    // Test Y axis
    if (!yCollapsed) {
        Vec3 axisDir = axisY.normalized();
        RayAxisTest testY = rayToAxisSegmentDistance(ray, axisY, axisDir, axisLength);
        if (testY.distance < AXIS_THRESHOLD) {
            return GizmoAxis::Y;
        }
    }
    
    // Test Z axis (respect zDimmed in 2D mode)
    if (!zDimmed && !zCollapsed && mode != EditorContext::GizmoMode::None) {
        Vec3 axisDir = axisZ.normalized();
        RayAxisTest testZ = rayToAxisSegmentDistance(ray, axisZ, axisDir, axisLength);
        if (testZ.distance < AXIS_THRESHOLD) {
            return GizmoAxis::Z;
        }
    }
    
    return GizmoAxis::None;
}
```

**Step 4: Update call site in `onImGuiRender()`**

At line 122, replace:
```cpp
m_hoveredAxis = intersectTest(mousePosGlm, screenPos, endX, endY, endZ, handleLen, ctx.gizmoMode, zDimmed, xCollapsed, yCollapsed, zCollapsed);
```

With:
```cpp
// Build VP matrix for raycasting
f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
f32 aspect = vpSize.x / std::max(vpSize.y, 1.0f);
Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, 10000.0f);
Mat4 vp = proj * view;
Mat4 vpInverse = vp.inverted();

// World-space axis vectors (from entity origin, in world units)
Vec3 entityPos = transform->position;
Vec3 worldAxisX = entityPos + Vec3(handleWorld, 0.0f, 0.0f);
Vec3 worldAxisY = entityPos + Vec3(0.0f, handleWorld, 0.0f);
Vec3 worldAxisZ = entityPos + Vec3(0.0f, 0.0f, handleWorld);

// Perform raycasting intersection test
m_hoveredAxis = intersectTest(
    mousePosGlm, vpInverse, camPos, vpSize,
    worldAxisX, worldAxisY, worldAxisZ, handleWorld,
    ctx.gizmoMode, zDimmed, xCollapsed, yCollapsed, zCollapsed);
```

**Step 5: Compile and verify**

Run: `cd /home/pedro/repo/caffeine/build && make doppio 2>&1 | tail -20`

Expected: Successful build with no errors. May have warnings about unused variables (that's OK for now).

**Step 6: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/TransformGizmo.hpp src/editor/TransformGizmo.cpp
git commit -m "feat(gizmo): integrate raycasting into intersectTest for world-space picking"
```

---

## Task 4: Manual Testing & Visual Validation

**Files:**
- None (testing only)

**Step 1: Build and launch editor**

```bash
cd /home/pedro/repo/caffeine/build
make doppio -j8
./doppio
```

**Step 2: Create a test scene**

1. Open/create a scene with a simple cube or mesh
2. Enter 3D viewport (click "3D" button top-right)
3. Rotate the camera aggressively (yaw ~90°, pitch varying)
4. Check that gizmo axes are visible and centered on entity

**Step 3: Test axis picking**

For each axis (X, Y, Z):
1. Hover over the axis line in screen space
2. **Expect:** Axis turns yellow (hovered)
3. **Critical test:** Move camera to an angle where the axis is heavily foreshortened (pointing nearly at camera)
   - **Old behavior:** Click becomes impossible or unreliable
   - **New behavior:** Click should still work reliably (raycasting in world space)
4. Try clicking and dragging along each axis
   - Entity should move along that axis in world space

**Step 4: Test center grab**

1. Hover over the center (gizmo origin)
2. **Expect:** Entity stays in place, no axis selected
3. Click near center (not on any axis)
4. **Expect:** Gizmo doesn't activate (Center grab not fully implemented yet, but shouldn't crash)

**Step 5: Document observations**

Write down:
- ✓ Does hover detection work at all angles?
- ✓ Does picking remain stable when axes are foreshortened?
- ✓ Any visual glitches or unexpected behavior?

---

## Task 5: Handle Edge Cases & Debug

**Files:**
- Modify: `src/editor/TransformGizmo.cpp` (debugging & fixes)

**Step 1: Verify perspective divide correctness**

Add temporary debug output in `screenToWorldRay()` after perspective divide:

```cpp
// DEBUG: Verify perspective divide worked
if (std::abs(worldNear.w) > 0.0001f) {
    worldNear.x /= worldNear.w;
    worldNear.y /= worldNear.w;
    worldNear.z /= worldNear.w;
    // Optional: printf("[DEBUG] World point after divide: (%.3f, %.3f, %.3f) w=%.3f\n", 
    //     worldNear.x, worldNear.y, worldNear.z, worldNear.w);
}
```

**Step 2: Check VP⁻¹ validity**

Before calling `intersectTest()`, verify inversion succeeded:

```cpp
if (!vp.inverted().isValid()) {
    // Fallback: skip raycasting this frame
    m_hoveredAxis = GizmoAxis::None;
} else {
    // ... perform raycasting
}
```

(Add `Mat4::isValid()` method if needed: checks `det(M) ≠ 0`)

**Step 3: Clamp axis length in world space**

Verify `handleWorld` is reasonable (e.g., > 0.01f). If it's too small, raycasting becomes difficult:

```cpp
if (handleWorld < 0.01f) {
    handleWorld = 0.01f;  // Minimum axis length for picking
}
```

**Step 4: Recompile and test**

```bash
cd /home/pedro/repo/caffeine/build && make doppio -j8
./doppio
```

Run the same visual tests from Task 4.

**Step 5: Remove debug output if successful**

If tests pass, comment out or remove debug printf statements.

**Step 6: Commit**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/TransformGizmo.cpp src/editor/TransformGizmo.hpp
git commit -m "fix(gizmo): add edge case handling for VP inverse and threshold validation"
```

---

## Task 6: Performance & Threshold Tuning

**Files:**
- Modify: `src/editor/TransformGizmo.cpp` (constants)

**Step 1: Measure performance impact**

Profile the `intersectTest()` call:
- Ray casting 3 axes per frame
- Ray-segment distance × 3 = ~9 dot products + 3 sqrts
- Should be **negligible** (~0.01ms for gizmo picking alone)

If you notice frame drops:
- Check if VP⁻¹ inversion is being called unnecessarily
- Consider caching VP⁻¹ in EditorContext (Task 7 follow-up)

**Step 2: Tune AXIS_THRESHOLD**

Current: `0.05f` world units

Test different distances:
- Too small (0.01f): Hard to click, especially from distance
- Too large (0.2f): Can click "empty" space and activate gizmo
- Sweet spot: ~0.05f-0.1f

**If camera is far away and gizmo is hard to click:**
Optionally implement distance-based threshold scaling:
```cpp
f32 distToCam = (axisX - camPos).length();
f32 effectiveThreshold = AXIS_THRESHOLD * (distToCam * 0.01f);  // Scale by distance
```

For now, keep it simple: `0.05f` fixed.

**Step 3: Commit threshold tuning**

```bash
cd /home/pedro/repo/caffeine
git add src/editor/TransformGizmo.cpp
git commit -m "perf(gizmo): tune raycasting threshold and verify performance"
```

---

## Task 7: (Follow-up) Cache VP⁻¹ in EditorContext

**Files:**
- Modify: `src/editor/EditorContext.hpp` (add fields)
- Modify: `src/editor/SceneViewport.cpp` (populate cache)

**Context:** The VP and VP⁻¹ matrices are rebuilt for every gizmo frame (and grid, mesh, etc.). Caching them in EditorContext avoids recomputation.

**Files to modify:**
- `EditorContext.hpp`: Add `Mat4 cachedVP`, `Mat4 cachedVPInverse`
- `onImGuiRender()` in SceneViewport: Build VP/VP⁻¹ once per frame and cache in ctx

**This is a performance optimization—do it after raycasting is fully working.**

---

## Summary

| Task | What | Time | Files |
|------|------|------|-------|
| 1 | Add `screenToWorldRay()` | 5 min | TransformGizmo.hpp/cpp |
| 2 | Add `rayToAxisSegmentDistance()` | 10 min | TransformGizmo.hpp/cpp |
| 3 | Refactor `intersectTest()` + integrate raycasting | 15 min | TransformGizmo.hpp/cpp |
| 4 | Manual testing & validation | 10 min | (test only) |
| 5 | Edge case handling & debug | 10 min | TransformGizmo.cpp |
| 6 | Performance tuning & threshold | 5 min | TransformGizmo.cpp |
| 7 | (Follow-up) VP⁻¹ caching in EditorContext | 10 min | EditorContext + SceneViewport |

**Total: ~65 minutes for Tasks 1-6 (full working raycasting gizmo)**

**Key Deliverables:**
- ✅ Screen-to-world raycasting via VP⁻¹
- ✅ Finite axis-segment collision detection
- ✅ World-space threshold for stable picking
- ✅ Translate gizmo with accurate axis picking at any viewing angle
- ✅ Ready for pattern-matching to Rotate/Scale modes

---

## Testing Checklist

- [ ] Gizmo renders at entity position in 3D
- [ ] Hover detection works at all camera angles
- [ ] Hover detection reliable when axes are foreshortened
- [ ] Axis drag moves entity along correct world axis
- [ ] Center grab area works (or fails gracefully)
- [ ] No crashes when VP⁻¹ inversion fails (fallback to no-hover)
- [ ] Performance acceptable (no frame drops from raycasting)
- [ ] Threshold tuning feels right (not too sensitive, not too hard)

---

## Next Steps After This Plan

1. **Pattern-match to Rotate/Scale modes** (same raycasting logic)
2. **Implement center grab** (move entity freely on XY plane)
3. **Cache VP/VP⁻¹ in EditorContext** (performance optimization)
4. **Add Orthographic Camera for Mode2D/Isometric** (use Mat4::ortho)
5. **VP matrix cache & reuse** across grid, gizmo, mesh rendering
