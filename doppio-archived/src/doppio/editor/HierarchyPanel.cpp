#include "editor/HierarchyPanel.hpp"
#include "caffeine/ui/UI.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/PrefabSystem.hpp"
#include "editor/DragDropSystem.hpp"
#include "ecs/PrefabComponents.hpp"
#include "ui/UIComponents.hpp"
#include "scene/HierarchySystem.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/SkyboxComponents.hpp"
#include "ecs/EnvironmentEffectsComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "render/RenderFeatures.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/PrefabComponents.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "audio/AudioComponents.hpp"
#include "animation/AnimationComponents.hpp"
#include "animation/AnimationPlayer.hpp"
#include "animation/SkinLibrary.hpp"
#include "effects/EffectTypes.hpp"
#include "navigation/NavVolume.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "script/ScriptTypes.hpp"
#include "ui/UIComponents.hpp"
#include "scene/LightingSystem.hpp"
#include "math/Quat.hpp"
#include "terrain/TerrainCache.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

HierarchyPanel::~HierarchyPanel() {
    delete m_renameDialog;
    delete m_placePrefabDialog;
}

namespace {

bool matchesFilter(const char* name, const char* filter) {
    if (!name || !filter || filter[0] == '\0') return true;
    for (const char* n = name; *n; ++n) {
        const char* nn = n;
        const char* fn = filter;
        while (*nn && *fn &&
               std::tolower(static_cast<unsigned char>(*nn)) ==
                   std::tolower(static_cast<unsigned char>(*fn))) {
            ++nn;
            ++fn;
        }
        if (*fn == '\0') return true;
    }
    return false;
}

}  // namespace

void HierarchyPanel::render(ECS::World& world, EditorContext& ctx) {
    m_world   = &world;
    m_context = &ctx;
    onImGuiRender();
}

void HierarchyPanel::onImGuiRender() {
    if (!m_open) return;
    if (!m_context || !m_world) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_renameDialog) {
        m_renameDialog = new Dialog("Rename Entity");
        m_renameDialog->setInitialSize(ImVec2(380.0f, 190.0f));
        m_renameDialog->setOnClose([this] { m_renaming = ECS::Entity::INVALID; });
    }
    if (!m_placePrefabDialog) {
        m_placePrefabDialog = new Dialog("Place Prefab");
        m_placePrefabDialog->setInitialSize(ImVec2(460.0f, 230.0f));
        m_placePrefabDialog->setOnClose([this] { m_prefabError.clear(); });
    }

    Panel root("Hierarchy", "Scene entities", "arrows-vertical", &m_open);
    root.setOnBegin([this] { handleDeleteKey(); });
    UIContext ui;

    auto& search = root.emplace<Elements::SearchField>(
        m_searchFilter, sizeof(m_searchFilter), "Search entities...");
    search.setGrow(true);

    auto& toolbar = root.emplace<Toolbar>("hierarchy_toolbar");
    renderToolbar(toolbar);

    if (m_openCreateMenu) {
        ImGui::OpenPopup("hierarchy_create_entity");
        m_openCreateMenu = false;
    }
    if (ImGui::BeginPopup("hierarchy_create_entity")) {
        renderCreateEntityMenuItems();
        ImGui::EndPopup();
    }

    root.emplace<Separator>();
    auto& tree = root.emplace<TreeView>("hierarchy_tree");
    tree.setHeight(0.0f);

    ECS::ComponentQuery allQ;
    const bool filtering = m_searchFilter[0] != '\0';
    m_world->forEach<NameComponent>(allQ, [&](ECS::Entity entity, NameComponent&) {
        if (filtering) {
            if (matchesFilter(getEntityName(*m_world, entity), m_searchFilter)) {
                addEntityItem(tree, entity, false);
            }
            return;
        }
        bool isRoot = true;
        if (auto* parent = m_world->get<Scene::Parent>(entity)) {
            if (parent->parent.isValid()) isRoot = false;
        }
        if (isRoot) addEntityItem(tree, entity, true);
    });

    tree.setOnEmptyContextMenu([this] {
        if (ImGui::BeginMenu("Create Entity")) {
            renderCreateEntityMenuItems();
            ImGui::EndMenu();
        }
        if (m_context->clipboardEntity.isValid() &&
            ImGui::MenuItem("Paste\tCtrl+V")) {
            duplicateEntity(*m_world, m_context->clipboardEntity);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Place Prefab...")) openPlacePrefab();
    });

    ui.draw(root);
    renderDialogs();
}

void HierarchyPanel::renderToolbar(Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit::Elements;
    auto& create = parent.emplace<Button>("+", ButtonVariant::Toolbar);
    create.setTooltip("Create entity");
    create.setOnClick([this] { m_openCreateMenu = true; });
    auto& presets = parent.emplace<Button>("Presets", ButtonVariant::Toolbar);
    presets.setTooltip("Open Entity Presets wizard");
    presets.setOnClick([this] {
        if (m_openPresets) m_openPresets();
    });
    auto& remove = parent.emplace<Button>("Delete", ButtonVariant::Toolbar);
    remove.setTooltip("Delete selected entities");
    remove.setOnClick([this] { deleteSelected(); });
}

void HierarchyPanel::handleDeleteKey() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete)) return;
    deleteSelected();
}

void HierarchyPanel::deleteSelected() {
    if (!m_context || !m_world) return;
    if (m_context->hasMultiSelection()) {
        m_context->beginUndo(EditorCommand::RemoveEntity, u32_max, *m_world);
        for (auto& entity : m_context->selectedEntities) {
            if (entity.isValid()) m_world->destroy(entity);
        }
        m_context->clearSelection();
        m_context->endUndo(*m_world);
    } else if (m_context->selectedEntity.isValid()) {
        m_context->beginUndo(EditorCommand::RemoveEntity,
                             m_context->selectedEntity.id(), *m_world);
        m_world->destroy(m_context->selectedEntity);
        m_context->clearSelection();
        m_context->endUndo(*m_world);
    }
}

void HierarchyPanel::addEntityItem(Caffeine::UIKit::Elements::Container& parent,
                                   ECS::Entity entity, bool recurse) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    if (!entity.isValid() || !m_context || !m_world) return;

    const char* name = getEntityName(*m_world, entity);
    const bool effectivelyDisabled = Scene::isEffectivelyDisabled(*m_world, entity);
    const bool prefabLinked = PrefabSystem::FindInstanceRoot(*m_world, entity).isValid();
    std::string label = name;
    if (prefabLinked) label = "[P] " + label;
    const bool childExists = hasChildren(entity);

    auto& item = parent.emplace<TreeItem>(label, false);
    item.setId("entity:" + std::to_string(entity.id()));
    item.setOpenOnArrow(true);
    item.setSelected(m_context->isSelected(entity));
    if (entity == m_expandEntity) {
        item.setForceOpen(true);
        m_expandEntity = ECS::Entity::INVALID;
    }
    if (m_context->selectedEntity == entity && entity != m_lastScrollTarget) {
        item.setScrollHere(true);
        m_lastScrollTarget = entity;
    }
    if (effectivelyDisabled) {
        item.setTextColor(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    } else if (prefabLinked) {
        item.setTextColor(ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
    }

    item.setOnClick([this, entity] {
        if (ImGui::GetIO().KeyCtrl) {
            m_context->toggleSelection(entity);
        } else if (ImGui::GetIO().KeyShift) {
            m_context->addToSelection(entity);
        } else {
            m_context->selectEntity(entity);
        }
    });
    item.setOnDrag([this, entity] {
        u32 id = entity.id();
        ImGui::SetDragDropPayload("ENTITY_DRAG", &id, sizeof(u32));
        ImGui::Text("%s", getEntityName(*m_world, entity));
    });
    item.setOnDrop([this, entity] { handleEntityDrop(entity); });
    item.setOnContextMenu([this, entity] { renderEntityContextMenu(entity); });

    if (recurse && childExists) {
        ECS::ComponentQuery childQ;
        childQ.with<Scene::Parent>();
        m_world->forEach<Scene::Parent>(childQ, [&](ECS::Entity child, Scene::Parent& link) {
            if (link.parent == entity) addEntityItem(item, child, true);
        });
    }
}

void HierarchyPanel::handleEntityDrop(ECS::Entity entity) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadAssetPath)) {
        const auto* asset = static_cast<const AssetDropPayload*>(payload->Data);
        if (asset->type == AssetType::Prefab) {
            m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
            ECS::Entity root = PrefabSystem::Instantiate(*m_world, asset->path);
            if (root.isValid()) {
                auto& parentComp = m_world->add<Scene::Parent>(root);
                parentComp.parent = entity;
                parentComp.dirty = true;
                m_context->selectEntity(root);
                m_expandEntity = entity;
            }
            m_context->endUndo(*m_world);
        }

        const std::filesystem::path droppedPath(asset->path);
        const std::string extension = droppedPath.extension().string();
        if (extension == ".lua" || extension == ".dyd") {
            std::filesystem::path stored = droppedPath;
            if (!m_context->projectRootPath.empty() && droppedPath.is_absolute()) {
                const std::filesystem::path relative =
                    droppedPath.lexically_relative(m_context->projectRootPath);
                if (!relative.empty() && relative.native().rfind("..", 0) != 0) {
                    stored = relative;
                }
            }
            m_context->beginUndo(EditorCommand::SetField, entity.id(), *m_world);
            if (auto* script = m_world->get<Script::ScriptComponent>(entity)) {
                script->scriptPath = stored.generic_string();
            } else {
                Script::ScriptComponent component;
                component.scriptPath = stored.generic_string();
                m_world->add<Script::ScriptComponent>(entity, component);
            }
            m_context->markDirty();
            m_context->endUndo(*m_world);
            if (extension == ".dyd") m_context->dryadToOpen = stored.generic_string();
        }
    } else if (const ImGuiPayload* payload =
                   ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
        ECS::Entity dragged(*static_cast<const u32*>(payload->Data), m_world);
        if (dragged != entity) {
            m_context->beginUndo(EditorCommand::MoveEntity, dragged.id(), *m_world);
            auto& parentComp = m_world->add<Scene::Parent>(dragged);
            parentComp.parent = entity;
            parentComp.dirty = true;
            m_expandEntity = entity;
            m_context->endUndo(*m_world);
        }
    }
}

void HierarchyPanel::renderEntityContextMenu(ECS::Entity entity) {
    if (ImGui::MenuItem("Rename")) openRename(entity);
    if (ImGui::MenuItem("Duplicate\tCtrl+D")) duplicateEntity(*m_world, entity);
    if (ImGui::MenuItem("Copy\tCtrl+C")) m_context->clipboardEntity = entity;
    ImGui::Separator();
    if (ImGui::MenuItem("Create Child")) {
        m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
        ECS::Entity child = m_world->create();
        setEntityName(*m_world, child, "Child");
        m_world->add<ECS::Transform>(child);
        auto& parentComp = m_world->add<Scene::Parent>(child);
        parentComp.parent = entity;
        parentComp.dirty = true;
        m_expandEntity = entity;
        m_context->selectEntity(child);
        m_context->endUndo(*m_world);
    }
    if (auto* pc = m_world->get<Scene::Parent>(entity)) {
        if (pc->parent.isValid() && ImGui::MenuItem("Unparent")) {
            m_context->beginUndo(EditorCommand::MoveEntity, entity.id(), *m_world);
            m_world->remove<Scene::Parent>(entity);
            m_context->endUndo(*m_world);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete")) {
        m_context->beginUndo(EditorCommand::RemoveEntity, entity.id(), *m_world);
        m_world->destroy(entity);
        if (m_context->selectedEntity == entity) m_context->clearSelection();
        m_context->endUndo(*m_world);
    }
}

void HierarchyPanel::openRename(ECS::Entity entity) {
    m_renaming = entity;
    const char* name = getEntityName(*m_world, entity);
    std::strncpy(m_renameBuffer, name, sizeof(m_renameBuffer) - 1);
    m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
    m_renameFocus = true;
    if (m_renameDialog) m_renameDialog->open();
}

void HierarchyPanel::applyRename() {
    if (!m_renaming.isValid() || !m_context || !m_world) return;
    m_context->beginUndo(EditorCommand::SetField, m_renaming.id(), *m_world);
    setEntityName(*m_world, m_renaming, m_renameBuffer);
    m_context->markDirty();
    m_context->endUndo(*m_world);
    m_renaming = ECS::Entity::INVALID;
    if (m_renameDialog) m_renameDialog->close();
}

void HierarchyPanel::openPlacePrefab() {
    m_prefabError.clear();
    m_prefabFocus = true;
    if (m_placePrefabDialog) m_placePrefabDialog->open();
}

void HierarchyPanel::loadPrefab() {
    if (!m_context || !m_world) return;
    if (m_prefabPath[0] == '\0') {
        m_prefabError = "Path cannot be empty";
        return;
    }
    m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
    ECS::Entity root = PrefabSystem::Instantiate(*m_world, m_prefabPath);
    if (!root.isValid()) {
        m_context->endUndo(*m_world);
        m_prefabError = "Failed to load prefab file";
        return;
    }
    m_context->selectEntity(root);
    m_context->endUndo(*m_world);
    m_prefabPath[0] = '\0';
    m_prefabError.clear();
    if (m_placePrefabDialog) m_placePrefabDialog->close();
}

void HierarchyPanel::renderDialogs() {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (m_renameDialog) {
        if (m_renameDialog->isOpen()) {
            m_renameDialog->clear();
            if (m_renaming.isValid()) {
                char title[160];
                std::snprintf(title, sizeof(title), "Rename %s",
                              getEntityName(*m_world, m_renaming));
                m_renameDialog->emplace<Label>(title, TextTone::Accent).setHeading(true);
                auto& input = m_renameDialog->emplace<TextInput>(
                    m_renameBuffer, sizeof(m_renameBuffer));
                if (m_renameFocus) {
                    input.focusNextFrame();
                    m_renameFocus = false;
                }
                auto& row = m_renameDialog->emplace<Row>();
                row.emplace<Button>("Rename", ButtonVariant::Primary)
                    .setOnClick([this] { applyRename(); });
                row.emplace<Button>("Cancel")
                    .setOnClick([this] { m_renameDialog->close(); });
            }
        }
        UIContext ui;
        ui.draw(*m_renameDialog);
    }

    if (m_placePrefabDialog) {
        if (m_placePrefabDialog->isOpen()) {
            m_placePrefabDialog->clear();
            m_placePrefabDialog->emplace<Label>("Place prefab", TextTone::Accent)
                .setHeading(true);
            m_placePrefabDialog->emplace<Label>(
                "Enter the prefab asset path:", TextTone::Muted);
            auto& input = m_placePrefabDialog->emplace<TextInput>(
                m_prefabPath, sizeof(m_prefabPath));
            if (m_prefabFocus) {
                input.focusNextFrame();
                m_prefabFocus = false;
            }
            if (!m_prefabError.empty()) {
                m_placePrefabDialog->emplace<Label>(m_prefabError, TextTone::Danger);
            }
            auto& row = m_placePrefabDialog->emplace<Row>();
            row.emplace<Button>("Load", ButtonVariant::Primary)
                .setOnClick([this] { loadPrefab(); });
            row.emplace<Button>("Cancel")
                .setOnClick([this] { m_placePrefabDialog->close(); });
        }
        UIContext ui;
        ui.draw(*m_placePrefabDialog);
    }
}

namespace {

template<typename T>
void copyComponent(ECS::World& world, ECS::Entity src, ECS::Entity dst) {
    if (const T* component = world.get<T>(src)) {
        world.add<T>(dst, *component);
    }
}

ECS::Entity duplicateEntityTree(ECS::World& world, ECS::Entity src, ECS::Entity newParent,
                                bool offsetRoot) {
    ECS::Entity dst = world.create();
    char newName[128];
    std::snprintf(newName, sizeof(newName), "%s (Copy)", getEntityName(world, src));
    setEntityName(world, dst, newName);

    copyComponent<ECS::Transform>(world, src, dst);
    copyComponent<ECS::Sprite>(world, src, dst);
    copyComponent<ECS::Tag>(world, src, dst);
    copyComponent<ECS::Acceleration2D>(world, src, dst);
    copyComponent<ECS::Position3D>(world, src, dst);
    copyComponent<ECS::Rotation3D>(world, src, dst);
    copyComponent<ECS::Scale3D>(world, src, dst);
    copyComponent<ECS::MeshFilterComponent>(world, src, dst);
    copyComponent<ECS::MeshRendererComponent>(world, src, dst);
    copyComponent<ECS::MeshGeometryComponent>(world, src, dst);
    copyComponent<ECS::SkinnedMeshRendererComponent>(world, src, dst);
    copyComponent<ECS::LightComponent>(world, src, dst);
    copyComponent<ECS::DirectionalLightComponent>(world, src, dst);
    copyComponent<ECS::PointLightComponent>(world, src, dst);
    copyComponent<ECS::SpotLightComponent>(world, src, dst);
    copyComponent<ECS::Camera2DComponent>(world, src, dst);
    copyComponent<ECS::Camera3DComponent>(world, src, dst);
    copyComponent<ECS::CameraActiveComponent>(world, src, dst);
    copyComponent<ECS::Camera3DControllerComponent>(world, src, dst);
    copyComponent<Physics2D::RigidBody2D>(world, src, dst);
    copyComponent<Physics2D::Collider2D>(world, src, dst);
    copyComponent<Physics3D::RigidBody3D>(world, src, dst);
    copyComponent<Physics3D::Collider3D>(world, src, dst);
    copyComponent<Audio::AudioEmitter>(world, src, dst);
    copyComponent<Script::ScriptComponent>(world, src, dst);
    copyComponent<Script::CppScriptComponent>(world, src, dst);
    copyComponent<ECS::PersistentComponent>(world, src, dst);
    copyComponent<ECS::DisabledTag>(world, src, dst);
    copyComponent<ECS::ParticleEmitterComponent>(world, src, dst);
    copyComponent<Scene::EntityLayer>(world, src, dst);
    copyComponent<UI::UIWidget>(world, src, dst);
    copyComponent<UI::UIButton>(world, src, dst);
    copyComponent<UI::UILabel>(world, src, dst);
    copyComponent<UI::UIProgressBar>(world, src, dst);
    copyComponent<UI::UISlider>(world, src, dst);
    copyComponent<UI::UICheckbox>(world, src, dst);
    copyComponent<Animation::Animator>(world, src, dst);
    copyComponent<Animation::AnimationPlayer>(world, src, dst);
    copyComponent<Animation::SpriteSheet>(world, src, dst);
    copyComponent<Animation::SkinnedPose>(world, src, dst);
    copyComponent<ECS::SkyboxComponent>(world, src, dst);
    copyComponent<ECS::TerrainComponent>(world, src, dst);
    copyComponent<ECS::ForwardRenderFeaturesComponent>(world, src, dst);
    copyComponent<ECS::PostProcessComponent>(world, src, dst);
    copyComponent<Navigation::NavVolume>(world, src, dst);
    copyComponent<Navigation::NavAgent>(world, src, dst);
    copyComponent<Effects::EffectComponent>(world, src, dst);
    copyComponent<ECS::EnvironmentEffectsComponent>(world, src, dst);
    copyComponent<ECS::ProceduralWorldComponent>(world, src, dst);
    copyComponent<ECS::PrefabInstance>(world, src, dst);

    if (offsetRoot) {
        if (auto* p = world.get<ECS::Position3D>(dst)) p->position.x += 1.0f;
        if (auto* t = world.get<ECS::Transform>(dst)) t->position.x += 1.0f;
    }

    if (newParent.isValid()) {
        world.add<Scene::Parent>(dst).parent = newParent;
    } else if (const Scene::Parent* parent = world.get<Scene::Parent>(src)) {
        world.add<Scene::Parent>(dst, *parent);
    }

    ECS::ComponentQuery children;
    children.with<Scene::Parent>();
    std::vector<ECS::Entity> kids;
    world.forEach<Scene::Parent>(children, [&](ECS::Entity child, Scene::Parent& link) {
        if (link.parent == src) kids.push_back(child);
    });
    for (ECS::Entity child : kids) {
        duplicateEntityTree(world, child, dst, false);
    }
    return dst;
}

}  // namespace

void HierarchyPanel::duplicateEntity(ECS::World& world, ECS::Entity src) {
    if (!src.isValid()) return;
    m_context->beginUndo(EditorCommand::AddEntity, u32_max, world);
    const ECS::Entity dst = duplicateEntityTree(world, src, ECS::Entity::INVALID, true);
    m_context->selectEntity(dst);
    m_context->markDirty();
    m_context->endUndo(world);
}

void HierarchyPanel::createEntityWithType(ECS::World& world, const char* name, const char* componentType) {
    m_context->beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity e = world.create();
    setEntityName(world, e, name);

    if      (strcmp(componentType, "Camera2D") == 0) {
        world.add<ECS::Camera2DComponent>(e);
    }
    else if (strcmp(componentType, "Camera3D") == 0) {
        world.add<ECS::Camera3DComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    }
    else if (strcmp(componentType, "DirectionalLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::DirectionalLightComponent>(e);
        world.add<ECS::Rotation3D>(e);
        Scene::applyDefaultSunOrientation(world, e);
    }
    else if (strcmp(componentType, "PointLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::PointLightComponent>(e);
        world.add<ECS::Position3D>(e);
    }
    else if (strcmp(componentType, "SpotLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::SpotLightComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
    }
    else if (strcmp(componentType, "MeshRenderer") == 0) {
        world.add<ECS::MeshRendererComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    }
    else if (strcmp(componentType, "Sprite2D") == 0) {
        world.add<ECS::Transform>(e);
        world.add<ECS::Sprite>(e);
    }
    else if (strcmp(componentType, "Sprite2DBox") == 0) {
        world.add<ECS::Transform>(e);
        world.add<ECS::Sprite>(e);
        world.add<Physics2D::RigidBody2D>(e);
        Physics2D::Collider2D col;
        col.shape = Physics2D::ColliderShape::AABB;
        col.size  = { 64.0f, 64.0f };
        world.add<Physics2D::Collider2D>(e, col);
    }
    else if (strcmp(componentType, "Sprite2DCircle") == 0) {
        world.add<ECS::Transform>(e);
        world.add<ECS::Sprite>(e);
        world.add<Physics2D::RigidBody2D>(e);
        Physics2D::Collider2D col;
        col.shape  = Physics2D::ColliderShape::Circle;
        col.radius = 32.0f;
        world.add<Physics2D::Collider2D>(e, col);
    }
    else if (strcmp(componentType, "Sprite2DCapsule") == 0) {
        world.add<ECS::Transform>(e);
        world.add<ECS::Sprite>(e);
        world.add<Physics2D::RigidBody2D>(e);
        Physics2D::Collider2D col;
        col.shape  = Physics2D::ColliderShape::Circle;
        col.radius = 24.0f;
        col.size   = { 48.0f, 96.0f };
        world.add<Physics2D::Collider2D>(e, col);
    }
    else if (strcmp(componentType, "Cube3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Cube;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Sphere3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Sphere;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Capsule3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Capsule;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Cylinder3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Cylinder;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Plane3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Plane;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Cone3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Cone;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Pyramid3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Pyramid;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Torus3D") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        ECS::MeshFilterComponent mf;
        mf.primitive = ECS::MeshPrimitive::Torus;
        world.add<ECS::MeshFilterComponent>(e, mf);
        world.add<ECS::MeshRendererComponent>(e);
    }
    else if (strcmp(componentType, "Skybox") == 0) {
        ECS::SkyboxComponent sky;
        sky.presetIndex = 0;
        sky.enabled = true;
        std::strncpy(sky.customTexturePath, ECS::kDefaultProjectSkyPath, sizeof(sky.customTexturePath) - 1);
        world.add<ECS::SkyboxComponent>(e, sky);
        world.add<ECS::PersistentComponent>(e);
    }
    else if (strcmp(componentType, "EnvironmentEffects") == 0) {
        world.add<ECS::EnvironmentEffectsComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Transform>(e);
        world.add<ECS::PersistentComponent>(e);
        auto& features = world.add<ECS::ForwardRenderFeaturesComponent>(e);
        features.ibl.enabled = false;
        features.occlusion.enabled = true;
        features.volumetrics.enabled = true;
        features.volumetrics.quality = Render::VolumetricQuality::High;
        features.volumetrics.sampleShadows = true;
        features.volumetrics.density = 0.045f;
        features.volumetrics.height = 80.0f;
        features.volumetrics.anisotropy = 0.62f;
        auto& post = world.add<ECS::PostProcessComponent>(e);
        post.ambientOcclusion.enabled = true;
        post.ambientOcclusion.intensity = 1.1f;
        post.ambientOcclusion.radius = 0.55f;
    }
    else if (strcmp(componentType, "Terrain") == 0) {
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
        world.add<ECS::TerrainComponent>(e);
        Terrain::TerrainCache::instance().initializeEntity(world, e);
    }
    else if (strcmp(componentType, "GameManager") == 0) {
        world.add<ECS::PersistentComponent>(e);
    }
    else if (strcmp(componentType, "UICanvas") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::Canvas;
        w.computedRect = { {0.0f, 0.0f}, {1280.0f, 720.0f} };
        world.add<UI::UIWidget>(e, w);
    }
    else if (strcmp(componentType, "UIPanel") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::Panel;
        w.transform.offsetMax = { 200.0f, 100.0f };
        world.add<UI::UIWidget>(e, w);
    }
    else if (strcmp(componentType, "UILabel") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::Label;
        w.interactable = false;
        w.transform.offsetMax = { 200.0f, 30.0f };
        world.add<UI::UIWidget>(e, w);
        UI::UILabel lbl;
        lbl.text = "Label";
        world.add<UI::UILabel>(e, lbl);
    }
    else if (strcmp(componentType, "UIButton") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::Button;
        w.transform.offsetMax = { 120.0f, 40.0f };
        world.add<UI::UIWidget>(e, w);
        UI::UIButton btn;
        btn.labelText = "Button";
        world.add<UI::UIButton>(e, btn);
    }
    else if (strcmp(componentType, "UIProgressBar") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::ProgressBar;
        w.interactable = false;
        w.transform.offsetMax = { 200.0f, 20.0f };
        world.add<UI::UIWidget>(e, w);
        world.add<UI::UIProgressBar>(e);
    }
    else if (strcmp(componentType, "UISlider") == 0) {
        UI::UIWidget w;
        w.type = UI::UIWidgetType::Slider;
        w.transform.offsetMax = { 200.0f, 20.0f };
        world.add<UI::UIWidget>(e, w);
        world.add<UI::UISlider>(e);
    }

    m_context->selectEntity(e);
    m_context->endUndo(world);
}

void HierarchyPanel::renderCreateEntityMenuItems() {
    if (ImGui::MenuItem("Empty Entity")) {
        m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
        ECS::Entity e = m_world->create();
        setEntityName(*m_world, e, "New Entity");
        m_world->add<ECS::Transform>(e);
        m_context->selectEntity(e);
        m_context->endUndo(*m_world);
    }
    ImGui::Separator();

    if (ImGui::BeginMenu("2D Objects")) {
        if (ImGui::MenuItem("Sprite"))                    createEntityWithType(*m_world, "Sprite",            "Sprite2D");
        if (ImGui::MenuItem("Sprite (Box Collider)"))     createEntityWithType(*m_world, "Sprite",            "Sprite2DBox");
        if (ImGui::MenuItem("Sprite (Circle Collider)"))  createEntityWithType(*m_world, "Sprite",            "Sprite2DCircle");
        if (ImGui::MenuItem("Sprite (Capsule Collider)")) createEntityWithType(*m_world, "Sprite",            "Sprite2DCapsule");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("3D Objects")) {
        if (ImGui::MenuItem("Cube"))      createEntityWithType(*m_world, "Cube",      "Cube3D");
        if (ImGui::MenuItem("Sphere"))    createEntityWithType(*m_world, "Sphere",    "Sphere3D");
        if (ImGui::MenuItem("Capsule"))   createEntityWithType(*m_world, "Capsule",   "Capsule3D");
        if (ImGui::MenuItem("Cylinder"))  createEntityWithType(*m_world, "Cylinder",  "Cylinder3D");
        if (ImGui::MenuItem("Plane"))     createEntityWithType(*m_world, "Plane",     "Plane3D");
        if (ImGui::MenuItem("Cone"))      createEntityWithType(*m_world, "Cone",      "Cone3D");
        if (ImGui::MenuItem("Pyramid"))   createEntityWithType(*m_world, "Pyramid",   "Pyramid3D");
        if (ImGui::MenuItem("Torus"))     createEntityWithType(*m_world, "Torus",     "Torus3D");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Camera")) {
        if (ImGui::MenuItem("Camera 2D")) createEntityWithType(*m_world, "Camera 2D", "Camera2D");
        if (ImGui::MenuItem("Camera 3D")) createEntityWithType(*m_world, "Camera 3D", "Camera3D");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Light")) {
        if (ImGui::MenuItem("Directional Light")) createEntityWithType(*m_world, "Directional Light", "DirectionalLight");
        if (ImGui::MenuItem("Point Light"))       createEntityWithType(*m_world, "Point Light",       "PointLight");
        if (ImGui::MenuItem("Spot Light"))        createEntityWithType(*m_world, "Spot Light",        "SpotLight");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Environment")) {
        if (ImGui::MenuItem("Skybox"))  createEntityWithType(*m_world, "Skybox",  "Skybox");
        if (ImGui::MenuItem("Environment Effects"))
            createEntityWithType(*m_world, "Environment Effects", "EnvironmentEffects");
        if (ImGui::MenuItem("Terrain")) createEntityWithType(*m_world, "Terrain", "Terrain");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas"))       createEntityWithType(*m_world, "Canvas",       "UICanvas");
        if (ImGui::MenuItem("Panel"))        createEntityWithType(*m_world, "Panel",        "UIPanel");
        if (ImGui::MenuItem("Label"))        createEntityWithType(*m_world, "Label",        "UILabel");
        if (ImGui::MenuItem("Button"))       createEntityWithType(*m_world, "Button",       "UIButton");
        if (ImGui::MenuItem("Progress Bar")) createEntityWithType(*m_world, "Progress Bar", "UIProgressBar");
        if (ImGui::MenuItem("Slider"))       createEntityWithType(*m_world, "Slider",       "UISlider");
        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("System")) {
        if (ImGui::MenuItem("Game Manager")) createEntityWithType(*m_world, "Game Manager", "GameManager");
        ImGui::EndMenu();
    }
}

bool HierarchyPanel::hasChildren(ECS::Entity entity) const {
    bool found = false;
    ECS::ComponentQuery childQ;
    childQ.with<Scene::Parent>();
    m_world->forEach<Scene::Parent>(childQ, [&](ECS::Entity, Scene::Parent& p) {
        if (p.parent == entity) found = true;
    });
    return found;
}


} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
