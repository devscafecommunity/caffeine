#include "editor/InspectorPanel.hpp"
#include "editor/DragDropSystem.hpp"
#include <filesystem>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Drawer registry ──────────────────────────────────────────────

void InspectorPanel::registerDrawer(u32 componentTypeId, ComponentDrawer drawer) {
    m_drawers.set(componentTypeId, std::move(drawer));
}

// ── Main render ──────────────────────────────────────────────────

void InspectorPanel::render(ECS::World& world, EditorContext& ctx) {
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
            ctx.beginUndo(EditorCommand::SetField, e.id(), world);
            setEntityName(world, e, nameBuf);
            ctx.endUndo(world);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Entity %u", e.id());

        ImGui::Separator();
        ImGui::BeginChild("components");

        drawTransform(world, e, ctx);
        drawSprite(world, e, ctx);

        ImGui::Separator();

        // Add Component button
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
            ImGui::OpenPopup("add_component");
        }
        if (ImGui::BeginPopup("add_component")) {
            if (!world.has<ECS::Position2D>(e) && ImGui::MenuItem("Position2D")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::Position2D>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::Velocity2D>(e) && ImGui::MenuItem("Velocity2D")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::Velocity2D>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::Sprite>(e) && ImGui::MenuItem("Sprite")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::Sprite>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::Health>(e) && ImGui::MenuItem("Health")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::Health>(e);
                ctx.endUndo(world);
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

// ── Transform drawer ─────────────────────────────────────────────

void InspectorPanel::drawTransform(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

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
            ECS::Rotation r;
            r.angle = degrees * 3.14159265f / 180.0f;
            world.add<ECS::Rotation>(e, r);
            ctx.isDirty = true;
        }
    }

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

// ── Sprite drawer ────────────────────────────────────────────────

void InspectorPanel::drawSprite(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
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
        // ── Drop target for texture assets ──
        if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
            if (asset->type == AssetType::Texture) {
                std::filesystem::path assetPath(asset->path);
                sprite->name = assetPath.filename().string();
                ctx.isDirty = true;
            }
        }
        int frame = static_cast<int>(sprite->frameIndex);
        if (ImGui::DragInt("Frame", &frame, 1, 0, 1000)) {
            sprite->frameIndex = static_cast<u32>(frame > 0 ? frame : 0);
            ctx.isDirty = true;
        }
    }
}

// ── Stub drawers ─────────────────────────────────────────────────

void InspectorPanel::drawCamera(ECS::World&, ECS::Entity, EditorContext&) {}
void InspectorPanel::drawRigidBody2D(ECS::World&, ECS::Entity, EditorContext&) {}
void InspectorPanel::drawAudioSource(ECS::World&, ECS::Entity, EditorContext&) {}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
