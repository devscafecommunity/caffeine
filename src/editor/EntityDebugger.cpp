#include "editor/EntityDebugger.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/PrefabComponents.hpp"
#include "ecs/ComponentID.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "audio/AudioComponents.hpp"
#include "script/ScriptTypes.hpp"
#include "ui/UIComponents.hpp"
#include "scene/SceneComponents.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

namespace {

struct DebugComponentProbe {
    const char* typeName;
    u32 componentId;
    usize sizeBytes;
    std::function<bool(ECS::World&, ECS::Entity)> has;
    std::function<const void*(ECS::World&, ECS::Entity)> getData;
};

struct SystemQueryInfo {
    const char* name;
    int priority;
    const char* const* requiredComponents;
    usize requiredCount;
};

const std::vector<DebugComponentProbe>& debugProbes() {
    static const std::vector<DebugComponentProbe> probes = {
        {"NameComponent", ECS::ComponentID::get<Editor::NameComponent>(), sizeof(Editor::NameComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<Editor::NameComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Editor::NameComponent>(e); }},
        {"Transform", ECS::ComponentID::get<ECS::Transform>(), sizeof(ECS::Transform),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Transform>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Transform>(e); }},
        {"Acceleration2D", ECS::ComponentID::get<ECS::Acceleration2D>(), sizeof(ECS::Acceleration2D),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Acceleration2D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Acceleration2D>(e); }},
        {"Sprite", ECS::ComponentID::get<ECS::Sprite>(), sizeof(ECS::Sprite),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Sprite>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Sprite>(e); }},
        {"Tag", ECS::ComponentID::get<ECS::Tag>(), sizeof(ECS::Tag),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Tag>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Tag>(e); }},
        {"Position3D", ECS::ComponentID::get<ECS::Position3D>(), sizeof(ECS::Position3D),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Position3D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Position3D>(e); }},
        {"Rotation3D", ECS::ComponentID::get<ECS::Rotation3D>(), sizeof(ECS::Rotation3D),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Rotation3D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Rotation3D>(e); }},
        {"Scale3D", ECS::ComponentID::get<ECS::Scale3D>(), sizeof(ECS::Scale3D),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Scale3D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Scale3D>(e); }},
        {"MeshFilter", ECS::ComponentID::get<ECS::MeshFilterComponent>(), sizeof(ECS::MeshFilterComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::MeshFilterComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::MeshFilterComponent>(e); }},
        {"MeshRenderer", ECS::ComponentID::get<ECS::MeshRendererComponent>(), sizeof(ECS::MeshRendererComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::MeshRendererComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::MeshRendererComponent>(e); }},
        {"Camera2D", ECS::ComponentID::get<ECS::Camera2DComponent>(), sizeof(ECS::Camera2DComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Camera2DComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Camera2DComponent>(e); }},
        {"Camera3D", ECS::ComponentID::get<ECS::Camera3DComponent>(), sizeof(ECS::Camera3DComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::Camera3DComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::Camera3DComponent>(e); }},
        {"CameraActive", ECS::ComponentID::get<ECS::CameraActiveComponent>(), sizeof(ECS::CameraActiveComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::CameraActiveComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::CameraActiveComponent>(e); }},
        {"Light", ECS::ComponentID::get<ECS::LightComponent>(), sizeof(ECS::LightComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::LightComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::LightComponent>(e); }},
        {"DirectionalLight", ECS::ComponentID::get<ECS::DirectionalLightComponent>(), sizeof(ECS::DirectionalLightComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::DirectionalLightComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::DirectionalLightComponent>(e); }},
        {"PointLight", ECS::ComponentID::get<ECS::PointLightComponent>(), sizeof(ECS::PointLightComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::PointLightComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::PointLightComponent>(e); }},
        {"SpotLight", ECS::ComponentID::get<ECS::SpotLightComponent>(), sizeof(ECS::SpotLightComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::SpotLightComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::SpotLightComponent>(e); }},
        {"RigidBody2D", ECS::ComponentID::get<Physics2D::RigidBody2D>(), sizeof(Physics2D::RigidBody2D),
         [](ECS::World& w, ECS::Entity e) { return w.has<Physics2D::RigidBody2D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Physics2D::RigidBody2D>(e); }},
        {"Collider2D", ECS::ComponentID::get<Physics2D::Collider2D>(), sizeof(Physics2D::Collider2D),
         [](ECS::World& w, ECS::Entity e) { return w.has<Physics2D::Collider2D>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Physics2D::Collider2D>(e); }},
        {"AudioEmitter", ECS::ComponentID::get<Audio::AudioEmitter>(), sizeof(Audio::AudioEmitter),
         [](ECS::World& w, ECS::Entity e) { return w.has<Audio::AudioEmitter>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Audio::AudioEmitter>(e); }},
        {"Script (Lua)", ECS::ComponentID::get<Script::ScriptComponent>(), sizeof(Script::ScriptComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<Script::ScriptComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Script::ScriptComponent>(e); }},
        {"Script (C++)", ECS::ComponentID::get<Script::CppScriptComponent>(), sizeof(Script::CppScriptComponent),
         [](ECS::World& w, ECS::Entity e) { return w.has<Script::CppScriptComponent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Script::CppScriptComponent>(e); }},
        {"Parent", ECS::ComponentID::get<Scene::Parent>(), sizeof(Scene::Parent),
         [](ECS::World& w, ECS::Entity e) { return w.has<Scene::Parent>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Scene::Parent>(e); }},
        {"WorldTransform", ECS::ComponentID::get<Scene::WorldTransform>(), sizeof(Scene::WorldTransform),
         [](ECS::World& w, ECS::Entity e) { return w.has<Scene::WorldTransform>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<Scene::WorldTransform>(e); }},
        {"PrefabInstance", ECS::ComponentID::get<ECS::PrefabInstance>(), sizeof(ECS::PrefabInstance),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::PrefabInstance>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::PrefabInstance>(e); }},
        {"UIWidget", ECS::ComponentID::get<UI::UIWidget>(), sizeof(UI::UIWidget),
         [](ECS::World& w, ECS::Entity e) { return w.has<UI::UIWidget>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<UI::UIWidget>(e); }},
        {"DisabledTag", ECS::ComponentID::get<ECS::DisabledTag>(), sizeof(ECS::DisabledTag),
         [](ECS::World& w, ECS::Entity e) { return w.has<ECS::DisabledTag>(e); },
         [](ECS::World& w, ECS::Entity e) -> const void* { return w.get<ECS::DisabledTag>(e); }},
    };
    return probes;
}

const DebugComponentProbe* findProbeByName(const char* name) {
    for (const auto& probe : debugProbes()) {
        if (std::strcmp(probe.typeName, name) == 0) {
            return &probe;
        }
    }
    return nullptr;
}

const std::vector<SystemQueryInfo>& systemQueries() {
    static const char* kPhysics[] = {"Transform", "RigidBody2D", "Collider2D"};
    static const char* kSpriteRender[] = {"Transform", "Sprite"};
    static const char* kMeshRender[] = {"Position3D", "MeshFilter", "MeshRenderer"};
    static const char* kScript[] = {"Script (Lua)"};
    static const char* kUI[] = {"UIWidget"};
    static const char* kHierarchy[] = {"Parent", "WorldTransform"};
    static const char* kAudio[] = {"AudioEmitter"};

    static const std::vector<SystemQueryInfo> systems = {
        {"PhysicsSystem2D", 50, kPhysics, 3},
        {"SpriteRenderSystem", 100, kSpriteRender, 2},
        {"MeshRenderSystem", 100, kMeshRender, 3},
        {"ScriptSystem", 10, kScript, 1},
        {"UISystem", 80, kUI, 1},
        {"HierarchySystem", 5, kHierarchy, 2},
        {"AudioSystem", 60, kAudio, 1},
    };
    return systems;
}

}  // namespace

ECS::Entity EntityDebuggerPanel::resolveTargetEntity(ECS::World& world, EditorContext& ctx) const {
    if (m_useManualId) {
        ECS::Entity manual(m_manualEntityId, &world);
        return world.isEntityAlive(manual) ? manual : ECS::Entity{};
    }
    if (ctx.selectedEntity.isValid() && world.isEntityAlive(ctx.selectedEntity)) {
        return ctx.selectedEntity;
    }
    return ECS::Entity{};
}

std::string EntityDebuggerPanel::componentNameForId(u32 componentId) const {
    for (const auto& probe : debugProbes()) {
        if (probe.componentId == componentId) {
            return probe.typeName;
        }
    }
    return "Component#" + std::to_string(componentId);
}

void EntityDebuggerPanel::refresh(ECS::World& world, ECS::Entity entity) {
    m_cachedComponents.clear();
    m_totalComponentBytes = 0;
    m_archetypeIndex = u32_max;
    m_archetypeHash = 0;

    if (!entity.isValid() || !world.isEntityAlive(entity)) {
        return;
    }

    const auto loc = world.getEntityLocation(entity);
    m_archetypeIndex = loc.archetypeIndex;

    if (const ECS::Archetype* arch = world.getArchetype(loc.archetypeIndex)) {
        m_archetypeHash = arch->getComponentSet().hash();
    }

    for (const auto& probe : debugProbes()) {
        if (!probe.has(world, entity)) {
            continue;
        }

        ComponentInfo info;
        info.name = probe.typeName;
        info.componentId = probe.componentId;
        info.sizeBytes = probe.sizeBytes;
        info.rawData = probe.getData(world, entity);
        m_totalComponentBytes += probe.sizeBytes;

        for (const auto& system : systemQueries()) {
            bool matches = true;
            for (usize i = 0; i < system.requiredCount; ++i) {
                const DebugComponentProbe* req = findProbeByName(system.requiredComponents[i]);
                if (!req || !req->has(world, entity)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                info.matchingSystems.push_back(system.name);
            }
        }

        m_cachedComponents.push_back(std::move(info));
    }
}

void EntityDebuggerPanel::renderMemoryHexView(const u8* data, usize size) {
    if (!data || size == 0) {
        ImGui::TextDisabled("(empty)");
        return;
    }

    const usize maxBytes = std::min(size, static_cast<usize>(128));
    char line[96];
    for (usize offset = 0; offset < maxBytes; offset += 16) {
        int pos = std::snprintf(line, sizeof(line), "%04zx: ", offset);
        const usize rowBytes = std::min(static_cast<usize>(16), maxBytes - offset);
        for (usize i = 0; i < rowBytes; ++i) {
            pos += std::snprintf(line + pos, sizeof(line) - pos, "%02X ", data[offset + i]);
        }
        ImGui::TextUnformatted(line);
    }
    if (size > maxBytes) {
        ImGui::TextDisabled("... (%zu more bytes)", size - maxBytes);
    }
}

void EntityDebuggerPanel::renderEntityHeader(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
    ImGui::Checkbox("Auto-refresh", &m_autoRefresh);
    ImGui::SameLine();
    ImGui::Checkbox("Manual ID", &m_useManualId);

    if (m_useManualId) {
        ImGui::SetNextItemWidth(120);
        ImGui::InputScalar("Entity ID", ImGuiDataType_U32, &m_manualEntityId);
    } else if (ctx.selectedEntity.isValid()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Using selection: %u", ctx.selectedEntity.id());
        if (ImGui::Button("Use Selected")) {
            m_manualEntityId = ctx.selectedEntity.id();
        }
    }

    if (!entity.isValid() || !world.isEntityAlive(entity)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No valid entity targeted");
        ImGui::TextDisabled("Select an entity in Hierarchy/Viewport or enter a valid ID.");
        return;
    }

    const char* name = getEntityName(world, entity);
    ImGui::Text("Entity: %u (%s)", entity.id(), name);
    ImGui::Text("World: %u entities, %u archetypes", world.entityCount(), world.archetypeCount());
}

void EntityDebuggerPanel::renderArchetypeInfo(ECS::World& world, ECS::Entity entity) {
    if (!entity.isValid() || !world.isEntityAlive(entity)) {
        return;
    }

    const auto loc = world.getEntityLocation(entity);
    ImGui::Separator();
    ImGui::Text("Archetype");
    ImGui::Text("  Index: %u", loc.archetypeIndex);
    ImGui::Text("  Slot:  %u", loc.indexInArchetype);
    ImGui::Text("  Hash:  0x%016llX", static_cast<unsigned long long>(m_archetypeHash));

    if (const ECS::Archetype* arch = world.getArchetype(loc.archetypeIndex)) {
        ImGui::Text("  Components in archetype:");
        ImGui::Indent();
        arch->getComponentSet().forEachComponentId([&](u32 componentId) {
            ImGui::BulletText("%s (id %u)", componentNameForId(componentId).c_str(), componentId);
        });
        ImGui::Unindent();
    }

    ImGui::Text("Memory (entity instance): %zu bytes", m_totalComponentBytes);
}

void EntityDebuggerPanel::renderComponentList() {
    ImGui::Separator();
    ImGui::Text("Components (%zu)", m_cachedComponents.size());

    if (m_cachedComponents.empty()) {
        ImGui::TextDisabled("No registered components on this entity.");
        return;
    }

    for (int i = 0; i < static_cast<int>(m_cachedComponents.size()); ++i) {
        const auto& comp = m_cachedComponents[i];
        ImGui::PushID(i);

        if (ImGui::TreeNodeEx(comp.name.c_str(),
                              ImGuiTreeNodeFlags_DefaultOpen * (i == 0 ? 1 : 0))) {
            ImGui::Text("Component ID: %u", comp.componentId);
            ImGui::Text("Size: %zu bytes", comp.sizeBytes);

            if (comp.name == "Transform" && comp.rawData) {
                const auto* t = static_cast<const ECS::Transform*>(comp.rawData);
                ImGui::Text("pos: [%.2f, %.2f, %.2f]", t->position.x, t->position.y, t->position.z);
            } else if (comp.name == "Position3D" && comp.rawData) {
                const auto* p = static_cast<const ECS::Position3D*>(comp.rawData);
                ImGui::Text("pos: [%.2f, %.2f, %.2f]", p->position.x, p->position.y, p->position.z);
            } else if (comp.name == "Sprite" && comp.rawData) {
                const auto* s = static_cast<const ECS::Sprite*>(comp.rawData);
                ImGui::Text("texture: %s", s->name.c_str());
            }

            ImGui::TextUnformatted("Hex dump:");
            renderMemoryHexView(static_cast<const u8*>(comp.rawData), comp.sizeBytes);

            if (!comp.matchingSystems.empty()) {
                ImGui::TextUnformatted("Matching systems:");
                for (const auto& sys : comp.matchingSystems) {
                    ImGui::BulletText("%s", sys.c_str());
                }
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

void EntityDebuggerPanel::renderSystemQueries(ECS::World& world) {
    if (m_cachedComponents.empty()) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("Active Systems (query match)");

    bool any = false;
    for (const auto& system : systemQueries()) {
        bool matches = true;
        for (usize i = 0; i < system.requiredCount; ++i) {
            const DebugComponentProbe* req = findProbeByName(system.requiredComponents[i]);
            if (!req || !req->has(world, m_targetEntity)) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;

        any = true;
        ImGui::BulletText("%s (priority %d)", system.name, system.priority);
    }

    if (!any) {
        ImGui::TextDisabled("No registered systems match this entity.");
    }
}

void EntityDebuggerPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    if (ImGui::Begin("Entity Debugger", &m_open)) {
        ECS::Entity target = resolveTargetEntity(world, ctx);

        if (m_autoRefresh || target != m_targetEntity) {
            m_targetEntity = target;
            refresh(world, m_targetEntity);
        }

        renderEntityHeader(world, m_targetEntity, ctx);
        renderArchetypeInfo(world, m_targetEntity);
        renderComponentList();
        renderSystemQueries(world);

        if (!m_autoRefresh && ImGui::Button("Refresh")) {
            m_targetEntity = resolveTargetEntity(world, ctx);
            refresh(world, m_targetEntity);
        }
    }
    ImGui::End();
}

}  // namespace Caffeine::Editor

#endif  // CF_HAS_IMGUI
