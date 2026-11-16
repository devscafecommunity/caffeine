#include "caffeine/ui/UI.hpp"
#include "editor/EntityDebugger.hpp"
#include "scene/EditorContext.hpp"
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
#include <limits>
#include <string>
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

std::string EntityDebuggerPanel::memoryHexDump(const u8* data, usize size) const {
    if (!data || size == 0) return "(empty)";
    const usize maxBytes = std::min(size, static_cast<usize>(128));
    std::string out;
    char line[96];
    for (usize offset = 0; offset < maxBytes; offset += 16) {
        int pos = std::snprintf(line, sizeof(line), "%04zx: ", offset);
        const usize rowBytes = std::min(static_cast<usize>(16), maxBytes - offset);
        for (usize i = 0; i < rowBytes; ++i) {
            pos += std::snprintf(line + pos, sizeof(line) - pos, "%02X ",
                                 data[offset + i]);
        }
        out += line;
        out += '\n';
    }
    if (size > maxBytes) {
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), "... (%zu more bytes)\n", size - maxBytes);
        out += suffix;
    }
    return out;
}

void EntityDebuggerPanel::buildHeader(Caffeine::UIKit::Elements::Container& parent,
                                      ECS::World& world, ECS::Entity entity,
                                      EditorContext& ctx) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("debug_header");
    card.emplace<Label>("Target", TextTone::Accent).setHeading(true);
    auto& toggles = card.emplace<Row>("debug_toggles");
    toggles.emplace<Checkbox>("Auto-refresh", &m_autoRefresh);
    toggles.emplace<Checkbox>("Manual ID", &m_useManualId);

    if (m_useManualId) {
        m_manualEntityIdEdit = static_cast<int>(
            std::min<u32>(m_manualEntityId, static_cast<u32>(std::numeric_limits<int>::max())));
        auto& grid = card.emplace<PropertyGrid>("debug_manual_id");
        grid.typedRow<IntProperty>("Entity ID", m_manualEntityIdEdit, 0,
                                   std::numeric_limits<int>::max(), 1.0f, "%d")
            .setOnChange([this](int value) {
                m_manualEntityId = static_cast<u32>(std::max(value, 0));
            });
    } else if (ctx.selectedEntity.isValid()) {
        card.emplace<Label>("Using selection: " + std::to_string(ctx.selectedEntity.id()),
                            TextTone::Muted);
        card.emplace<Button>("Use Selected").setOnClick([this, id = ctx.selectedEntity.id()] {
            m_manualEntityId = id;
            m_manualEntityIdEdit = static_cast<int>(
                std::min<u32>(id, static_cast<u32>(std::numeric_limits<int>::max())));
        });
    }

    if (!entity.isValid() || !world.isEntityAlive(entity)) {
        card.emplace<Label>("No valid entity targeted", TextTone::Danger);
        card.emplace<Label>("Select an entity in Hierarchy/Viewport or enter a valid ID.",
                            TextTone::Muted);
        return;
    }

    card.emplace<Label>("Entity: " + std::to_string(entity.id()) + " (" +
                            getEntityName(world, entity) + ")",
                        TextTone::Normal)
        .setHeading(false);
    card.emplace<Label>("World: " + std::to_string(world.entityCount()) + " entities, " +
                            std::to_string(world.archetypeCount()) + " archetypes",
                        TextTone::Muted);
}

void EntityDebuggerPanel::buildArchetype(Caffeine::UIKit::Elements::Container& parent,
                                         ECS::World& world, ECS::Entity entity) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    if (!entity.isValid() || !world.isEntityAlive(entity)) return;

    const auto loc = world.getEntityLocation(entity);
    auto& card = parent.emplace<Card>("debug_archetype");
    card.emplace<Label>("Archetype", TextTone::Accent).setHeading(true);
    card.emplace<KeyValue>("Index", std::to_string(loc.archetypeIndex));
    card.emplace<KeyValue>("Slot", std::to_string(loc.indexInArchetype));
    char hash[32];
    std::snprintf(hash, sizeof(hash), "0x%016llX",
                  static_cast<unsigned long long>(m_archetypeHash));
    card.emplace<KeyValue>("Hash", hash);
    card.emplace<KeyValue>("Memory", std::to_string(m_totalComponentBytes) + " bytes");

    if (const ECS::Archetype* arch = world.getArchetype(loc.archetypeIndex)) {
        card.emplace<Label>("Components in archetype", TextTone::Muted);
        arch->getComponentSet().forEachComponentId([&](u32 componentId) {
            card.emplace<Label>("- " + componentNameForId(componentId) + " (id " +
                                    std::to_string(componentId) + ")",
                                TextTone::Muted);
        });
    }
}

void EntityDebuggerPanel::buildComponents(Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("debug_components");
    card.emplace<Label>("Components (" + std::to_string(m_cachedComponents.size()) + ")",
                        TextTone::Accent)
        .setHeading(true);
    if (m_cachedComponents.empty()) {
        card.emplace<Label>("No registered components on this entity.", TextTone::Muted);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_cachedComponents.size()); ++i) {
        const auto& comp = m_cachedComponents[i];
        auto& item = card.emplace<TreeItem>(comp.name, i == 0);
        item.setId("component:" + comp.name);
        item.emplace<Label>("Component ID: " + std::to_string(comp.componentId),
                            TextTone::Muted);
        item.emplace<Label>("Size: " + std::to_string(comp.sizeBytes) + " bytes",
                            TextTone::Muted);

        if (comp.name == "Transform" && comp.rawData) {
            const auto* t = static_cast<const ECS::Transform*>(comp.rawData);
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "pos: [%.2f, %.2f, %.2f]", t->position.x,
                          t->position.y, t->position.z);
            item.emplace<Label>(buffer, TextTone::Normal);
        } else if (comp.name == "Position3D" && comp.rawData) {
            const auto* p = static_cast<const ECS::Position3D*>(comp.rawData);
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "pos: [%.2f, %.2f, %.2f]", p->position.x,
                          p->position.y, p->position.z);
            item.emplace<Label>(buffer, TextTone::Normal);
        } else if (comp.name == "Sprite" && comp.rawData) {
            const auto* s = static_cast<const ECS::Sprite*>(comp.rawData);
            item.emplace<Label>("texture: " + s->name, TextTone::Normal);
        }

        item.emplace<Label>("Hex dump:", TextTone::Muted);
        item.emplace<Label>(memoryHexDump(static_cast<const u8*>(comp.rawData),
                                          comp.sizeBytes),
                            TextTone::Disabled);

        if (!comp.matchingSystems.empty()) {
            item.emplace<Label>("Matching systems:", TextTone::Muted);
            for (const auto& system : comp.matchingSystems) {
                item.emplace<Label>("- " + system, TextTone::Muted);
            }
        }
    }
}

void EntityDebuggerPanel::buildSystems(Caffeine::UIKit::Elements::Container& parent,
                                       ECS::World& world) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    if (m_cachedComponents.empty()) return;

    auto& card = parent.emplace<Card>("debug_systems");
    card.emplace<Label>("Active Systems (query match)", TextTone::Accent).setHeading(true);
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
        card.emplace<Label>("- " + std::string(system.name) + " (priority " +
                                std::to_string(system.priority) + ")",
                            TextTone::Normal);
    }
    if (!any) {
        card.emplace<Label>("No registered systems match this entity.", TextTone::Muted);
    }
}

void EntityDebuggerPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Entity Debugger", "Inspect ECS entity internals", "account", &m_open);
    UIContext ui;

    ECS::Entity target = resolveTargetEntity(world, ctx);
    if (m_autoRefresh || target != m_targetEntity) {
        m_targetEntity = target;
        refresh(world, m_targetEntity);
    }

    buildHeader(root, world, m_targetEntity, ctx);
    buildArchetype(root, world, m_targetEntity);
    buildComponents(root);
    buildSystems(root, world);
    if (!m_autoRefresh) {
        root.emplace<Button>("Refresh").setOnClick([this, &world, &ctx] {
            m_targetEntity = resolveTargetEntity(world, ctx);
            refresh(world, m_targetEntity);
        });
    }

    ui.draw(root);
}

}  // namespace Caffeine::Editor

#endif  // CF_HAS_IMGUI
