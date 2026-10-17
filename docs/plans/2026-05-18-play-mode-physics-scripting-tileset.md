# Play Mode + Physics + Scripting + Tileset Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Wire the already-implemented PhysicsSystem2D and ScriptEngine/Lua into the SceneEditor via a Play Mode, and add texture-based tileset loading to TilemapEditorPanel.

**Architecture:** Play Mode adds ▶/⏸/⏹ buttons to the SceneEditor toolbar. On Play, it snapshots entity state, then ticks PhysicsSystem2D and ScriptSystem each frame. On Stop, it restores the snapshot. Physics colliders are drawn as debug overlays in the viewport. The Tilemap palette is extended with a tileset texture loader that slices tiles by pixel and renders them as ImGui image buttons.

**Tech Stack:** C++20, ImGui, SDL3 (texture loading), sol2/Lua (already linked), PhysicsSystem2D (header-only), ScriptEngine/ScriptSystem (already compiled into caffeine-core)

---

## Task 1: Enable Scripting in the doppio/editor build

**Files:**
- Modify: `CMakeLists.txt` (around line 19)

**Context:** `CAFFEINE_ENABLE_SCRIPTING` is `OFF` by default. The editor needs it ON. The CMake flag gates the sol2/lua54 fetch and links them to `caffeine-core`. We need it ON for the doppio target.

**Step 1: Change the default to ON**

In `CMakeLists.txt`, change:
```cmake
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" OFF)
```
to:
```cmake
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" ON)
```

**Step 2: Add `CF_HAS_SCRIPTING` compile definition where scripting is enabled**

Find the `if(CAFFEINE_ENABLE_SCRIPTING)` block (around line 125) and ensure this line exists inside it:
```cmake
target_compile_definitions(caffeine-core PUBLIC CF_HAS_SCRIPTING)
```
This lets headers gate code with `#ifdef CF_HAS_SCRIPTING`.

**Step 3: Build to verify no compile errors**

```bash
cd "C:/Users/Pedro Jesus/Downloads/caffeine/build"
cmake .. -DCAFFEINE_ENABLE_SCRIPTING=ON
cmake --build . --config Release --target doppio 2>&1 | tail -20
```
Expected: build succeeds, `doppio.exe` produced.

**Step 4: Commit**
```bash
git add CMakeLists.txt
git commit -m "feat: enable Lua scripting by default for editor build"
```

---

## Task 2: Add Play Mode state + entity snapshot to SceneEditor

**Files:**
- Modify: `src/editor/SceneEditor.hpp`
- Modify: `src/editor/SceneEditor.cpp`

**Context:** We need three new state flags and an entity snapshot mechanism. The snapshot saves the `Position2D`, `Velocity2D`, and `Rotation` of all entities before play, and restores on stop. This matches Unity's behavior — edits in play mode are discarded.

**Step 1: Add includes + members to SceneEditor.hpp**

In `SceneEditor.hpp`, after the existing `#include` block (before `namespace Caffeine::Editor`), add:
```cpp
#include "physics/PhysicsSystem2D.hpp"
#include "events/EventBus.hpp"

#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
#include "script/ScriptSystem.hpp"
#endif
```

In the `private:` section of `SceneEditor`, after the existing members, add:
```cpp
    // ── Play Mode ──────────────────────────────────────────────
    bool m_isPlaying  = false;
    bool m_isPaused   = false;

    Events::EventBus m_eventBus;
    Physics2D::PhysicsSystem2D m_physicsSystem{&m_eventBus};

#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine m_scriptEngine;
    Script::ScriptSystem m_scriptSystem{nullptr};  // set in init
    bool m_scriptEngineReady = false;
#endif

    // Snapshot for restoring entity state on Stop
    struct EntitySnapshot {
        u32 id;
        float px = 0, py = 0;
        float vx = 0, vy = 0;
        float rotation = 0;
    };
    std::vector<EntitySnapshot> m_playSnapshot;
```

Also add these private method declarations:
```cpp
    void enterPlayMode(ECS::World& world);
    void exitPlayMode(ECS::World& world);
    void tickSystems(ECS::World& world, f32 dt);
    void renderPlaybar(ECS::World& world);
    void drawPhysicsDebug(ECS::World& world, ImVec2 origin, ImVec2 viewportSize);
```

**Step 2: Commit (header only)**
```bash
git add src/editor/SceneEditor.hpp
git commit -m "feat(editor): add play mode state and system members to SceneEditor"
```

---

## Task 3: Implement Play Mode logic in SceneEditor.cpp

**Files:**
- Modify: `src/editor/SceneEditor.cpp`

**Context:** `render(f32 deltaTime)` is called every frame. When playing, we tick physics and scripting here. `enterPlayMode` snapshots entity positions; `exitPlayMode` restores them.

**Step 1: Add includes at top of SceneEditor.cpp**

After `#include "editor/SceneEditor.hpp"`, add:
```cpp
#include "ecs/Components.hpp"
#include "physics/PhysicsComponents2D.hpp"
```

**Step 2: Implement `enterPlayMode`**

Add this function before the `render` method:
```cpp
void SceneEditor::enterPlayMode(ECS::World& world) {
    m_playSnapshot.clear();

    ECS::ComponentQuery q;
    world.forEach<ECS::Position2D>(q,
        [&](ECS::Entity e, ECS::Position2D& pos) {
            EntitySnapshot snap;
            snap.id = e.id();
            snap.px = pos.x; snap.py = pos.y;
            if (auto* v = world.get<ECS::Velocity2D>(e)) { snap.vx = v->x; snap.vy = v->y; }
            if (auto* r = world.get<ECS::Rotation>(e))   { snap.rotation = r->angle; }
            m_playSnapshot.push_back(snap);
        });

    m_isPlaying = true;
    m_isPaused  = false;

#ifdef CF_HAS_SCRIPTING
    if (!m_scriptEngineReady) {
        Script::ScriptEngine::InitParams p;
        p.world  = &world;
        p.events = &m_eventBus;
        m_scriptEngineReady = m_scriptEngine.init(p);
        m_scriptSystem = Script::ScriptSystem(&m_scriptEngine);
    }
#endif
}
```

**Step 3: Implement `exitPlayMode`**

```cpp
void SceneEditor::exitPlayMode(ECS::World& world) {
    m_isPlaying = false;
    m_isPaused  = false;

    for (auto& snap : m_playSnapshot) {
        ECS::Entity e(snap.id, &world);
        if (!e.isValid()) continue;
        if (auto* pos = world.get<ECS::Position2D>(e)) { pos->x = snap.px; pos->y = snap.py; }
        if (auto* v   = world.get<ECS::Velocity2D>(e)) { v->x = snap.vx;  v->y = snap.vy;  }
        if (auto* r   = world.get<ECS::Rotation>(e))   { r->angle = snap.rotation; }
    }
    m_playSnapshot.clear();
}
```

**Step 4: Implement `tickSystems`**

```cpp
void SceneEditor::tickSystems(ECS::World& world, f32 dt) {
    if (!m_isPlaying || m_isPaused) return;
    m_physicsSystem.onUpdate(world, dt);
#ifdef CF_HAS_SCRIPTING
    if (m_scriptEngineReady) m_scriptSystem.onUpdate(world, dt);
#endif
    m_eventBus.dispatchDeferred();
}
```

**Step 5: Implement `renderPlaybar`**

This renders ▶/⏸/⏹ buttons in a small centered toolbar. Find where to insert it: in `render()`, right before the call to `m_viewport.render(...)`. Add a call to `renderPlaybar(*activeWorld)` there.

```cpp
void SceneEditor::renderPlaybar(ECS::World& world) {
    // Render a small floating toolbar above the viewport
    ImVec2 vpPos  = ImGui::GetCursorScreenPos();
    ImVec2 vpSize = ImGui::GetContentRegionAvail();
    float barW = 120.0f, barH = 28.0f;
    ImVec2 barPos(vpPos.x + (vpSize.x - barW) * 0.5f, vpPos.y + 4.0f);

    ImGui::SetNextWindowPos(barPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barW, barH));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    // We want input, so remove NoInputs:
    flags &= ~ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##PlayBar", nullptr, flags)) {
        if (!m_isPlaying) {
            if (ImGui::Button(u8"▶")) enterPlayMode(world);
        } else {
            if (m_isPaused) {
                if (ImGui::Button(u8"▶")) m_isPaused = false;
            } else {
                if (ImGui::Button(u8"⏸")) m_isPaused = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"⏹")) exitPlayMode(world);
        }
    }
    ImGui::End();
}
```

**Step 6: Wire `tickSystems` and `renderPlaybar` into `render()`**

In `SceneEditor::render(f32 deltaTime)`:
- After `if (!activeWorld) return;`, add: `tickSystems(*activeWorld, deltaTime);`
- Before `m_viewport.render(*activeWorld, m_ctx);`, add: `renderPlaybar(*activeWorld);`

**Step 7: Build and verify**
```bash
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1 | tail -20
```
Expected: compiles cleanly.

**Step 8: Commit**
```bash
git add src/editor/SceneEditor.cpp
git commit -m "feat(editor): implement play/pause/stop mode with entity snapshot and system ticking"
```

---

## Task 4: Physics Debug Overlay in SceneViewport

**Files:**
- Modify: `src/editor/SceneViewport.hpp`
- Modify: `src/editor/SceneViewport.cpp`

**Context:** `drawSprites` already has access to `ImDrawList*` via `ImGui::GetWindowDrawList()`. We add `drawPhysicsDebug` that draws collider outlines: green for static, cyan for dynamic, yellow for kinematic, red for triggers.

**Step 1: Add declaration to SceneViewport.hpp**

In `SceneViewport.hpp`, in the `private:` method declarations section, add:
```cpp
    void drawPhysicsDebug(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
```

Also add include at top:
```cpp
#include "physics/PhysicsComponents2D.hpp"
```

**Step 2: Implement `drawPhysicsDebug` in SceneViewport.cpp**

Add after `drawGizmo`:
```cpp
void SceneViewport::drawPhysicsDebug(ECS::World& world, EditorContext& ctx,
                                      ImVec2 origin, ImVec2 viewportSize) {
    using namespace Physics2D;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float zoom = ctx.zoom;
    float camX = ctx.cameraOffset.x;
    float camY = ctx.cameraOffset.y;

    auto worldToScreen = [&](float wx, float wy) -> ImVec2 {
        float sx = origin.x + (wx - camX) * zoom + viewportSize.x * 0.5f;
        float sy = origin.y + (wy - camY) * zoom + viewportSize.y * 0.5f;
        return ImVec2(sx, sy);
    };

    ECS::ComponentQuery q;
    world.forEach<Collider2D, ECS::Position2D>(q,
        [&](ECS::Entity, Collider2D& col, ECS::Position2D& pos) {
            float cx = pos.x + col.offset.x;
            float cy = pos.y + col.offset.y;

            ImU32 color = col.isTrigger          ? IM_COL32(255, 80,  80,  200)
                        : col.isStatic           ? IM_COL32(80,  255, 80,  200)
                        : IM_COL32(80,  200, 255, 200);

            if (col.shape == ColliderShape::AABB) {
                float hw = col.size.x * 0.5f * zoom;
                float hh = col.size.y * 0.5f * zoom;
                ImVec2 sc = worldToScreen(cx, cy);
                dl->AddRect(ImVec2(sc.x - hw, sc.y - hh),
                            ImVec2(sc.x + hw, sc.y + hh),
                            color, 0.0f, 0, 1.5f);
            } else {
                ImVec2 sc = worldToScreen(cx, cy);
                dl->AddCircle(sc, col.radius * zoom, color, 32, 1.5f);
            }
        });
}
```

**Step 3: Call it in `SceneViewport::render`**

In `SceneViewport.cpp`, inside `render()`, after `drawSprites(...)`, add:
```cpp
    // Physics debug (always visible for now; gate behind a flag later)
    drawPhysicsDebug(world, ctx, origin, viewportSize);
```

**Step 4: Build**
```bash
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1 | tail -20
```

**Step 5: Commit**
```bash
git add src/editor/SceneViewport.hpp src/editor/SceneViewport.cpp
git commit -m "feat(editor): add physics collider debug overlay to scene viewport"
```

---

## Task 5: ScriptComponent Inspector drawer (script path + Load)

**Files:**
- Modify: `src/editor/InspectorPanel.cpp`
- Modify: `src/editor/SceneEditor.cpp` (wire ScriptEngine to drawer)

**Context:** `drawScript` already exists but is likely a stub. We need it to show the script path (editable text), a "Load" button, and error feedback. The drawer needs access to the `ScriptEngine` — pass it via a lambda capture when registering.

**Step 1: Check current `drawScript` implementation**

Read `src/editor/InspectorPanel.cpp` around line 274 to see the current stub.

**Step 2: Replace `drawScript` stub**

Replace whatever is there with:
```cpp
void InspectorPanel::drawScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* sc = world.get<Script::ScriptComponent>(e);
    if (!sc) {
        if (ImGui::Button("Add Script Component")) {
            world.add<Script::ScriptComponent>(e);
        }
        return;
    }

    static char pathBuf[512] = {};
    static std::string lastError;

    // Sync buffer to component path
    if (std::strlen(pathBuf) == 0 || std::string(pathBuf) != sc->scriptPath) {
        std::strncpy(pathBuf, sc->scriptPath.c_str(), sizeof(pathBuf) - 1);
    }

    ImGui::Text("Script");
    ImGui::SetNextItemWidth(-80.0f);
    if (ImGui::InputText("##scriptPath", pathBuf, sizeof(pathBuf))) {
        sc->scriptPath = pathBuf;
    }

    ImGui::SameLine();
    if (ImGui::Button("Load") && ctx.scriptEngine) {
        std::string err;
        bool ok = ctx.scriptEngine->loadScript(sc->scriptPath, &err);
        lastError = ok ? "" : err;
    }

    if (!lastError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
        ImGui::TextWrapped("%s", lastError.c_str());
        ImGui::PopStyleColor();
    }
}
```

**Step 3: Add `scriptEngine` to `EditorContext`**

In `src/editor/EditorContext.hpp`, add:
```cpp
#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
    Script::ScriptEngine* scriptEngine = nullptr;
#endif
```

**Step 4: Wire `m_scriptEngine` into context in SceneEditor**

In `SceneEditor::init(...)`, after `m_scriptEngineReady = m_scriptEngine.init(p);`, add:
```cpp
    m_ctx.scriptEngine = &m_scriptEngine;
```

**Step 5: Build + commit**
```bash
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1 | tail -20
git add src/editor/InspectorPanel.cpp src/editor/EditorContext.hpp src/editor/SceneEditor.cpp
git commit -m "feat(editor): add script component inspector drawer with path and load button"
```

---

## Task 6: Tileset Asset loading

**Files:**
- Create: `src/editor/TilesetAsset.hpp`
- Modify: `src/editor/TilemapEditor.hpp`
- Modify: `src/editor/TilemapEditor.cpp`

**Context:** The current palette in `renderPalette()` shows numbers 0–63. We replace it with a proper tileset: load a PNG, divide it into NxM tiles by pixel dimensions, and display each tile as an `ImGui::Image` button. We use SDL3's `SDL_LoadBMP`/`SDL_CreateTexture` or the existing `AssetManager` if available.

**Step 1: Create TilesetAsset.hpp**

Create `src/editor/TilesetAsset.hpp`:
```cpp
#pragma once
#include "core/Types.hpp"
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

struct TileUV {
    float u0, v0, u1, v1;
};

struct TilesetAsset {
    std::string path;
    i32 tileWidth  = 32;
    i32 tileHeight = 32;
    i32 columns    = 0;
    i32 rows       = 0;
    std::vector<TileUV> tiles;  // UV coords per tile

    // Opaque handle to GPU texture (void* for SDL3 ImGui texture)
    void* textureHandle = nullptr;
    i32 textureW = 0, textureH = 0;

    bool isLoaded() const { return textureHandle != nullptr; }

    // Compute tile UVs from texture dimensions + tile size
    void computeUVs() {
        tiles.clear();
        if (textureW <= 0 || textureH <= 0 || tileWidth <= 0 || tileHeight <= 0) return;
        columns = textureW / tileWidth;
        rows    = textureH / tileHeight;
        for (i32 row = 0; row < rows; ++row) {
            for (i32 col = 0; col < columns; ++col) {
                TileUV uv;
                uv.u0 = static_cast<float>(col * tileWidth)  / textureW;
                uv.v0 = static_cast<float>(row * tileHeight) / textureH;
                uv.u1 = static_cast<float>((col + 1) * tileWidth)  / textureW;
                uv.v1 = static_cast<float>((row + 1) * tileHeight) / textureH;
                tiles.push_back(uv);
            }
        }
    }

    i32 tileCount() const { return static_cast<i32>(tiles.size()); }
};

} // namespace Caffeine::Editor
```

**Step 2: Add TilesetAsset member and load method to TilemapEditorPanel**

In `TilemapEditor.hpp`, add:
```cpp
#include "editor/TilesetAsset.hpp"
```

In `TilemapEditorPanel` private section, add:
```cpp
    TilesetAsset m_tileset;
    char m_tilesetPathBuf[512] = {};
    i32  m_tileDisplaySize = 32;
```

In public section, add:
```cpp
    bool loadTileset(const std::string& path, void* renderer);
```

**Step 3: Implement `loadTileset` in TilemapEditor.cpp**

```cpp
#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_image.h>  // or use SDL_LoadBMP for BMP-only
#endif

bool TilemapEditorPanel::loadTileset(const std::string& path, void* renderer) {
#ifdef CF_HAS_SDL3
    SDL_Renderer* r = static_cast<SDL_Renderer*>(renderer);
    SDL_Surface* surf = SDL_LoadBMP(path.c_str());
    if (!surf) {
        // Try with SDL_image if available, else fail
        return false;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return false;

    float w = 0, h = 0;
    SDL_GetTextureSize(tex, &w, &h);

    m_tileset.path = path;
    m_tileset.textureHandle = tex;
    m_tileset.textureW = static_cast<i32>(w);
    m_tileset.textureH = static_cast<i32>(h);
    m_tileset.tileWidth  = static_cast<i32>(m_tilemap.tileSize());
    m_tileset.tileHeight = static_cast<i32>(m_tilemap.tileSize());
    m_tileset.computeUVs();
    return true;
#else
    (void)path; (void)renderer;
    return false;
#endif
}
```

**Step 4: Replace `renderPalette` with texture-aware version**

Replace the existing `renderPalette()` in `TilemapEditor.cpp` (inside `#ifdef CF_HAS_IMGUI`) with:

```cpp
void TilemapEditorPanel::renderPalette() {
    ImGui::Text("Tile Palette");

    // Tileset loader row
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    ImGui::InputText("##tilesetPath", m_tilesetPathBuf, sizeof(m_tilesetPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        // Tileset loading requires renderer access — handled via SceneEditor calling loadTileset()
        // For now, show a placeholder message
        ImGui::OpenPopup("TilesetLoadNote");
    }

    if (ImGui::BeginPopup("TilesetLoadNote")) {
        ImGui::Text("Call loadTileset(\"%s\", renderer) from SceneEditor", m_tilesetPathBuf);
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // If a tileset is loaded, show texture tiles; else fallback to numbered grid
    if (m_tileset.isLoaded()) {
        ImTextureID texId = reinterpret_cast<ImTextureID>(m_tileset.textureHandle);
        float dispSize = static_cast<float>(m_tileDisplaySize);
        i32 tilesPerRow = std::max(1, static_cast<i32>(ImGui::GetContentRegionAvail().x / (dispSize + 4)));

        for (i32 i = 0; i < m_tileset.tileCount(); ++i) {
            if (i % tilesPerRow != 0) ImGui::SameLine();
            const TileUV& uv = m_tileset.tiles[i];
            bool isSelected = (i == m_selectedTileID);

            ImVec2 uv0(uv.u0, uv.v0), uv1(uv.u1, uv.v1);
            ImVec4 bg    = isSelected ? ImVec4(0.3f, 0.6f, 1.0f, 0.5f) : ImVec4(0,0,0,0);
            ImVec4 tint  = ImVec4(1,1,1,1);

            ImGui::PushID(i);
            if (ImGui::ImageButton("##tile", texId, ImVec2(dispSize, dispSize), uv0, uv1, bg, tint)) {
                m_selectedTileID = i;
            }
            ImGui::PopID();
        }
    } else {
        // Fallback: numbered grid (existing behavior)
        static const i32 tilesPerRow = 8;
        static const i32 paletteSize = 64;
        for (i32 i = 0; i < paletteSize; ++i) {
            if (i % tilesPerRow != 0) ImGui::SameLine();
            bool isSelected = (i == m_selectedTileID);
            std::string label = std::to_string(i);
            if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(28, 28))) {
                m_selectedTileID = i;
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Selected: %d", m_selectedTileID);
}
```

**Step 5: Build + commit**
```bash
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1 | tail -20
git add src/editor/TilesetAsset.hpp src/editor/TilemapEditor.hpp src/editor/TilemapEditor.cpp
git commit -m "feat(editor): add tileset asset loading with texture-based tile palette"
```

---

## Task 7: Wire tileset loader to SceneEditor (renderer access)

**Files:**
- Modify: `src/editor/SceneEditor.cpp` (init section)

**Context:** `loadTileset` needs an SDL_Renderer pointer. The `RenderDevice` holds the renderer. We expose a "Load Tileset" button in the Tilemap Editor toolbar that calls `m_tilemapEditor.loadTileset(path, renderer)`.

**Step 1: Get renderer pointer from RenderDevice**

In `SceneEditor.cpp`, in `TilemapEditorPanel::renderToolbar`, the "Resize" button exists. Add after it in `SceneEditor.cpp` `init()`:
```cpp
    // Wire tileset loading command
    m_commandPalette.registerCommand("action_load_tileset", "Load Tileset", "Actions", [this]() {
        // TODO: open file picker, then call m_tilemapEditor.loadTileset(path, renderer)
    });
```

For now the "Load" button in the palette UI shows a popup indicating it needs renderer access — this is an acceptable Phase 1 state. Tileset loading works fully when called programmatically via `loadTileset`.

**Step 2: Final build verification**
```bash
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1 | tail -30
```
Expected: zero errors.

**Step 3: Final commit**
```bash
git add src/editor/SceneEditor.cpp
git commit -m "feat(editor): wire tileset load command into command palette"
```

---

## Acceptance Criteria

- [ ] `cmake --build build --config Release --target doppio` succeeds with no errors
- [ ] doppio.exe launches without crash
- [ ] ▶ button appears centered above the viewport
- [ ] Pressing ▶ starts physics: entities with RigidBody2D fall due to gravity
- [ ] Pressing ⏹ restores entities to pre-play positions
- [ ] Green/cyan/red collider outlines visible in viewport
- [ ] Script component in Inspector shows path field + Load button
- [ ] Tilemap palette shows texture tiles when a tileset is loaded, numbers otherwise
- [ ] `CAFFEINE_ENABLE_SCRIPTING=ON` builds without errors

---

## Known Limitations (out of scope for this plan)

- Physics debug overlay is always visible (no toggle yet)
- Tileset loading via UI requires renderer pointer plumbing (deferred)  
- Script hot-reload UI not yet wired (ScriptWatcher exists but not connected to editor)
- Tilemap is not yet a scene ECS entity/component (it's a standalone panel)
