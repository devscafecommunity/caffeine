#include "editor/HierarchyPanel.hpp"
#include <cctype>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Public render methods ────────────────────────────────────────

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

        m_world->forEach<NameComponent>(allQ, [&](ECS::Entity e, NameComponent& nc) {
            if (m_entityCount >= MAX_VISIBLE) return;

            bool isRoot = true;
            if (auto* parent = m_world->get<Scene::Parent>(e)) {
                if (parent->parent.isValid()) isRoot = false;
            }

            if (!hasFilter) {
                if (isRoot) {
                    m_entities[m_entityCount++] = e;
                }
            } else {
                const char* name = getEntityName(*m_world, e);
                bool match = false;
                for (const char* n = name; *n; ++n) {
                    const char* fn = m_searchFilter;
                    const char* nn = n;
                    while (*nn && *fn && std::tolower(static_cast<unsigned char>(*nn)) == std::tolower(static_cast<unsigned char>(*fn))) {
                        ++nn; ++fn;
                    }
                    if (!*fn) { match = true; break; }
                }

                if (match) {
                    m_entities[m_entityCount++] = e;
                }
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

// ── Search bar ───────────────────────────────────────────────────

void HierarchyPanel::renderSearchBar() {
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##hierarchy_search", "Search entities...",
                             m_searchFilter, sizeof(m_searchFilter));
    ImGui::PopItemWidth();
}

// ── Toolbar ──────────────────────────────────────────────────────

void HierarchyPanel::renderToolbar() {
    if (ImGui::Button("+", ImVec2(24, 0))) {
        m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
        ECS::Entity e = m_world->create();
        setEntityName(*m_world, e, "New Entity");
        m_context->selectedEntity = e;
        m_context->endUndo(*m_world);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(0, 0))) {
        if (m_context->selectedEntity.isValid()) {
            m_context->beginUndo(EditorCommand::RemoveEntity,
                                m_context->selectedEntity.id(), *m_world);
            m_world->destroy(m_context->selectedEntity);
            m_context->selectedEntity = ECS::Entity::INVALID;
            m_context->endUndo(*m_world);
        }
    }
}

// ── Delete key ───────────────────────────────────────────────────

void HierarchyPanel::handleDeleteKey() {
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (m_context->selectedEntity.isValid()) {
            m_context->beginUndo(EditorCommand::RemoveEntity,
                                m_context->selectedEntity.id(), *m_world);
            m_world->destroy(m_context->selectedEntity);
            m_context->selectedEntity = ECS::Entity::INVALID;
            m_context->endUndo(*m_world);
        }
    }
}

// ── Entity tree node ─────────────────────────────────────────────

void HierarchyPanel::renderEntityNode(ECS::Entity entity) {
    if (!entity.isValid()) return;

    const char* name = getEntityName(*m_world, entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_context->selectedEntity == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool childExists = hasChildren(entity);
    if (!childExists) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)entity.id(), flags, "%s", name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_context->selectedEntity = entity;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
            ECS::Entity dragged(*(u32*)payload->Data, m_world);
            if (dragged != entity) {
                m_context->beginUndo(EditorCommand::MoveEntity, dragged.id(), *m_world);
                auto& parentComp = m_world->add<Scene::Parent>(dragged);
                parentComp.parent = entity;
                parentComp.dirty = true;
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
        if (ImGui::MenuItem("Rename")) { m_renaming = entity; }
        if (ImGui::MenuItem("Create Child")) {
            m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
            ECS::Entity child = m_world->create();
            setEntityName(*m_world, child, "Child");
            auto& parentComp = m_world->add<Scene::Parent>(child);
            parentComp.parent = entity;
            parentComp.dirty = true;
            m_context->endUndo(*m_world);
        }
        if (ImGui::MenuItem("Delete")) {
            m_context->beginUndo(EditorCommand::RemoveEntity, entity.id(), *m_world);
            m_world->destroy(entity);
            if (m_context->selectedEntity == entity) {
                m_context->selectedEntity = ECS::Entity::INVALID;
            }
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
            const char* curName = getEntityName(*m_world, entity);
            std::strncpy(buf, curName, sizeof(buf));
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

// ── Empty space context menu ─────────────────────────────────────

void HierarchyPanel::createEntityWithType(ECS::World& world, const char* name, const char* componentType) {
    m_context->beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity e = world.create();
    setEntityName(world, e, name);
    
    if (strcmp(componentType, "Camera2D") == 0) {
        world.add<ECS::Camera2DComponent>(e);
    } else if (strcmp(componentType, "Camera3D") == 0) {
        world.add<ECS::Camera3DComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    } else if (strcmp(componentType, "DirectionalLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::DirectionalLightComponent>(e);
    } else if (strcmp(componentType, "PointLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::PointLightComponent>(e);
        world.add<ECS::Position3D>(e);
    } else if (strcmp(componentType, "SpotLight") == 0) {
        world.add<ECS::LightComponent>(e);
        world.add<ECS::SpotLightComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
    } else if (strcmp(componentType, "MeshRenderer") == 0) {
        world.add<ECS::MeshRendererComponent>(e);
        world.add<ECS::Position3D>(e);
        world.add<ECS::Rotation3D>(e);
        world.add<ECS::Scale3D>(e);
    }
    
    m_context->selectedEntity = e;
    m_context->endUndo(world);
}

void HierarchyPanel::renderEmptyContextMenu() {
    if (ImGui::BeginPopupContextWindow("hierarchy_empty_ctx")) {
        if (ImGui::BeginMenu("Create Entity")) {
            if (ImGui::MenuItem("Empty Entity")) {
                m_context->beginUndo(EditorCommand::AddEntity, u32_max, *m_world);
                ECS::Entity e = m_world->create();
                setEntityName(*m_world, e, "New Entity");
                m_context->selectedEntity = e;
                m_context->endUndo(*m_world);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Camera");
            if (ImGui::MenuItem("Camera 2D")) {
                createEntityWithType(*m_world, "Camera 2D", "Camera2D");
            }
            if (ImGui::MenuItem("Camera 3D")) {
                createEntityWithType(*m_world, "Camera 3D", "Camera3D");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Lights");
            if (ImGui::MenuItem("Directional Light")) {
                createEntityWithType(*m_world, "Directional Light", "DirectionalLight");
            }
            if (ImGui::MenuItem("Point Light")) {
                createEntityWithType(*m_world, "Point Light", "PointLight");
            }
            if (ImGui::MenuItem("Spot Light")) {
                createEntityWithType(*m_world, "Spot Light", "SpotLight");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Rendering");
            if (ImGui::MenuItem("Mesh Renderer")) {
                createEntityWithType(*m_world, "Mesh Renderer", "MeshRenderer");
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

// ── Helpers ──────────────────────────────────────────────────────

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
        if (p.parent == parent) {
            renderEntityNode(child);
        }
    });
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
