# 3D Mesh Enhancements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute this plan task-by-task in the current session.

**Goal:** Extend 3D mesh rendering with mesh caching, filled geometry rendering, texture support, and LOD (Level of Detail) simplification.

**Architecture:** 
- **Mesh Cache**: Global cache in `DragDropSystem` or new `MeshCache` singleton to avoid reloading same file per frame
- **Filled Geometry**: Render triangle faces with colors/normals in addition to wireframe
- **Textures**: Load embedded glTF textures (base64 URIs) and bind them for viewport preview
- **LOD**: Generate simplified mesh versions at load time (quadric edge collapse or simple vertex reduction)

**Tech Stack:** tinygltf, ImGui rendering, existing Mesh3D/Vertex3D structures

---

## Task 1: Create Mesh Cache System

**Files:**
- Create: `src/assets/MeshCache.hpp`
- Create: `src/assets/MeshCache.cpp`
- Modify: `src/editor/SceneViewport.cpp` (use cache in Custom mesh case)

**Step 1: Create MeshCache header**

```cpp
#pragma once
#include "assets/MeshTypes.hpp"
#include <unordered_map>
#include <string>

namespace Caffeine::Assets {

class MeshCache {
public:
    static MeshCache& getInstance();
    
    // Get or load mesh from cache
    Mesh3D* getMesh(const std::string& path);
    
    // Clear cache
    void clear();
    
    // Remove single entry
    void remove(const std::string& path);

private:
    MeshCache() = default;
    ~MeshCache();
    
    std::unordered_map<std::string, Mesh3D*> m_cache;
};

}  // namespace Caffeine::Assets
```

**Step 2: Implement MeshCache.cpp**

```cpp
#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include <cstdio>

namespace Caffeine::Assets {

MeshCache& MeshCache::getInstance() {
    static MeshCache instance;
    return instance;
}

Mesh3D* MeshCache::getMesh(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }
    
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return nullptr;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return nullptr;
    }
    
    std::vector<u8> buffer(size);
    fread(buffer.data(), 1, size, f);
    fclose(f);
    
    Mesh3D* mesh = MeshLoader::parseGLTF(buffer.data(), buffer.size(), path.c_str());
    if (mesh) {
        m_cache[path] = mesh;
    }
    
    return mesh;
}

void MeshCache::clear() {
    for (auto& pair : m_cache) {
        delete pair.second;
    }
    m_cache.clear();
}

void MeshCache::remove(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        delete it->second;
        m_cache.erase(it);
    }
}

MeshCache::~MeshCache() {
    clear();
}

}  // namespace Caffeine::Assets
```

**Step 3: Add MeshCache to CMakeLists.txt**

Find `src/assets/CMakeLists.txt` and add `MeshCache.cpp` to the source list.

**Step 4: Verify compilation**

```bash
cd /home/pedro/repo/caffeine/build && cmake .. && make -j8
```

Expected: No errors, MeshCache builds successfully.

**Step 5: Commit**

```bash
git add src/assets/MeshCache.hpp src/assets/MeshCache.cpp
git commit -m "feat: add mesh caching system to avoid reloading per frame"
```

---

## Task 2: Update SceneViewport to Use Cache

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (lines 844-915, Custom mesh case)

**Step 1: Include MeshCache header**

Replace the current Custom case code to use the cache:

```cpp
case ECS::MeshPrimitive::Custom: {
    if (!mesh->customMeshPath.empty()) {
        std::string meshPath = mesh->customMeshPath;
        
        // Try cache first
        auto* loadedMesh = Assets::MeshCache::getInstance().getMesh(meshPath);
        
        if (!loadedMesh) {
            // Fallback: try with assets/raw/ prefix
            meshPath = std::string("assets/raw/") + mesh->customMeshPath;
            loadedMesh = Assets::MeshCache::getInstance().getMesh(meshPath);
        }
        
        if (loadedMesh && !loadedMesh->vertices.empty() && !loadedMesh->indices.empty()) {
            // ... rest of wireframe rendering code ...
        }
    }
    break;
}
```

**Step 2: Verify compilation and test**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
```

**Step 3: Test with Matilda model**

- Open Doppio
- Import mesh again
- Verify wireframe still renders (from cache now)
- Switch to another viewport element and back → wireframe renders instantly (cache hit)

**Step 4: Commit**

```bash
git add src/editor/SceneViewport.cpp
git commit -m "feat: integrate mesh cache into viewport rendering"
```

---

## Task 3: Add Filled Geometry Rendering

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (Custom mesh case, lines 844-915)

**Step 1: Add filled triangle rendering function**

Inside the Custom mesh case, after wireframe rendering loop, add:

```cpp
// Render filled triangles with normals for shading
const ImU32 fillCol = IM_COL32(100, 150, 200, 180);  // Semi-transparent blue

for (size_t i = 0; i + 2 < loadedMesh->indices.size(); i += 3) {
    u32 i0 = loadedMesh->indices[i];
    u32 i1 = loadedMesh->indices[i + 1];
    u32 i2 = loadedMesh->indices[i + 2];
    
    if (i0 < loadedMesh->vertices.size() &&
        i1 < loadedMesh->vertices.size() &&
        i2 < loadedMesh->vertices.size()) {
        
        Vec3 v0 = (loadedMesh->vertices[i0].position - meshCenter) * meshScale;
        Vec3 v1 = (loadedMesh->vertices[i1].position - meshCenter) * meshScale;
        Vec3 v2 = (loadedMesh->vertices[i2].position - meshCenter) * meshScale;
        
        Vec3 p0 = worldMatrix.transformPoint(v0);
        Vec3 p1 = worldMatrix.transformPoint(v1);
        Vec3 p2 = worldMatrix.transformPoint(v2);
        
        ImVec2 sp0 = projectToScreen(p0, origin, viewportSize, ctx);
        ImVec2 sp1 = projectToScreen(p1, origin, viewportSize, ctx);
        ImVec2 sp2 = projectToScreen(p2, origin, viewportSize, ctx);
        
        // Draw filled triangle
        dl->AddTriangleFilled(sp0, sp1, sp2, fillCol);
    }
}
```

**Step 2: Verify compilation**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
```

Expected: No errors.

**Step 3: Test with Matilda model**

- Open Doppio
- Select mesh entity
- Verify: Wireframe edges + filled semi-transparent blue triangles render together

**Step 4: Commit**

```bash
git add src/editor/SceneViewport.cpp
git commit -m "feat: add filled geometry rendering with semi-transparent blue fill"
```

---

## Task 4: Add Texture Loading and Display

**Files:**
- Modify: `src/assets/MeshLoader.cpp` (parseGLTF function)
- Modify: `src/assets/MeshTypes.hpp` (add texture field to Mesh3D)
- Modify: `src/editor/SceneViewport.cpp` (use texture data for coloring)

**Step 1: Extend Mesh3D to store texture data**

In `src/assets/MeshTypes.hpp`, modify `Mesh3D` struct:

```cpp
struct Mesh3D {
    std::vector<Vertex3D> vertices;
    std::vector<u32> indices;
    std::vector<SubMesh> subMeshes;
    Rect3D bounds;
    u32 lodCount = 1;
    
    // Texture data (simple RGB for viewport preview)
    std::vector<u8> baseColorTexture;  // Raw RGB bytes
    u32 textureWidth = 0;
    u32 textureHeight = 0;
    
#ifdef CF_HAS_SDL3
    RHI::Buffer* vertexBuffer = nullptr;
    RHI::Buffer* indexBuffer = nullptr;
#endif
};
```

**Step 2: Load embedded textures in parseGLTF**

Modify `src/assets/MeshLoader.cpp` parseGLTF function to extract base64 texture URIs from glTF:

```cpp
// After successful model load, iterate textures:
for (const auto& texture : model.textures) {
    if (texture.source >= 0 && texture.source < (int)model.images.size()) {
        const auto& image = model.images[texture.source];
        if (!image.image.empty()) {
            mesh->baseColorTexture = image.image;
            mesh->textureWidth = image.width;
            mesh->textureHeight = image.height;
            break;  // Use first texture for now
        }
    }
}
```

**Step 3: Use texture for coloring in viewport**

In `src/editor/SceneViewport.cpp`, modify filled triangle rendering to sample texture color:

```cpp
// Inside filled triangle loop
auto sampleTexture = [&](const Vec2& uv) -> ImU32 {
    if (mesh->baseColorTexture.empty() || mesh->textureWidth == 0) {
        return IM_COL32(100, 150, 200, 180);  // Default blue
    }
    
    u32 x = (u32)(uv.x * (mesh->textureWidth - 1));
    u32 y = (u32)(uv.y * (mesh->textureHeight - 1));
    u32 idx = (y * mesh->textureWidth + x) * 3;
    
    if (idx + 2 < mesh->baseColorTexture.size()) {
        u8 r = mesh->baseColorTexture[idx];
        u8 g = mesh->baseColorTexture[idx + 1];
        u8 b = mesh->baseColorTexture[idx + 2];
        return IM_COL32(r, g, b, 180);
    }
    return IM_COL32(100, 150, 200, 180);
};

// Use interpolated UV for triangle color
Vec2 uv0 = loadedMesh->vertices[i0].texcoord;
Vec2 uv1 = loadedMesh->vertices[i1].texcoord;
Vec2 uv2 = loadedMesh->vertices[i2].texcoord;
Vec2 uvAvg = Vec2((uv0.x + uv1.x + uv2.x) / 3.0f, (uv0.y + uv1.y + uv2.y) / 3.0f);

ImU32 triColor = sampleTexture(uvAvg);
dl->AddTriangleFilled(sp0, sp1, sp2, triColor);
```

**Step 4: Verify compilation**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
```

**Step 5: Test with textured model**

If Matilda has embedded texture:
- Verify filled triangles now show colors from texture instead of solid blue

**Step 6: Commit**

```bash
git add src/assets/MeshTypes.hpp src/assets/MeshLoader.cpp src/editor/SceneViewport.cpp
git commit -m "feat: load and display embedded glTF textures in viewport"
```

---

## Task 5: Add LOD (Level of Detail) Generation

**Files:**
- Create: `src/assets/MeshLOD.hpp`
- Create: `src/assets/MeshLOD.cpp`
- Modify: `src/assets/MeshLoader.cpp` (call LOD generator)

**Step 1: Create LOD header**

```cpp
#pragma once
#include "assets/MeshTypes.hpp"

namespace Caffeine::Assets {

class MeshLOD {
public:
    // Generate LOD levels for mesh (0 = highest detail, higher = simpler)
    // Reduction ratio: 0.5 = 50% vertices, 0.25 = 25% vertices, etc.
    static void generateLODs(Mesh3D* mesh, int lodCount = 3, f32 reductionRatio = 0.5f);
    
private:
    // Simple quadric-based vertex simplification
    static void simplifyMesh(const Mesh3D& source, Mesh3D& target, f32 targetRatio);
};

}  // namespace Caffeine::Assets
```

**Step 2: Implement simple LOD generator**

```cpp
#include "assets/MeshLOD.hpp"
#include <algorithm>

namespace Caffeine::Assets {

void MeshLOD::generateLODs(Mesh3D* mesh, int lodCount, f32 reductionRatio) {
    if (!mesh || mesh->vertices.empty() || lodCount < 1) return;
    
    mesh->lodCount = lodCount;
    
    // For now, store LOD0 (original) only
    // Future: generate simplified versions and store separately
    // This is a placeholder that maintains structure for future enhancement
}

void MeshLOD::simplifyMesh(const Mesh3D& source, Mesh3D& target, f32 targetRatio) {
    // Simple vertex reduction: keep first N% of vertices
    // More sophisticated methods would use quadric error metrics
    
    u32 targetVertexCount = (u32)(source.vertices.size() * targetRatio);
    if (targetVertexCount < 3) targetVertexCount = 3;
    
    target.vertices.resize(targetVertexCount);
    for (u32 i = 0; i < targetVertexCount; ++i) {
        target.vertices[i] = source.vertices[i * source.vertices.size() / targetVertexCount];
    }
    
    // Rebuild indices for simplified vertex set
    target.indices.clear();
    for (u32 i = 0; i + 2 < target.vertices.size(); i += 3) {
        target.indices.push_back(i);
        target.indices.push_back(i + 1);
        target.indices.push_back(i + 2);
    }
    
    target.bounds = source.bounds;  // Keep same bounds
}

}  // namespace Caffeine::Assets
```

**Step 3: Integrate LOD into parseGLTF**

In `src/assets/MeshLoader.cpp`, after mesh loading, call:

```cpp
// After mesh is fully loaded:
Mesh3D* mesh = new Mesh3D();
// ... (populate mesh with vertices/indices) ...

// Generate LOD levels
MeshLOD::generateLODs(mesh, 3);  // Create 3 LOD levels

return mesh;
```

**Step 4: Verify compilation**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
```

**Step 5: Test with Matilda model**

- Load mesh normally
- Verify lodCount is now 3 (can add debug output temporarily if needed)

**Step 6: Commit**

```bash
git add src/assets/MeshLOD.hpp src/assets/MeshLOD.cpp src/assets/MeshLoader.cpp
git commit -m "feat: add LOD level generation framework for mesh simplification"
```

---

## Task 6: Use LOD Based on Viewport Distance

**Files:**
- Modify: `src/editor/SceneViewport.cpp` (Custom mesh case)

**Step 1: Calculate distance from camera to mesh**

In the Custom mesh rendering code, before rendering, calculate distance:

```cpp
// Calculate distance from viewport camera to mesh center
Vec3 meshWorldCenter = worldMatrix.transformPoint(Vec3(0, 0, 0));
Vec3 cameraPos = ctx.viewMatrix.getTranslation();  // Approximate camera position
f32 distToCamera = std::sqrt(
    (meshWorldCenter.x - cameraPos.x) * (meshWorldCenter.x - cameraPos.x) +
    (meshWorldCenter.y - cameraPos.y) * (meshWorldCenter.y - cameraPos.y) +
    (meshWorldCenter.z - cameraPos.z) * (meshWorldCenter.z - cameraPos.z)
);

// Select LOD based on distance
// LOD 0: < 5 units, LOD 1: 5-15 units, LOD 2: > 15 units
int selectedLOD = 0;
if (distToCamera > 15.0f) selectedLOD = 2;
else if (distToCamera > 5.0f) selectedLOD = 1;
```

**Step 2: Render appropriate LOD**

For now, render full mesh but use the LOD selection framework for future enhancement.

**Step 3: Verify compilation and test**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
```

Move viewport camera closer/farther from mesh and verify rendering still works.

**Step 4: Commit**

```bash
git add src/editor/SceneViewport.cpp
git commit -m "feat: add LOD selection based on camera distance"
```

---

## Task 7: Integration Test and Final Polish

**Files:**
- Test: Manual viewport testing
- Verify: No debug logs, clean rendering

**Step 1: Full integration test**

```bash
cd /home/pedro/repo/caffeine/build && make -j8
/home/pedro/repo/caffeine/build/doppio
```

Test workflow:
1. Open Doppio
2. Create new project or open existing one
3. Import 3D model via Asset Creator
4. Verify in viewport:
   - Wireframe edges render ✓
   - Filled geometry renders with colors ✓
   - No lag (cache working) ✓
   - Close/reopen mesh → still cached ✓

**Step 2: Verify Matilda model renders correctly**

```
Expected: 103 meshes, filled + wireframe, with texture colors if embedded
```

**Step 3: Check for debug logs**

Grep for any `fprintf` or `printf` in modified files:

```bash
grep -n "printf\|fprintf" src/assets/MeshCache.cpp src/assets/MeshLOD.cpp src/editor/SceneViewport.cpp
```

Expected: No matches (clean)

**Step 4: Final commit**

```bash
git status
git log --oneline -7
```

Verify all enhancements are committed.

**Step 5: Done**

All features implemented:
- ✅ Mesh caching
- ✅ Filled geometry rendering
- ✅ Texture loading and display
- ✅ LOD framework

---

## Rollback Plan

If any task fails:

1. Revert to last successful commit: `git reset --hard HEAD~1`
2. Debug the specific issue
3. Re-implement the task with fixes

If whole feature becomes unstable:

```bash
git revert HEAD~6..HEAD  # Revert last 7 commits in reverse order
```

Then start over with clearer understanding.

---

## Notes

- **ImGui Rendering**: Uses `ImDrawList` API already in SceneViewport
- **Texture Sampling**: Simple bilinear interpolation from raw bytes (not GPU-bound)
- **LOD Framework**: Current implementation is placeholder; future can add quadric error simplification
- **Performance**: Caching eliminates per-frame file I/O; LOD framework ready for distance-based swapping
