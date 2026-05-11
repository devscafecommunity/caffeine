#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentID.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"
#include "containers/HashMap.hpp"
#include <functional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstdio>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class InspectorPanel {
public:
    using ComponentDrawer = std::function<void(void* componentData)>;

    InspectorPanel() = default;

    void registerDrawer(ECS::ComponentID id, ComponentDrawer drawer) {
#ifdef CF_HAS_IMGUI
        m_drawers.set(id, std::move(drawer));
#endif
    }

    void render(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
        if (!m_open) return;
        if (ImGui::Begin("Inspector", &m_open)) {
            if (!ctx.selectedEntity.isValid()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No entity selected");
                ImGui::End();
                return;
            }

            ECS::Entity e = ctx.selectedEntity;

            // Entity header
            const char* name = getEntityName(world, e);
            char nameBuf[64];
            strncpy(nameBuf, name, sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                setEntityName(world, e, nameBuf);
                ctx.isDirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Entity %u", e.id());

            ImGui::Separator();
            ImGui::BeginChild("components");

            // --- Transform ---
            drawTransform(world, e, ctx);

            // --- SpriteRenderer ---
            drawSprite(world, e, ctx);

            // --- Custom drawers ---

            ImGui::Separator();
            // Add Component button
            if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
                ImGui::OpenPopup("add_component");
            }
            if (ImGui::BeginPopup("add_component")) {
                if (!world.has<ECS::Position2D>(e) && ImGui::MenuItem("Position2D")) {
                    world.add<ECS::Position2D>(e); ctx.isDirty = true;
                }
                if (!world.has<ECS::Velocity2D>(e) && ImGui::MenuItem("Velocity2D")) {
                    world.add<ECS::Velocity2D>(e); ctx.isDirty = true;
                }
                if (!world.has<ECS::Sprite>(e) && ImGui::MenuItem("Sprite")) {
                    world.add<ECS::Sprite>(e); ctx.isDirty = true;
                }
                if (!world.has<ECS::Health>(e) && ImGui::MenuItem("Health")) {
                    world.add<ECS::Health>(e); ctx.isDirty = true;
                }
                ImGui::EndPopup();
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
    void drawTransform(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
        if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

        // Position2D
        if (world.has<ECS::Position2D>(e)) {
            auto* pos = world.get<ECS::Position2D>(e);
            f32 p[2] = { pos->x, pos->y };
            if (ImGui::DragFloat2("Position", p, 0.5f)) {
                pos->x = p[0]; pos->y = p[1];
                ctx.isDirty = true;
            }
        } else {
            f32 p[2] = { 0, 0 };
            if (ImGui::DragFloat2("Position", p, 0.5f)) {
                world.add<ECS::Position2D>(e, p[0], p[1]);
                ctx.isDirty = true;
            }
        }

        // Rotation
        if (world.has<ECS::Rotation>(e)) {
            auto* rot = world.get<ECS::Rotation>(e);
            f32 degrees = rot->angle * 180.0f / 3.14159265f;
            if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f)) {
                rot->angle = degrees * 3.14159265f / 180.0f;
                ctx.isDirty = true;
            }
        } else {
            f32 degrees = 0;
            if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f)) {
                ECS::Rotation r; r.angle = degrees * 3.14159265f / 180.0f;
                world.add<ECS::Rotation>(e, r);
                ctx.isDirty = true;
            }
        }

        // Scale2D
        if (world.has<ECS::Scale2D>(e)) {
            auto* scl = world.get<ECS::Scale2D>(e);
            f32 s[2] = { scl->x, scl->y };
            if (ImGui::DragFloat2("Scale", s, 0.1f, 0.01f, 100.0f)) {
                scl->x = s[0]; scl->y = s[1];
                ctx.isDirty = true;
            }
        } else {
            f32 s[2] = { 1, 1 };
            if (ImGui::DragFloat2("Scale", s, 0.1f, 0.01f, 100.0f)) {
                world.add<ECS::Scale2D>(e, s[0], s[1]);
                ctx.isDirty = true;
            }
        }
    }

    void drawSprite(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
        if (!world.has<ECS::Sprite>(e)) {
            if (ImGui::CollapsingHeader("Sprite Renderer")) {
                if (ImGui::Button("+ Add Sprite Renderer")) {
                    world.add<ECS::Sprite>(e);
                    ctx.isDirty = true;
                }
            }
            return;
        }

        if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto* sprite = world.get<ECS::Sprite>(e);
            char buf[256];
            strncpy(buf, sprite->name.c_str(), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("Texture", buf, sizeof(buf))) {
                sprite->name = buf;
                ctx.isDirty = true;
            }
            int frame = static_cast<int>(sprite->frameIndex);
            if (ImGui::DragInt("Frame", &frame, 1, 0, 1000)) {
                sprite->frameIndex = static_cast<u32>(frame > 0 ? frame : 0);
                ctx.isDirty = true;
            }
        }
    }

    void drawCamera(ECS::World&, ECS::Entity, EditorContext&) {}
    void drawRigidBody2D(ECS::World&, ECS::Entity, EditorContext&) {}
    void drawAudioSource(ECS::World&, ECS::Entity, EditorContext&) {}
#endif

    bool m_open = true;
    HashMap<ECS::ComponentID, ComponentDrawer> m_drawers;
};

} // namespace Caffeine::Editor
