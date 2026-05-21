#include "editor/HierarchyPanel.hpp"
#include "ui/UIComponents.hpp"
#include <cctype>
#include <cstdio>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void HierarchyPanel::render(ECS::World& world, EditorContext& ctx) {
    m_world   = &world;
    m_context = &ctx;
    onImGuiRender();
}

void HierarchyPanel::onImGuiRender() {
    if (!m_open) return;
    if (!m_context || !m_world) return;

    ImGui::Begin("Hierarchy", &m_open);

    renderSearchBar();
    renderToolbar();

    ImGui::Separator();

    if (ImGui::BeginChild("entity_list")) {
        handleDeleteKey();

        m_entityCount = 0;

        ECS::ComponentQuery allQ;
        bool hasFilter = (m_searchFilter[0] != '\0');

        m_world->forEach<NameComponent>(allQ, [&](ECS::Entity e, NameComponent&) {
            if (m_entityCount >= MAX_VISIBLE) return;

            bool isRoot = true;
            if (auto* parent = m_world->get<Scene::Parent>(e)) {
                if (parent->parent.isValid()) isRoot = false;
            }

            if (!hasFilter) {
                if (isRoot) m_entities[m_entityCount++] = e;
            } else {
                const char* name = getEntityName(*m_world, e);
                bool match = false;
                for (const char* n = name; *n; ++n) {
                    const char* fn = m_searchFilter;
                    const char* nn = n;
                    while (*nn && *fn && std::tolower((unsigned char)*nn) == std::tolower((unsigned char)*fn)) {
                        ++nn; ++fn;
                    }
                    if (!*fn) { match = true; break; }
                }
                if (match) m_entities[m_entityCount++] = e;
            }
        });

        for (u32 i = 0; i < m_entityCount; ++i) {
            renderEntityNode(m_entities[i]);
        }

        renderEmptyContextMenu();
    }
    ImGui::EndChild();

    ImGui::End();
}

void HierarchyPanel::renderSearchBar() {
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##hierarchy_search", "Search entities...",
                             m_searchFilter, sizeof(m_searchFilter));
    ImGui::PopItemWidth();
}

void HierarchyPanel::renderToolbar() {
    if (ImGui::Button("+", ImVec2(24, 0))) {
        m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
        ECS::Entity e = m_world->create();
        setEntityName(*m_world, e, "New Entity");
        m_world->add<ECS::Transform>(e);
        m_context->selectEntity(e);
        m_context->endUndo(*m_world);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        if (m_context->hasMultiSelection()) {
            m_context->beginUndo(EditorCommand::RemoveEntity, u32_max, *m_world);
            for (auto& e : m_context->selectedEntities) {
                if (e.isValid()) m_world->destroy(e);
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
}

void HierarchyPanel::handleDeleteKey() {
    if (!ImGui::IsWindowFocused()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete)) return;

    if (m_context->hasMultiSelection()) {
        m_context->beginUndo(EditorCommand::RemoveEntity, u32_max, *m_world);
        for (auto& e : m_context->selectedEntities) {
            if (e.isValid()) m_world->destroy(e);
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

void HierarchyPanel::renderEntityNode(ECS::Entity entity) {
    if (!entity.isValid()) return;

    const char* name = getEntityName(*m_world, entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_context->isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;

    bool childExists = hasChildren(entity);
    if (!childExists) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (entity == m_expandEntity) {
        ImGui::SetNextItemOpen(true);
        m_expandEntity = ECS::Entity::INVALID;
    }

    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)entity.id(), flags, "%s", name);

    if (entity == m_context->selectedEntity && entity != m_lastScrollTarget) {
        ImGui::SetScrollHereY(0.5f);
        m_lastScrollTarget = entity;
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeyCtrl) {
            m_context->toggleSelection(entity);
        } else if (ImGui::GetIO().KeyShift) {
            m_context->addToSelection(entity);
        } else {
            m_context->selectEntity(entity);
        }
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
            ECS::Entity dragged(*(u32*)payload->Data, m_world);
            if (dragged != entity) {
                m_context->beginUndo(EditorCommand::MoveEntity, dragged.id(), *m_world);
                auto& parentComp = m_world->add<Scene::Parent>(dragged);
                parentComp.parent = entity;
                parentComp.dirty  = true;
                m_expandEntity = entity;
                m_context->endUndo(*m_world);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        u32 eid = entity.id();
        ImGui::SetDragDropPayload("ENTITY_DRAG", &eid, sizeof(u32));
        ImGui::Text("%s", name);
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Rename"))        { m_renaming = entity; }
        if (ImGui::MenuItem("Duplicate\tCtrl+D")) { duplicateEntity(*m_world, entity); }
        if (ImGui::MenuItem("Copy\tCtrl+C"))  { m_context->clipboardEntity = entity; }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Child")) {
            m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
            ECS::Entity child = m_world->create();
            setEntityName(*m_world, child, "Child");
            m_world->add<ECS::Transform>(child);
            auto& parentComp = m_world->add<Scene::Parent>(child);
            parentComp.parent = entity;
            parentComp.dirty  = true;
            m_expandEntity = entity;
            m_context->selectEntity(child);
            m_context->endUndo(*m_world);
        }
        if (auto* pc = m_world->get<Scene::Parent>(entity)) {
            if (pc->parent.isValid()) {
                if (ImGui::MenuItem("Unparent")) {
                    m_context->beginUndo(EditorCommand::MoveEntity, entity.id(), *m_world);
                    m_world->remove<Scene::Parent>(entity);
                    m_context->endUndo(*m_world);
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            m_context->beginUndo(EditorCommand::RemoveEntity, entity.id(), *m_world);
            m_world->destroy(entity);
            if (m_context->selectedEntity == entity) m_context->clearSelection();
            m_context->endUndo(*m_world);
        }
        ImGui::EndPopup();
    }

    if (childExists && open) {
        renderChildren(entity);
        ImGui::TreePop();
    }

    if (m_renaming == entity) {
        ImGui::OpenPopup("##rename");
        if (ImGui::BeginPopup("##rename")) {
            ImGui::Text("Rename: %s", name);
            char buf[64];
            std::strncpy(buf, getEntityName(*m_world, entity), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("##rename_input", buf, sizeof(buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_context->beginUndo(EditorCommand::SetField, entity.id(), *m_world);
                setEntityName(*m_world, entity, buf);
                m_context->endUndo(*m_world);
                m_renaming = ECS::Entity::INVALID;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_renaming = ECS::Entity::INVALID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            m_renaming = ECS::Entity::INVALID;
        }
    }
}

void HierarchyPanel::duplicateEntity(ECS::World& world, ECS::Entity src) {
    if (!src.isValid()) return;

    m_context->beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity dst = world.create();

    char newName[128];
    std::snprintf(newName, sizeof(newName), "%s (Copy)", getEntityName(world, src));
    setEntityName(world, dst, newName);

    if (auto* c = world.get<ECS::Transform>(src))          { auto& d = world.add<ECS::Transform>(dst);   d = *c; }
    if (auto* c = world.get<ECS::Sprite>(src))              { auto& d = world.add<ECS::Sprite>(dst);      d = *c; }
    if (auto* c = world.get<Physics2D::RigidBody2D>(src))   { auto& d = world.add<Physics2D::RigidBody2D>(dst); d = *c; }
    if (auto* c = world.get<Physics2D::Collider2D>(src))    { auto& d = world.add<Physics2D::Collider2D>(dst);  d = *c; }
    if (auto* c = world.get<ECS::Velocity2D>(src))          { auto& d = world.add<ECS::Velocity2D>(dst);  d = *c; }
    if (auto* c = world.get<ECS::Health>(src))              { auto& d = world.add<ECS::Health>(dst);      d = *c; }

    m_context->selectEntity(dst);
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

void HierarchyPanel::renderEmptyContextMenu() {
    if (ImGui::BeginPopupContextWindow("hierarchy_empty_ctx")) {
        if (ImGui::BeginMenu("Create Entity")) {
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

            ImGui::EndMenu();
        }
        if (m_context->clipboardEntity.isValid()) {
            if (ImGui::MenuItem("Paste\tCtrl+V")) {
                duplicateEntity(*m_world, m_context->clipboardEntity);
            }
        }
        ImGui::EndPopup();
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

void HierarchyPanel::renderChildren(ECS::Entity parent) {
    ECS::ComponentQuery childQ;
    childQ.with<Scene::Parent>();
    m_world->forEach<Scene::Parent>(childQ, [&](ECS::Entity child, Scene::Parent& p) {
        if (p.parent == parent) renderEntityNode(child);
    });
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
