# Caffeine Engine - Current Status & Implementation Roadmap

**Branch**: `121-44-audio-preview-spatial-placement`
**Date**: 2026-05-18

---

## 1. BUILD & IDE STATUS ✅

### Completed
- ✅ doppio.exe fully builds and launches (1.2 MB)
- ✅ All executables built: doppio, caf-encode, caffeine-combined
- ✅ SDL3 integrated and working
- ✅ Clean default UI layout (Hierarchy + Viewport only)
- ✅ File picker works for project creation
- ✅ Project manager operational

### Known Issues to Fix
- ⚠️ FilePicker window requires scrolling (fixed window size ~600x400)
  - **Fix**: Make window resizable and increase default size
  - **File**: `src/editor/FilePicker.cpp` line 96-97

---

## 2. ARCHITECTURE OVERVIEW

### ECS (Entity Component System)
**Status**: ✅ Fully implemented and working

**Core Files**:
- `src/ecs/Entity.hpp` - Entity definition
- `src/ecs/World.hpp` - World/scene management
- `src/ecs/Components.hpp` - Basic components (Position2D, Velocity2D, Rotation, Scale2D, Sprite, Health, Tag, ParticleEmitterComponent)
- `src/ecs/ComponentID.hpp` - Component type system
- `src/ecs/ComponentQuery.hpp` - Entity querying

**Current Components Available**:
1. **Transform**: Position2D, Velocity2D, Acceleration2D, Rotation, Scale2D
2. **Visual**: Sprite (name, frameIndex only - minimal)
3. **Audio**: AudioComponents.hpp (exists but not integrated)
4. **Animation**: AnimationComponents.hpp (exists)
5. **Camera**: CameraComponents.hpp
6. **Light**: LightComponents.hpp
7. **Particle**: ParticleEmitterComponent
8. **Mesh**: MeshComponents.hpp (3D, not priority for 2D)
9. **Health/Tag**: For basic game mechanics

---

## 3. FEATURE GAPS & PRIORITIES

### 🔴 CRITICAL (Blocking Game Development)

#### 1. **Inspector Panel - Minimal (15-20% complete)**
   - **Current State**:
     - Only 6 component drawers implemented (Transform, Sprite, Camera, RigidBody2D, AudioSource, Script)
     - Sprite drawer is basic: just name + frameIndex
     - No property discovery/reflection system
     - Hard-coded drawer registration
   
   - **What's Missing**:
     - Dynamic component property inspection
     - Proper enum selection UI (e.g., for ColliderShape, ToolMode)
     - Vector/color pickers
     - Asset reference selectors
     - Dropdown selectors for components
     - Undo/redo support
   
   - **File**: `src/editor/InspectorPanel.hpp/cpp`
   - **Priority**: 🔴 CRITICAL - Can't edit entities without this
   - **Effort**: ~3-5 days

#### 2. **Physics System - Incomplete (30% implemented)**
   - **Current State**:
     - Components exist: RigidBody2D, Collider2D, PhysicsMaterial
     - PhysicsSystem2D has 700+ lines of implementation
     - Collision detection logic exists
     - Force/impulse system exists
   
   - **What's Missing**:
     - Physics simulation not hooked into editor/game loop properly
     - Collision callbacks not firing (callOnCollision exists but untested)
     - No visual debugging (collision visualizers)
     - No physics settings UI in editor
     - Sleep optimization incomplete
   
   - **Files**: `src/physics/PhysicsComponents2D.hpp`, `src/physics/PhysicsSystem2D.hpp`
   - **Priority**: 🔴 CRITICAL - No game without physics
   - **Effort**: ~2-3 days to integrate properly

#### 3. **Scripting System - Scaffolding only (5% usable)**
   - **Current State**:
     - ScriptEngine exists with lifecycle hooks: onCreate, onUpdate, onDestroy, onCollision
     - ScriptEngine can load scripts and call them
     - Script watcher for hot-reload
   
   - **What's Missing**:
     - **No script binding** - engine classes not exposed to scripts
     - **No runtime** - what language? (Lua? C#? Custom?)
     - **No entity access** - scripts can't modify entities
     - **No API documentation**
     - Script component UI in inspector
   
   - **Files**: `src/script/ScriptEngine.hpp/cpp`, `src/script/ScriptSystem.cpp`
   - **Priority**: 🔴 CRITICAL - Game logic impossible without this
   - **Effort**: ~5-7 days (depends on language choice)

#### 4. **2D Tools - Partial (40% usable)**
   - **Tilemap Editor**:
     - ✅ Brush, Bucket, Eraser, Picker tools defined
     - ✅ TileLayer and Tilemap classes exist
     - ❌ No tileset loading/display
     - ❌ No rendering
     - ❌ No grid visualization
     - ❌ No tool UI integration
   
   - **Sprite Handling**:
     - ✅ Sprite component exists
     - ❌ No sprite atlas/sheet support
     - ❌ No frame animation UI
     - ❌ No sprite preview in inspector
   
   - **Files**: `src/editor/TilemapEditor.hpp/cpp`
   - **Priority**: 🔴 CRITICAL for 2D - Can't make 2D games without tilesets
   - **Effort**: ~4-6 days

---

### 🟡 HIGH (Important for full feature set)

#### 5. **Component Registration/Discovery**
   - **Current State**: Components are discoverable via ECS but Inspector doesn't auto-discover them
   - **Need**: Reflection/metadata system so inspector can show all component properties
   - **Priority**: HIGH - Unlocks dynamic UI generation
   - **Effort**: ~2-3 days

#### 6. **Asset Pipeline**
   - **Current State**: Basic asset browser exists
   - **Missing**: 
     - Texture import/settings
     - Sprite atlas creation
     - Tileset definition files
     - Audio asset metadata
   - **Priority**: HIGH
   - **Effort**: ~3-5 days

#### 7. **Scene Serialization**
   - **Current State**: Likely partial
   - **Missing**: Save/load entity hierarchies with full state
   - **Priority**: HIGH
   - **Effort**: ~2-3 days

---

### 🟢 MEDIUM (Nice to have)

#### 8. **Particle System Integration**
   - ParticleEmitterComponent exists but likely not wired to renderer
   - **Effort**: ~1-2 days

#### 9. **Animation System**
   - AnimationComponents exist
   - **Effort**: ~2-3 days

#### 10. **3D Support**
   - Components3D.hpp exists
   - **Effort**: Defer - focus on 2D first

---

## 4. IMMEDIATE NEXT STEPS (Priority Order)

### Week 1:
1. **Fix FilePicker window sizing** (1 hour)
   - Make resizable, increase default size to 800x600
   
2. **Expand Inspector with property types** (2-3 days)
   - Add UI for all basic property types (float, int, bool, Vec2, color, enum)
   - Test with Transform and Sprite components
   - Add component add/remove UI
   
3. **Integrate Physics visually** (2 days)
   - Hook PhysicsSystem2D into scene editor
   - Add collision debug visualization
   - Test RigidBody2D + Collider2D on entities

### Week 2:
4. **Basic Scripting Integration** (3-5 days)
   - Choose scripting language (recommend: **Lua** for simplicity, or **C#** if .NET available)
   - Create bindings for basic engine functions
   - Allow attaching scripts to entities
   - Test onCreate/onUpdate lifecycle

5. **2D Editor Tools** (4-6 days)
   - Implement tileset editor
   - Implement tilemap painter
   - Grid visualization
   - Basic rendering integration

---

## 5. REFERENCE ARCHITECTURE (Unity-inspired)

### Inspector (Property Editor)
```
Entity: "Player"
├─ Transform
│  ├─ Position: (0, 0) [Vec2Drawer]
│  ├─ Rotation: 45° [SliderDrawer]
│  └─ Scale: (1, 1) [Vec2Drawer]
├─ Sprite  
│  ├─ Texture: [AssetSelector]
│  ├─ Frame: 0 [IntDrawer]
│  └─ Color: white [ColorDrawer]
├─ RigidBody2D
│  ├─ Mass: 1.0 [FloatDrawer]
│  ├─ Gravity Scale: 1.0 [FloatDrawer]
│  ├─ Constraints: [FlagDrawer]
│  └─ Collision Matrix [LayerMaskDrawer]
├─ Collider2D
│  ├─ Shape: AABB [EnumDrawer]
│  ├─ Size: (32, 32) [Vec2Drawer]
│  ├─ Layer: 0 [LayerDrawer]
│  └─ Is Trigger: false [BoolDrawer]
└─ [+ Add Component...]
```

### ScriptAPI (example Lua)
```lua
-- Player.lua
entity:getComponent("RigidBody2D").velocity = {100, 0}
entity:getComponent("Sprite").frameIndex = 1
input.isKeyDown("w") -- true/false
```

---

## 6. FILE CHECKLIST

### Core Systems (Check/expand these)
- [ ] `src/ecs/Components.hpp` - Add missing component types
- [ ] `src/editor/InspectorPanel.cpp` - Expand with property drawers
- [ ] `src/physics/PhysicsSystem2D.hpp` - Wire to main game loop
- [ ] `src/script/ScriptEngine.cpp` - Add engine bindings
- [ ] `src/editor/TilemapEditor.cpp` - Implement rendering
- [ ] `src/editor/FilePicker.cpp` - Fix window sizing

### Next Exploration
- [ ] How scenes are serialized (look for SceneSerializer)
- [ ] How assets are loaded (look in `src/assets/`)
- [ ] Render pipeline (look in `src/render/`)
- [ ] How systems integrate into main game loop (look in `src/engine/`)

---

## 7. BUILD & TESTING

### Current Build Status
```bash
cd build && cmake --build . --config Release
# doppio.exe ready in build/Release/

# Test game creation:
./build/Release/doppio.exe
# → Create project → Open scene → Edit entities
```

### What to Test Next
1. Create new 2D project
2. Add entity with Sprite + RigidBody2D + Collider2D
3. Verify inspector shows all properties
4. Save/load scene
5. Run game with physics

---

## SUMMARY

**Caffeine Engine is a solid skeleton** with ECS, physics, scripting, and 2D tools scaffolding in place. The critical path to usable game development is:

1. **Inspector expansion** (15-20 hours) - Make it show/edit all component properties
2. **Physics integration** (10-15 hours) - Hook to game loop, add debug visualization
3. **Scripting bindings** (20-30 hours) - Expose engine API to script language
4. **2D tools completion** (25-35 hours) - Tileset editor, tilemap painter
5. **Testing & bug fixes** (ongoing)

**Realistic timeline**: 3-4 weeks for a usable 2D indie game editor at Unity-lite feature level.
