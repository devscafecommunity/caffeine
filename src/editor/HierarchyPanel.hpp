#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstring>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class HierarchyPanel {
public:
    HierarchyPanel() = default;

    void render(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
        if (!m_open) return;
        if (ImGui::Begin("Hierarchy", &m_open)) {
            // Toolbar
            if (ImGui::Button("+", ImVec2(24, 0))) {
                ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
                ECS::Entity e = world.create();
                setEntityName(world, e, "New Entity");
                ctx.selectedEntity = e;
                ctx.endUndo(world);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete", ImVec2(0, 0))) {
                if (ctx.selectedEntity.isValid()) {
                    ctx.beginUndo(EditorCommand::RemoveEntity, ctx.selectedEntity.id(), world);
                    world.destroy(ctx.selectedEntity);
                    ctx.selectedEntity = ECS::Entity::INVALID;
                    ctx.endUndo(world);
                }
            }

            ImGui::Separator();
            ImGui::BeginChild("entity_list");

            // Build root list: entities without Parent component, or with Parent::INVALID
            m_entityCount = 0;
            ECS::ComponentQuery allQ;
            world.forEach<NameComponent>(allQ, [&](ECS::Entity e, NameComponent& nc) {
                m_entities[m_entityCount++] = e;
            });

            for (u32 i = 0; i < m_entityCount; ++i) {
                ECS::Entity e = m_entities[i];
                const char* name = getEntityName(world, e);
                renderEntityNode(world, e, name, ctx);
            }

            ImGui::EndChild();
        }
        ImGui::End();
#endif
    }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void renderEntityNode(ECS::World& world, ECS::Entity entity, const char* name, EditorContext& ctx) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ctx.selectedEntity == entity) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // Check for children
        bool hasChildren = false;
        ECS::ComponentQuery childQ;
        childQ.with<Scene::Parent>();
        world.forEach<Scene::Parent>(childQ, [&](ECS::Entity, Scene::Parent& p) {
            if (p.parent == entity) hasChildren = true;
        });

        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        bool open = ImGui::TreeNodeEx((void*)(uintptr_t)entity.id(), flags, "%s", name);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            ctx.selectedEntity = entity;
        }

        // Drag-drop target for reparenting
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
                ECS::Entity dragged(*(u32*)payload->Data, &world);
                if (dragged != entity) {
                    ctx.beginUndo(EditorCommand::MoveEntity, dragged.id(), world);
                    auto& parentComp = world.add<Scene::Parent>(dragged);
                    parentComp.parent = entity;
                    parentComp.dirty = true;
                    ctx.endUndo(world);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Drag source
        if (ImGui::BeginDragDropSource()) {
            u32 eid = entity.id();
            ImGui::SetDragDropPayload("ENTITY_DRAG", &eid, sizeof(u32));
            ImGui::Text("%s", name);
            ImGui::EndDragDropSource();
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename")) { m_renaming = entity; }
            if (ImGui::MenuItem("Create Child")) {
                ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
                ECS::Entity child = world.create();
                setEntityName(world, child, "Child");
                auto& parentComp = world.add<Scene::Parent>(child);
                parentComp.parent = entity;
                parentComp.dirty = true;
                ctx.endUndo(world);
            }
            if (ImGui::MenuItem("Delete")) {
                ctx.beginUndo(EditorCommand::RemoveEntity, entity.id(), world);
                world.destroy(entity);
                if (ctx.selectedEntity == entity) ctx.selectedEntity = ECS::Entity::INVALID;
                ctx.endUndo(world);
            }
            ImGui::EndPopup();
        }

        if (hasChildren && open) {
            // Render children recursively
            ECS::ComponentQuery childQ2;
            childQ2.with<Scene::Parent>();
            world.forEach<Scene::Parent>(childQ2, [&](ECS::Entity child, Scene::Parent& p) {
                if (p.parent == entity) {
                    const char* childName = getEntityName(world, child);
                    renderEntityNode(world, child, childName, ctx);
                }
            });
            ImGui::TreePop();
        }

        // Inline rename popup
        if (m_renaming == entity) {
            ImGui::OpenPopup("##rename");
            if (ImGui::BeginPopup("##rename")) {
                ImGui::Text("Rename: %s", name);
                char buf[64];
                const char* curName = getEntityName(world, entity);
                strncpy(buf, curName, sizeof(buf));
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText("##rename_input", buf, sizeof(buf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    ctx.beginUndo(EditorCommand::SetField, entity.id(), world);
                    setEntityName(world, entity, buf);
                    ctx.endUndo(world);
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
#endif

    bool m_open = true;
    ECS::Entity m_entities[4096];
    u32 m_entityCount = 0;
    ECS::Entity m_renaming = ECS::Entity::INVALID;
};

} // namespace Caffeine::Editor
