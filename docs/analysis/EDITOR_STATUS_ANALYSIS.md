# Doppio Editor — Component Status Analysis

**Date**: 2026-05-16  
**Analyzer**: Sisyphus  
**Status**: Complete investigation with actionable findings

---

## Executive Summary

The Doppio editor is **well-architected but has a critical workflow gap**:

- ✅ **8/12 components are fully functional**
- ⚠️ **4 components are blocked by missing integrations** (not broken)
- ❌ **Critical blocker**: No Project Manager UI → editor starts with hardcoded "Untitled" scene
- **User's main complaint** ("Asset Browser broken, blocks other editors") is **incorrect**:
  - Asset Browser is fully functional
  - Problem: 4 editors need drag-drop event handlers to connect to Asset Browser

---

## The Real Problems (in priority order)

### 🔴 CRITICAL: Scene Startup Flow Missing

**What happens now**:
```
main.cpp
  → RenderDevice + AssetManager init
  → SceneEditor.init()
    → m_tabManager.newScene("Untitled")  ← Hardcoded, no project!
    → render()
```

**What should happen**:
```
main.cpp
  → ProjectManager UI Dialog
    ├─ [Create New Project]
    ├─ [Open Recent Project]
    └─ [Browse for Project]
  → Load ProjectConfig
  → SceneEditor.init(config)
    → Load last scene from config.LastScene
```

**Impact**: Users cannot create/manage projects; cannot switch projects without restarting

**Root Cause**: ProjectManager code exists (ProjectManager.cpp), but:
- No UI dialog implementation
- Not wired into SceneEditor.init()
- No startup flow in main.cpp

**Effort to Fix**: ~3-4 hours (create ProjectStartupDialog, wire into main.cpp)

---

### 🟠 HIGH: Editors disconnected from Asset Browser

User reported 4 editors as "broken":

| Editor | Actual Status | Real Issue | Fix |
|--------|---------------|-----------|-----|
| **Script Editor** | Data layer ✅ | No drag-drop handler for .lua | Add `ImGui::AcceptDragDropPayload("ASSET_PATH")` ~15 min |
| **Audio Preview** | Playback ✅ | No drag-drop handler for .wav/.ogg | Add payload handler ~15 min |
| **Animation Timeline** | Keyframe system ✅ | render() missing delta-time param | Update signature ~30 min |
| **Tilemap Editor** | Cell data ✅ | Only shows tile IDs, no visual grid | Implement grid canvas ~2 hours |

**Why they appear broken**:
- User tries to drag .lua file from AssetBrowser to ScriptEditor → nothing happens
- Asset drop event never reaches the editor because it has no handler
- **Not a bug in Asset Browser, but missing integration in each editor**

**Evidence**:
- `AssetBrowser::getDroppedAsset()` implemented (line 414 of AssetBrowser.cpp)
- `ImGui::AcceptDragDropPayload()` used elsewhere:
  - HierarchyPanel.cpp line 146 (for entity drag)
  - InspectorPanel.cpp line 295 (for asset path)
- SceneEditor.cpp line 468: `auto dropped = m_assetBrowser.getDroppedAsset();` ← Works here!

**So why not in Script Editor?** It just wasn't done.

---

### 🟡 MEDIUM: Partially-implemented editors

#### Animation Timeline
- **What works**: Keyframe data structures, play/stop logic, easing functions
- **What's blocked**: 
  - `render()` has no delta-time parameter
  - Timeline playback needs `m_currentTime += deltaTime` in render loop
  - TODO comment on line 55 explains it
- **Fix**: Pass `f32 deltaTime` through render signature chain

#### Tilemap Editor
- **What works**: Layer management, tile cell storage, auto-tiling rules
- **What's blocked**: Visual representation
  - Currently: Renders tile IDs as numbers in grid layout
  - Needed: ImGui child windows with tile visualization, drag-select, paint tools
- **Fix**: Implement visual grid canvas (~200 lines of ImGui)

---

## Component Detail Report

### ✅ FULLY FUNCTIONAL (8 components)

| Component | File | Lines | Capability |
|-----------|------|-------|-----------|
| **AssetBrowser** | AssetBrowser.cpp | 428 | File browsing, search, filter, thumbnails, drag-drop |
| **HierarchyPanel** | HierarchyPanel.cpp | 250+ | Entity tree, parent-child, drag-reorder |
| **InspectorPanel** | InspectorPanel.cpp | 400+ | Component editor, property serialization |
| **SceneViewport** | SceneViewport.cpp | 300+ | 2D/3D rendering, camera, transform gizmos |
| **ConsoleWindow** | ConsoleWindow.hpp (inline) | ~100 | Log display, error output |
| **ProfilerWindow** | ProfilerWindow.hpp (inline) | ~100 | Frame stats, perf metrics |
| **CommandPalette** | CommandPalette.cpp | 350+ | Keyboard command search & dispatch |
| **SceneTabManager** | SceneTabManager.cpp | 200+ | Multi-tab scenes, active scene switching |

---

### ⚠️ PARTIALLY FUNCTIONAL (4 components)

#### ScriptEditorWindow
```cpp
// File: src/editor/ScriptEditorWindow.cpp (148 lines)

// ✅ Works:
- openFile(path)      // Load .lua from disk
- saveFile(index)     // Save to disk
- render()            // Tab UI with text area

// ❌ Missing:
- Drag-drop from AssetBrowser
- Syntax highlighting (TextEditor integration)
- Script execution/debugging
```

**Fix Priority**: HIGH (1-2 hours)  
**Effort**: Add payload handler in `render()`:
```cpp
if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
        const char* path = (const char*)payload->Data;
        openFile(path);
    }
    ImGui::EndDragDropTarget();
}
```

---

#### AudioPreviewPanel
```cpp
// File: src/editor/AudioPreviewPanel.cpp (237 lines)

// ✅ Works:
- init()              // Create AudioSystem
- loadAsset(clip)     // Load AudioClip*
- play(), stop()      // Playback control
- onImGuiRender()     // UI with waveform

// ❌ Missing:
- Drag-drop asset loading
- Waveform visualization
```

**Fix Priority**: HIGH (1 hour)  
**Effort**: Same as ScriptEditor — add drag-drop handler

---

#### AnimationTimeline
```cpp
// File: src/editor/AnimationTimeline.cpp (283 lines)

// ✅ Works:
- SpriteTrack, TransformTrack, EventTrack classes
- addKeyframe(), removeKeyframe()
- applyEasing(), interpolateValue()
- Keyframe data storage

// ❌ Missing:
- render() signature missing delta-time parameter
  Line 55: TODO comment explains the blocker
- Timeline UI interaction (click to add keyframes)
```

**Fix Priority**: MEDIUM (1.5 hours)

**Requirement**: Update render signature everywhere:
```cpp
// Old:
void AnimationTimelinePanel::render();

// New:
void AnimationTimelinePanel::render(f32 deltaTime);

// Then in SceneEditor.render():
m_animationTimeline.render(deltaTime);  // Need to calculate deltaTime
```

---

#### TilemapEditor
```cpp
// File: src/editor/TilemapEditor.cpp (280 lines)

// ✅ Works:
- TileLayer class (cell storage, resize)
- Tilemap class (layer management)
- paintTile(), applyAutoTile()
- Neighbor detection & auto-tiling rules

// ❌ Missing:
- Visual grid rendering (just shows IDs)
- Mouse interaction (paint, select, erase)
- Tile palette UI
```

**Fix Priority**: MEDIUM (3-4 hours)

**Current output**:
```
render() {
    ImGui::Text("Layer %d", currentLayer);
    ImGui::Text("Tile IDs: %d %d %d ...");
}
```

**Needed output**:
- Visual grid with tile graphics
- Selectable tiles (highlight on hover)
- Paint tool, erase tool
- Layer visibility toggle

---

### ❌ STUB / MISSING (3 components)

#### Material Editor
```cpp
// File: src/editor/MaterialEditorPanel.cpp

// Status: STUB
- onImGuiRender() exists but does nothing
- ShaderGraph.cpp has data structures but no UI rendering
- No visual shader graph editor

// Impact: Cannot edit materials; shaders are code-only
```

#### ProjectManager (UI)
```cpp
// File: src/editor/ProjectManager.cpp

// Status: IMPLEMENTATION exists, NO UI
- CreateNewProject() ✅ (functional)
- OpenProject() ✅ (functional)
- SaveProjectFile() ✅ (functional)
- LoadProjectFile() ✅ (functional)

// Missing:
- ProjectStartupDialog (ImGui)
- Integration into SceneEditor.init()
- Recent projects UI
```

#### Scene Startup Flow
```cpp
// File: apps/doppio/main.cpp

// Current (line 56-64):
Caffeine::Editor::SceneEditor editor;
if (!editor.init(&device, &assetManager)) {
    // Fails: no project context, hardcoded to "Untitled"
}

// Needed:
// 1. Show ProjectManager UI first
// 2. Load ProjectConfig
// 3. Pass config to SceneEditor.init()
// 4. Load project's last scene
```

---

## Architecture Assessment

### Strengths
✅ **Clear separation**: Data layer (always compiled) / UI layer (CF_HAS_IMGUI)  
✅ **Component cohesion**: Each editor is self-contained  
✅ **Drag-drop system**: DragDropSystem.cpp provides infrastructure  
✅ **Tab management**: SceneTabManager handles multi-scene workflow  
✅ **Serialization**: SceneSerializer for save/load  

### Weaknesses
❌ **Missing startup workflow**: No project initialization UI  
❌ **Incomplete integrations**: Editors don't consume AssetBrowser drops  
❌ **No delta-time propagation**: Animation timeline blocked  
❌ **Stub components**: Material editor + Shader graph unfinished  

---

## Actionable Fixes (Priority-Ordered)

### 🔴 CRITICAL (Fix first — blocks workflow)

**1. Create ProjectStartupDialog** (2 hours)
- New file: `src/editor/ProjectStartupDialog.hpp/cpp`
- Show "New Project", "Open Recent", "Browse..." buttons
- Return selected ProjectConfig to main.cpp
- Integrate into SceneEditor initialization

**2. Wire ProjectManager into SceneEditor** (1 hour)
- Update `SceneEditor::init()` to accept `const ProjectConfig&`
- Pass asset paths from config to AssetBrowser.init()
- Load last scene from config instead of hardcoded "Untitled"

**3. Update main.cpp startup flow** (0.5 hour)
- Show ProjectStartupDialog before SceneEditor.init()
- Pass returned ProjectConfig to editor

### 🟠 HIGH (Fix next — unblock dependent work)

**4. Add drag-drop to ScriptEditor** (0.5 hour)
- Add `ImGui::AcceptDragDropPayload("ASSET_PATH")` in render()
- Call `openFile(path)` on drop

**5. Add drag-drop to AudioPreviewPanel** (0.5 hour)
- Same pattern as ScriptEditor
- Call `loadAsset(path)` on drop

### 🟡 MEDIUM (Fix when ready — complete features)

**6. Update AnimationTimeline render signature** (1 hour)
- Add `f32 deltaTime` parameter
- Update all call sites in SceneEditor
- Implement playback advancement

**7. Implement Tilemap visual grid** (3 hours)
- Create child window for tile grid
- Render tiles with visual appearance
- Add paint/select tools

### 🟢 LOW (Nice-to-have)

**8. Implement Material Editor / ShaderGraph UI** (4+ hours)
- Visual shader graph node editor
- Connection UI
- Shader preview

---

## File Locations Quick Reference

```
src/editor/
├── AssetBrowser.{hpp,cpp}          ✅ Complete
├── SceneEditor.{hpp,cpp}           ⚠️ Missing startup
├── ProjectManager.{hpp,cpp}        ⚠️ Code OK, no UI
├── ScriptEditorWindow.{hpp,cpp}    ⚠️ Missing drag-drop
├── AudioPreviewPanel.{hpp,cpp}     ⚠️ Missing drag-drop
├── AnimationTimeline.{hpp,cpp}     ⚠️ Missing delta-time
├── TilemapEditor.{hpp,cpp}         ⚠️ Missing visual grid
├── MaterialEditorPanel.{hpp,cpp}   ❌ Stub only
└── [6 more fully functional]

apps/doppio/
└── main.cpp                         ⚠️ Missing ProjectManager UI init
```

---

## Conclusion

**The user's diagnosis was incomplete.** Asset Browser is not broken; the workflow is broken. The fixes are straightforward integration work:

1. **High-impact** (2-4 hours): Create project startup dialog, wire integrations
2. **High-value** (2 hours): Add drag-drop handlers to 2 editors
3. **Medium-value** (4 hours): Fix animation timeline, tilemap grid

Once these are done, the editor becomes fully functional for 2D game development with scenes, assets, scripts, and animation.
