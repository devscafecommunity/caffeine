# Inspector 2.0 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Upgrade the Inspector with a unified Transform component, typed widget library, asset picker fields, component lifecycle (enable/disable/remove), and a searchable Add Component registry.

**Architecture:** A new `InspectorWidgets.hpp` header provides stateless helper functions used by every drawer. A `ComponentRegistry` singleton owns the add-component list and is populated at startup. The existing `InspectorPanel` drawers are rewritten on top of these primitives without changing the panel's public API. `DisabledTag` is a zero-byte ECS tag that systems must opt-out of via `q.without<DisabledTag>()`.

**Tech Stack:** C++20, ImGui, ECS::World template API, DragDropSystem (already in codebase), `std::filesystem`

**Build command:** `cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio`

**Key files to understand before starting:**
- `src/ecs/Components.hpp` — existing component structs
- `src/editor/InspectorPanel.hpp/.cpp` — current inspector (~591 lines)
- `src/editor/DragDropSystem.hpp` — `DragDropManager::AcceptAssetDrop()` usage
- `src/editor/HierarchyPanel.cpp` — `createEntityWithType()` to update
- `src/containers/FixedString.hpp` — `cStr()`, not `c_str()`
- `src/math/Vec3.hpp` — Vec3 struct fields

---

## Task 1: Add `Transform` and `DisabledTag` to ECS

**Files:**
- Modify: `src/ecs/Components.hpp`

**Step 1: Read Vec3 struct to know field names**

Open `src/math/Vec3.hpp` — confirm fields are `x`, `y`, `z` (f32).

**Step 2: Add structs after `PersistentComponent`**

In `src/ecs/Components.hpp`, after `struct PersistentComponent { ... };`, add:

```cpp
struct DisabledTag {};

struct Transform {
    Vec3 position = {0.0f, 0.0f, 0.0f};
    Vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler angles in degrees
    Vec3 scale    = {1.0f, 1.0f, 1.0f};
};
```

**Step 3: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```
Expected: build succeeds (no compilation errors — these are simple struct additions).

**Step 4: Commit**

```bash
git add src/ecs/Components.hpp
git commit -m "feat(ecs): add Transform component and DisabledTag"
```

---

## Task 2: Create `InspectorWidgets.hpp`

**Files:**
- Create: `src/editor/InspectorWidgets.hpp`

**Step 1: Create the file**

```cpp
#pragma once
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include <string>
#include <filesystem>
#include <functional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>

namespace Caffeine::Editor::Widgets {

inline bool DragVec3(const char* label, Vec3& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    float tmp[3] = { v.x, v.y, v.z };
    if (ImGui::DragFloat3(label, tmp, speed, lo, hi)) {
        v.x = tmp[0]; v.y = tmp[1]; v.z = tmp[2];
        return true;
    }
    return false;
}

inline bool DragVec3Disabled(const char* label, Vec3& v, float speed = 0.1f) {
    ImGui::BeginDisabled();
    float tmp[3] = { v.x, v.y, v.z };
    ImGui::DragFloat3(label, tmp, speed);
    ImGui::EndDisabled();
    return false;
}

inline bool DragVec2(const char* label, Vec2& v, float speed = 0.1f,
                     float lo = -1e9f, float hi = 1e9f) {
    float tmp[2] = { v.x, v.y };
    if (ImGui::DragFloat2(label, tmp, speed, lo, hi)) {
        v.x = tmp[0]; v.y = tmp[1];
        return true;
    }
    return false;
}

inline bool InputText(const char* label, std::string& str) {
    char buf[512];
    strncpy(buf, str.c_str(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText(label, buf, sizeof(buf))) {
        str = buf;
        return true;
    }
    return false;
}

template<int N>
inline bool EnumCombo(const char* label, int& current,
                      const char* const (&names)[N]) {
    return ImGui::Combo(label, &current, names, N);
}

// Renders: [path (truncated)]  [...]
// Returns true when path changed.
// filter: semicolon-separated extensions, e.g. ".png;.jpg"
inline bool AssetField(const char* label, std::string& path,
                       const char* filter,
                       const std::filesystem::path& projectRoot) {
    bool changed = false;

    // Truncated display
    std::string display = path.empty() ? "(none)" : std::filesystem::path(path).filename().string();
    ImGui::InputText(label, display.data(), display.size() + 1,
                     ImGuiInputTextFlags_ReadOnly);

    ImGui::SameLine();
    std::string btnId = std::string("...##") + label;
    if (ImGui::Button(btnId.c_str(), ImVec2(28, 0))) {
        ImGui::OpenPopup(label);
    }

    if (ImGui::BeginPopup(label)) {
        ImGui::Text("Select asset (%s)", filter);
        ImGui::Separator();

        static char search[128] = {};
        ImGui::InputText("##search", search, sizeof(search));

        if (std::filesystem::exists(projectRoot)) {
            std::string filterStr(filter);
            for (auto& entry : std::filesystem::recursive_directory_iterator(
                     projectRoot, std::filesystem::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                if (filterStr.find(ext) == std::string::npos) continue;
                std::string fname = entry.path().filename().string();
                if (search[0] != '\0' && fname.find(search) == std::string::npos) continue;

                if (ImGui::Selectable(fname.c_str())) {
                    path = entry.path().string();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::TextDisabled("No project root set");
        }

        ImGui::EndPopup();
    }

    return changed;
}

// Renders component header: [▶ Label]  [enabled checkbox]  [⋮]
// Returns true if header is open.
// Sets *outRemove = true if user clicked Remove.
inline bool ComponentHeader(const char* label, bool& enabled, bool& outRemove) {
    outRemove = false;

    ImGui::PushID(label);

    bool open = ImGui::CollapsingHeader("##hdr", ImGuiTreeNodeFlags_DefaultOpen |
                                                  ImGuiTreeNodeFlags_AllowOverlap);

    // Overlay: enabled checkbox
    ImGui::SameLine();
    ImGui::Checkbox("##en", &enabled);

    // Overlay: bold label
    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    // Overlay: context menu button
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
    if (ImGui::SmallButton("⋮")) {
        ImGui::OpenPopup("##cmenu");
    }
    if (ImGui::BeginPopup("##cmenu")) {
        if (ImGui::MenuItem("Remove Component")) outRemove = true;
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return open;
}

} // namespace Caffeine::Editor::Widgets
#endif // CF_HAS_IMGUI
```

**Step 2: Build to verify the header compiles**

The header is not included anywhere yet — add a temporary `#include "editor/InspectorWidgets.hpp"` at the top of `InspectorPanel.cpp` and build:

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```
Expected: success. Remove no code — leave the include in for the next task.

**Step 3: Commit**

```bash
git add src/editor/InspectorWidgets.hpp src/editor/InspectorPanel.cpp
git commit -m "feat(editor): add InspectorWidgets helper library"
```

---

## Task 3: Create `ComponentRegistry`

**Files:**
- Create: `src/editor/ComponentRegistry.hpp`
- Create: `src/editor/ComponentRegistry.cpp`

**Step 1: Create `ComponentRegistry.hpp`**

```cpp
#pragma once
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "core/Types.hpp"
#include <functional>
#include <vector>
#include <string>

namespace Caffeine::Editor {

struct ComponentEntry {
    std::string category;
    std::string name;
    std::function<bool(ECS::World&, ECS::Entity)> has;
    std::function<void(ECS::World&, ECS::Entity)> add;
};

class ComponentRegistry {
public:
    static ComponentRegistry& instance();

    void registerComponent(ComponentEntry entry);
    const std::vector<ComponentEntry>& entries() const { return m_entries; }

private:
    std::vector<ComponentEntry> m_entries;
};

void registerAllComponents(ComponentRegistry& reg);

} // namespace Caffeine::Editor
```

**Step 2: Create `ComponentRegistry.cpp`**

```cpp
#include "editor/ComponentRegistry.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "audio/AudioComponents.hpp"
#include "script/ScriptTypes.hpp"
#include "ui/UIComponents.hpp"

namespace Caffeine::Editor {

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry reg;
    return reg;
}

void ComponentRegistry::registerComponent(ComponentEntry entry) {
    m_entries.push_back(std::move(entry));
}

void registerAllComponents(ComponentRegistry& reg) {
    // ── 2D Physics ──
    reg.registerComponent({
        "Physics 2D", "RigidBody2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<Physics2D::RigidBody2D>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Physics2D::RigidBody2D>(e); }
    });
    reg.registerComponent({
        "Physics 2D", "Collider2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<Physics2D::Collider2D>(e); },
        [](ECS::World& w, ECS::Entity e){
            Physics2D::Collider2D col;
            col.shape = Physics2D::ColliderShape::AABB;
            col.size  = {64.0f, 64.0f};
            col.radius = 32.0f;
            w.add<Physics2D::Collider2D>(e, col);
        }
    });
    // ── Rendering ──
    reg.registerComponent({
        "Rendering", "Sprite Renderer",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Sprite>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Sprite>(e); }
    });
    reg.registerComponent({
        "Rendering", "Mesh Filter",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::MeshFilterComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::MeshFilterComponent>(e); }
    });
    reg.registerComponent({
        "Rendering", "Mesh Renderer",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::MeshRendererComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::MeshRendererComponent>(e); }
    });
    // ── Audio ──
    reg.registerComponent({
        "Audio", "Audio Source",
        [](ECS::World& w, ECS::Entity e){ return w.has<Audio::AudioEmitter>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Audio::AudioEmitter>(e); }
    });
    // ── Scripting ──
    reg.registerComponent({
        "Scripting", "Script",
        [](ECS::World& w, ECS::Entity e){ return w.has<Script::ScriptComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<Script::ScriptComponent>(e); }
    });
    // ── UI ──
    reg.registerComponent({
        "UI", "UI Widget",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIWidget>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIWidget>(e); }
    });
    reg.registerComponent({
        "UI", "UI Button",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIButton>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIButton>(e); }
    });
    reg.registerComponent({
        "UI", "UI Label",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UILabel>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UILabel>(e); }
    });
    reg.registerComponent({
        "UI", "UI Progress Bar",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UIProgressBar>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UIProgressBar>(e); }
    });
    reg.registerComponent({
        "UI", "UI Slider",
        [](ECS::World& w, ECS::Entity e){ return w.has<UI::UISlider>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<UI::UISlider>(e); }
    });
    // ── System ──
    reg.registerComponent({
        "System", "Persistent",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::PersistentComponent>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::PersistentComponent>(e); }
    });
    // ── Game ──
    reg.registerComponent({
        "Game", "Health",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Health>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Health>(e); }
    });
    reg.registerComponent({
        "Game", "Velocity2D",
        [](ECS::World& w, ECS::Entity e){ return w.has<ECS::Velocity2D>(e); },
        [](ECS::World& w, ECS::Entity e){ w.add<ECS::Velocity2D>(e); }
    });
}

} // namespace Caffeine::Editor
```

**Step 3: Add `ComponentRegistry.cpp` to CMake**

Open `CMakeLists.txt` (or the doppio target CMakeLists) and find where `InspectorPanel.cpp` is listed. Add `src/editor/ComponentRegistry.cpp` to the same source list.

To find the right file:
```bash
grep -rn "InspectorPanel.cpp" "C:/Users/Pedro Jesus/Downloads/caffeine" --include="CMakeLists.txt"
```

**Step 4: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```
Expected: success.

**Step 5: Commit**

```bash
git add src/editor/ComponentRegistry.hpp src/editor/ComponentRegistry.cpp CMakeLists.txt
git commit -m "feat(editor): add ComponentRegistry with all component entries"
```

---

## Task 4: Rewrite `InspectorPanel` — Transform drawer + lifecycle headers

**Files:**
- Modify: `src/editor/InspectorPanel.hpp`
- Modify: `src/editor/InspectorPanel.cpp`

**Step 1: Update `InspectorPanel.hpp` — add includes and declare `drawTransform3D`**

Add to the includes block (after existing includes):
```cpp
#include "editor/InspectorWidgets.hpp"
#include "editor/ComponentRegistry.hpp"
```

Add to the private section (after existing `drawUISlider` declaration):
```cpp
void drawTransform3D(ECS::World& world, ECS::Entity e, EditorContext& ctx);
std::filesystem::path resolveProjectRoot(const EditorContext& ctx) const;
```

Also add `#include <filesystem>` if not already present.

**Step 2: Rewrite `drawTransform` in `InspectorPanel.cpp`**

Replace the entire `drawTransform` function (lines ~153–208) with:

```cpp
void InspectorPanel::drawTransform(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    bool enabled = !world.has<ECS::DisabledTag>(e);
    bool removeRequested = false;

    if (!Widgets::ComponentHeader("Transform", enabled, removeRequested)) return;

    if (!enabled) world.add<ECS::DisabledTag>(e);
    else if (world.has<ECS::DisabledTag>(e)) world.remove<ECS::DisabledTag>(e);

    // Unified Transform (new entities)
    if (world.has<ECS::Transform>(e)) {
        auto* t = world.get<ECS::Transform>(e);
        if (ImGui::IsItemActivated()) { ctx.beginUndo(EditorCommand::SetField, e.id(), world); m_undoStarted = true; }

        bool is2D = !world.has<ECS::MeshFilterComponent>(e) && !world.has<ECS::MeshRendererComponent>(e);

        if (Widgets::DragVec3("Position", t->position, 0.5f)) ctx.isDirty = true;

        if (is2D) {
            ImGui::DragFloat("Rotation", &t->rotation.z, 1.0f, -360.0f, 360.0f);
            if (ImGui::IsItemEdited()) ctx.isDirty = true;
        } else {
            if (Widgets::DragVec3("Rotation", t->rotation, 1.0f, -360.0f, 360.0f)) ctx.isDirty = true;
        }

        if (is2D) {
            float s[2] = { t->scale.x, t->scale.y };
            if (ImGui::DragFloat2("Scale", s, 0.05f, 0.01f, 100.0f)) {
                t->scale.x = s[0]; t->scale.y = s[1];
                ctx.isDirty = true;
            }
        } else {
            if (Widgets::DragVec3("Scale", t->scale, 0.05f, 0.01f, 100.0f)) ctx.isDirty = true;
        }

        if (ImGui::IsItemDeactivatedAfterEdit() && m_undoStarted) { ctx.endUndo(world); m_undoStarted = false; }
        return;
    }

    // Legacy 2D components (backwards compatibility)
    if (world.has<ECS::Position2D>(e)) {
        auto* pos = world.get<ECS::Position2D>(e);
        float p[2] = { pos->x, pos->y };
        if (ImGui::DragFloat2("Position", p, 0.5f)) { pos->x = p[0]; pos->y = p[1]; ctx.isDirty = true; }
    }
    if (world.has<ECS::Rotation>(e)) {
        auto* rot = world.get<ECS::Rotation>(e);
        float deg = rot->angle * 180.0f / 3.14159265f;
        if (ImGui::DragFloat("Rotation", &deg, 1.0f, -360.0f, 360.0f)) {
            rot->angle = deg * 3.14159265f / 180.0f;
            ctx.isDirty = true;
        }
    }
    if (world.has<ECS::Scale2D>(e)) {
        auto* scl = world.get<ECS::Scale2D>(e);
        float s[2] = { scl->x, scl->y };
        if (ImGui::DragFloat2("Scale", s, 0.1f, 0.01f, 100.0f)) { scl->x = s[0]; scl->y = s[1]; ctx.isDirty = true; }
    }
}
```

**Note:** `world.remove<T>()` — verify this method exists on `ECS::World`. Search: `grep -n "remove" src/ecs/World.hpp`. If it's named differently (e.g. `world.detach<T>(e)` or `world.removeComponent<T>(e)`), use the correct name throughout this task.

**Step 3: Wrap remaining component headers with `ComponentHeader`**

For each drawer that currently calls `ImGui::CollapsingHeader(...)`, replace with the pattern below. Use `drawRigidBody2D` as the first example:

```cpp
void InspectorPanel::drawRigidBody2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::RigidBody2D>(e)) return;
    bool enabled = true;  // RigidBody2D has no per-component disable — use DisabledTag on entity
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("RigidBody2D", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Physics2D::RigidBody2D>(e);
        ctx.isDirty = true;
        return;
    }
    auto* rb = world.get<Physics2D::RigidBody2D>(e);
    // ... rest of drawer unchanged ...
}
```

Apply the same pattern to: `drawCollider2D`, `drawSprite`, `drawAudioSource`, `drawHealth`, `drawVelocity2D`, `drawMeshFilter`, `drawUIWidget`, `drawUIButton`, `drawUILabel`, `drawUIProgressBar`, `drawUISlider`, `drawPersistent`, `drawScript`.

For drawers that previously had "if component missing → show Add button" pattern: **remove that fallback** — the Add Component button now handles this via the registry.

**Step 4: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```
Fix any errors. Common issues:
- `world.remove<T>(e)` may not exist — check `ECS::World` API
- `ECS::DisabledTag` not in scope — add `#include "ecs/Components.hpp"` (already included)

**Step 5: Commit**

```bash
git add src/editor/InspectorPanel.hpp src/editor/InspectorPanel.cpp
git commit -m "feat(inspector): unified Transform drawer and ComponentHeader lifecycle"
```

---

## Task 5: Upgrade Sprite and AudioSource with `AssetField`

**Files:**
- Modify: `src/editor/InspectorPanel.cpp`

**Step 1: Add `resolveProjectRoot` helper**

At the bottom of `InspectorPanel.cpp`, before the closing `}`, add:

```cpp
std::filesystem::path InspectorPanel::resolveProjectRoot(const EditorContext& ctx) const {
    if (!ctx.currentScenePath.empty()) {
        return std::filesystem::path(ctx.currentScenePath).parent_path();
    }
    return {};
}
```

Check `EditorContext` struct for the correct field name — it may be `currentScenePath` (string). Grep: `grep -n "currentScene\|projectRoot\|RootPath" src/editor/EditorContext.hpp`.

**Step 2: Upgrade `drawSprite`**

Replace the `InputText("Texture", ...)` block in `drawSprite` with:

```cpp
auto* sprite = world.get<ECS::Sprite>(e);
if (Widgets::AssetField("Texture", sprite->name, ".png;.jpg;.bmp", resolveProjectRoot(ctx)))
    ctx.isDirty = true;
// Keep existing DragDrop target for backwards compat
if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
    if (asset->type == AssetType::Texture) {
        sprite->name = asset->path;
        ctx.isDirty = true;
    }
}
int frame = static_cast<int>(sprite->frameIndex);
if (ImGui::DragInt("Frame", &frame, 1, 0, 1000)) {
    sprite->frameIndex = static_cast<u32>(frame > 0 ? frame : 0);
    ctx.isDirty = true;
}
```

**Step 3: Upgrade `drawAudioSource`**

Replace the `InputText("Clip", ...)` block with:

```cpp
auto* emitter = world.get<Audio::AudioEmitter>(e);
std::string clipStr(emitter->clipPath.data());
if (Widgets::AssetField("Clip", clipStr, ".wav;.ogg;.mp3", resolveProjectRoot(ctx))) {
    emitter->clipPath = clipStr.c_str();
    ctx.isDirty = true;
}
```

**Step 4: Upgrade `drawMeshFilter`** — add AssetField for `customMeshPath`:

```cpp
if (prim->primitive == ECS::MeshPrimitive::Custom) {
    if (Widgets::AssetField("Mesh", prim->customMeshPath, ".obj;.fbx;.gltf", resolveProjectRoot(ctx)))
        ctx.isDirty = true;
}
```

**Step 5: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```

**Step 6: Commit**

```bash
git add src/editor/InspectorPanel.cpp
git commit -m "feat(inspector): asset picker fields for Sprite, AudioSource, MeshFilter"
```

---

## Task 6: Replace Add Component popup with Registry-driven searchable menu

**Files:**
- Modify: `src/editor/InspectorPanel.cpp`
- Modify: `src/editor/SceneEditor.cpp` (call `registerAllComponents` at init)

**Step 1: Replace the popup in `InspectorPanel::render`**

Find the block starting with `if (ImGui::Button("+ Add Component", ...))` (lines ~75–143) and replace it entirely with:

```cpp
if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
    ImGui::OpenPopup("add_component_v2");
    m_addComponentSearch[0] = '\0';
}

if (ImGui::BeginPopup("add_component_v2")) {
    ImGui::InputText("##search", m_addComponentSearch, sizeof(m_addComponentSearch));
    ImGui::Separator();

    const char* lastCategory = nullptr;
    for (const auto& entry : ComponentRegistry::instance().entries()) {
        if (entry.has(world, e)) continue;  // already on entity

        if (m_addComponentSearch[0] != '\0') {
            std::string lower = entry.name;
            std::string query = m_addComponentSearch;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::transform(query.begin(), query.end(), query.begin(), ::tolower);
            if (lower.find(query) == std::string::npos) continue;
        }

        if (!lastCategory || entry.category != lastCategory) {
            if (lastCategory) ImGui::Separator();
            ImGui::TextDisabled("%s", entry.category.c_str());
            lastCategory = entry.category.c_str();
        }

        if (ImGui::MenuItem(entry.name.c_str())) {
            ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
            entry.add(world, e);
            ctx.endUndo(world);
        }
    }

    ImGui::EndPopup();
}
```

**Step 2: Add `m_addComponentSearch` member to `InspectorPanel`**

In `InspectorPanel.hpp`, private section:
```cpp
char m_addComponentSearch[128] = {};
```

**Step 3: Call `registerAllComponents` at startup**

In `src/editor/SceneEditor.cpp`, in `SceneEditor::init(...)`, at the end before `return true;`:
```cpp
Editor::registerAllComponents(Editor::ComponentRegistry::instance());
```

Add the include at top of `SceneEditor.cpp`:
```cpp
#include "editor/ComponentRegistry.hpp"
```

**Step 4: Add required includes to `InspectorPanel.cpp`**

At the top:
```cpp
#include <algorithm>
#include <cctype>
```

**Step 5: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```

**Step 6: Commit**

```bash
git add src/editor/InspectorPanel.hpp src/editor/InspectorPanel.cpp src/editor/SceneEditor.cpp
git commit -m "feat(inspector): searchable Add Component menu via ComponentRegistry"
```

---

## Task 7: Update HierarchyPanel — new entities get `Transform`

**Files:**
- Modify: `src/editor/HierarchyPanel.cpp`

**Step 1: Find `createEntityWithType`**

```bash
grep -n "createEntityWithType\|Position2D\|Scale2D" src/editor/HierarchyPanel.cpp | head -40
```

**Step 2: Replace legacy component adds with `Transform` for new entity types**

For each `world.add<ECS::Position2D>(e, ...)`, `world.add<ECS::Rotation>(...)`, `world.add<ECS::Scale2D>(...)` — replace the three calls with one:

```cpp
world.add<ECS::Transform>(e);
```

Do this for all new entity creation paths in the Create menu section. **Leave** any existing code that reads/writes these components at runtime (physics system, etc.) untouched.

**Step 3: Build**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio
```

**Step 4: Commit**

```bash
git add src/editor/HierarchyPanel.cpp
git commit -m "feat(hierarchy): new entities created with Transform component"
```

---

## Task 8: Final verification

**Step 1: Full build, clean**

```
cmake --build "C:/Users/Pedro Jesus/Downloads/caffeine/build" --config Release --target doppio 2>&1
```
Expected: zero errors, zero warnings about new files.

**Step 2: Manual smoke test checklist**

Launch `build/Release/doppio.exe` and verify:
- [ ] Create → 3D → Cube → Inspector shows Transform with X/Y/Z position, rotation Z only grayed if no 3D components
- [ ] Create → 2D → Sprite2D → Inspector shows Transform with rotation showing only Z field
- [ ] Click `+ Add Component` → popup opens, search "rigid" filters to RigidBody2D
- [ ] Add Sprite Renderer → click `...` next to Texture → popup lists .png files from project
- [ ] Right-click (⋮) on any component header → "Remove Component" removes it
- [ ] Checkbox next to component header → disables/enables the entity tag

**Step 3: Final commit if any minor fixes needed**

```bash
git add -A
git commit -m "fix(inspector): post-verification fixes"
```
