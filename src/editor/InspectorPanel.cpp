#include "editor/InspectorPanel.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/InspectorWidgets.hpp"
#include "audio/AudioComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "ecs/MeshComponents.hpp"
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

        if (ctx.hasMultiSelection()) {
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%zu entities selected",
                               ctx.selectedEntities.size());
            ImGui::TextDisabled("Select a single entity to inspect its components.");
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
        drawScript(world, e, ctx);
        drawRigidBody2D(world, e, ctx);
        drawCollider2D(world, e, ctx);
        drawVelocity2D(world, e, ctx);
        drawHealth(world, e, ctx);
        drawAudioSource(world, e, ctx);
        drawPersistent(world, e, ctx);
        drawMeshFilter(world, e, ctx);
        drawUIWidget(world, e, ctx);
        drawUIButton(world, e, ctx);
        drawUILabel(world, e, ctx);
        drawUIProgressBar(world, e, ctx);
        drawUISlider(world, e, ctx);

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
             if (!world.has<Physics2D::RigidBody2D>(e) && ImGui::MenuItem("RigidBody2D")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<Physics2D::RigidBody2D>(e);
                ctx.endUndo(world);
            }
            if (!world.has<Physics2D::Collider2D>(e) && ImGui::MenuItem("Collider2D")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                Physics2D::Collider2D col;
                col.shape = Physics2D::ColliderShape::AABB;
                col.size = { 64.0f, 64.0f };
                col.radius = 32.0f;
                world.add<Physics2D::Collider2D>(e, col);
                ctx.endUndo(world);
            }
            if (!world.has<Audio::AudioEmitter>(e) && ImGui::MenuItem("Audio Source")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<Audio::AudioEmitter>(e);
                ctx.endUndo(world);
            }
            if (!world.has<Script::ScriptComponent>(e) && ImGui::MenuItem("Script")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<Script::ScriptComponent>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::PersistentComponent>(e) && ImGui::MenuItem("Persistent")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::PersistentComponent>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::MeshFilterComponent>(e) && ImGui::MenuItem("Mesh Filter")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::MeshFilterComponent>(e);
                ctx.endUndo(world);
            }
            if (!world.has<ECS::MeshRendererComponent>(e) && ImGui::MenuItem("Mesh Renderer")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<ECS::MeshRendererComponent>(e);
                ctx.endUndo(world);
            }
            if (!world.has<UI::UIWidget>(e) && ImGui::MenuItem("UI Widget")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<UI::UIWidget>(e);
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
        if (ImGui::IsItemActivated()) { ctx.beginUndo(EditorCommand::SetField, e.id(), world); m_undoStarted = true; }
        if (ImGui::DragFloat2("Position", p, 0.5f)) {
            pos->x = p[0]; pos->y = p[1];
            ctx.isDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_undoStarted) { ctx.endUndo(world); m_undoStarted = false; }
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
        if (ImGui::IsItemActivated()) { ctx.beginUndo(EditorCommand::SetField, e.id(), world); m_undoStarted = true; }
        if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f)) {
            rot->angle = degrees * 3.14159265f / 180.0f;
            ctx.isDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_undoStarted) { ctx.endUndo(world); m_undoStarted = false; }
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
        if (ImGui::IsItemActivated()) { ctx.beginUndo(EditorCommand::SetField, e.id(), world); m_undoStarted = true; }
        if (ImGui::DragFloat2("Scale", s, 0.1f, 0.01f, 100.0f)) {
            scl->x = s[0]; scl->y = s[1];
            ctx.isDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_undoStarted) { ctx.endUndo(world); m_undoStarted = false; }
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
                sprite->name = asset->path;
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

// NOTE: Camera2D is a global singleton in render system, not an ECS component.
// If ECS-based camera selection is needed in the future, implement here.
void InspectorPanel::drawCamera(ECS::World&, ECS::Entity, EditorContext&) {}
void InspectorPanel::drawRigidBody2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::RigidBody2D>(e)) {
        if (ImGui::CollapsingHeader("RigidBody2D")) {
            if (ImGui::Button("+ Add RigidBody2D")) {
                ctx.beginUndo(EditorCommand::AddComponent, e.id(), world);
                world.add<Physics2D::RigidBody2D>(e);
                ctx.endUndo(world);
            }
        }
        return;
    }

    if (ImGui::CollapsingHeader("RigidBody2D", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* rb = world.get<Physics2D::RigidBody2D>(e);

        ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.1f, 1000.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::DragFloat("Friction", &rb->friction, 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::DragFloat("Linear Damping", &rb->linearDamping, 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::Checkbox("Is Kinematic", &rb->isKinematic);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::Checkbox("Lock Rotation", &rb->lockRotation);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::Checkbox("Is Sleeping", &rb->isSleeping);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
    }
}
void InspectorPanel::drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    (void)ctx;
    if (!world.has<Audio::AudioEmitter>(e)) {
        if (ImGui::CollapsingHeader("Audio Source")) {
            if (ImGui::Button("+ Add Audio Source")) {
                world.add<Audio::AudioEmitter>(e);
                ctx.isDirty = true;
            }
        }
        return;
    }

    if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* emitter = world.get<Audio::AudioEmitter>(e);

        char buf[128];
        strncpy(buf, emitter->clipPath.data(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Clip", buf, sizeof(buf))) {
            emitter->clipPath = buf;
            ctx.isDirty = true;
        }
        if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
            if (asset->type == AssetType::Audio) {
                std::filesystem::path p(asset->path);
                emitter->clipPath = p.filename().string().c_str();
                ctx.isDirty = true;
            }
        }

        ImGui::SliderFloat("Volume", &emitter->volume, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::DragFloat("Max Distance", &emitter->maxDistance, 1.0f, 0.0f, 2000.0f, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;

        ImGui::Checkbox("Loop", &emitter->loop);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        ImGui::Checkbox("Play on Spawn", &emitter->playOnSpawn);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        ImGui::Checkbox("Spatial", &emitter->spatial);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
    }
}

void InspectorPanel::drawCollider2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::Collider2D>(e)) return;

    if (ImGui::CollapsingHeader("Collider2D", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* col = world.get<Physics2D::Collider2D>(e);

        const char* shapes[] = { "AABB", "Circle" };
        int shapeIdx = (col->shape == Physics2D::ColliderShape::Circle) ? 1 : 0;
        if (ImGui::Combo("Shape", &shapeIdx, shapes, 2)) {
            col->shape = (shapeIdx == 1) ? Physics2D::ColliderShape::Circle
                                         : Physics2D::ColliderShape::AABB;
            ctx.isDirty = true;
        }

        if (col->shape == Physics2D::ColliderShape::AABB) {
            float sz[2] = { col->size.x, col->size.y };
            if (ImGui::DragFloat2("Size", sz, 1.0f, 0.1f, 2000.0f)) {
                col->size.x = sz[0]; col->size.y = sz[1];
                ctx.isDirty = true;
            }
        } else {
            if (ImGui::DragFloat("Radius", &col->radius, 1.0f, 0.1f, 1000.0f)) {
                ctx.isDirty = true;
            }
        }

        float off[2] = { col->offset.x, col->offset.y };
        if (ImGui::DragFloat2("Offset", off, 0.5f)) {
            col->offset.x = off[0]; col->offset.y = off[1];
            ctx.isDirty = true;
        }

        if (ImGui::Checkbox("Is Static",    &col->isStatic))   ctx.isDirty = true;
        if (ImGui::Checkbox("Is Trigger",   &col->isTrigger))  ctx.isDirty = true;
        if (ImGui::Checkbox("Is One Way",   &col->isOneWay))   ctx.isDirty = true;

        int layer = static_cast<int>(col->layer);
        if (ImGui::DragInt("Layer", &layer, 1, 0, 31)) {
            col->layer = static_cast<u32>(layer);
            ctx.isDirty = true;
        }
    }
}

void InspectorPanel::drawVelocity2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Velocity2D>(e)) return;
    if (ImGui::CollapsingHeader("Velocity2D")) {
        auto* v = world.get<ECS::Velocity2D>(e);
        float vel[2] = { v->x, v->y };
        if (ImGui::DragFloat2("Velocity", vel, 1.0f)) {
            v->x = vel[0]; v->y = vel[1];
            ctx.isDirty = true;
        }
    }
}

void InspectorPanel::drawHealth(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Health>(e)) return;
    if (ImGui::CollapsingHeader("Health")) {
        auto* h = world.get<ECS::Health>(e);
        int curr = static_cast<int>(h->current);
        int mx = static_cast<int>(h->max);
        if (ImGui::DragInt("Current", &curr, 1, 0, 999999)) {
            h->current = static_cast<u32>(curr);
            ctx.isDirty = true;
        }
        if (ImGui::DragInt("Max", &mx, 1, 0, 999999)) {
            h->max = static_cast<u32>(mx);
            ctx.isDirty = true;
        }
    }
}

void InspectorPanel::drawScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
#ifdef CF_HAS_SCRIPTING
    using namespace Script;
    auto* sc = world.get<ScriptComponent>(e);
    if (!sc) {
        if (ImGui::Button("Add Script")) {
            world.add<ScriptComponent>(e);
        }
        return;
    }
    static char pathBuf[512] = {};
    static std::string lastError;
    if (sc->scriptPath != std::string(pathBuf)) {
        std::strncpy(pathBuf, sc->scriptPath.c_str(), sizeof(pathBuf) - 1);
        pathBuf[sizeof(pathBuf)-1] = 0;
    }
    ImGui::Text("Script Component");
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##scriptPath", pathBuf, sizeof(pathBuf))) {
        sc->scriptPath = pathBuf;
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::filesystem::path dropped(static_cast<const char*>(payload->Data));
            if (dropped.extension() == ".lua") {
                sc->scriptPath = dropped.string();
                std::strncpy(pathBuf, sc->scriptPath.c_str(), sizeof(pathBuf) - 1);
                pathBuf[sizeof(pathBuf) - 1] = 0;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        if (ctx.scriptEngine) {
            std::string err;
            bool ok = ctx.scriptEngine->loadScript(sc->scriptPath, &err);
            lastError = ok ? "" : err;
        } else {
            lastError = "ScriptEngine not available";
        }
    }
    if (!lastError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", lastError.c_str());
        ImGui::PopStyleColor();
    }
#else
    (void)world; (void)e; (void)ctx;
    ImGui::TextDisabled("Scripting not enabled");
#endif
}

void InspectorPanel::drawPersistent(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* pc = world.get<ECS::PersistentComponent>(e);
    if (!pc) return;
    if (!ImGui::CollapsingHeader("Persistent", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Checkbox("Don't Destroy On Load", &pc->dontDestroyOnLoad);
    (void)ctx;
}

void InspectorPanel::drawMeshFilter(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* mf = world.get<ECS::MeshFilterComponent>(e);
    if (!mf) return;
    if (!ImGui::CollapsingHeader("Mesh Filter", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static const char* primitiveNames[] = { "Custom", "Cube", "Sphere", "Capsule", "Cylinder", "Plane" };
    int current = static_cast<int>(mf->primitive);
    if (ImGui::Combo("Primitive", &current, primitiveNames, 6)) {
        mf->primitive = static_cast<ECS::MeshPrimitive>(current);
        ctx.isDirty = true;
    }
    if (mf->primitive == ECS::MeshPrimitive::Custom) {
        char buf[256];
        strncpy(buf, mf->customMeshPath.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Mesh Path", buf, sizeof(buf))) {
            mf->customMeshPath = buf;
            ctx.isDirty = true;
        }
    } else {
        ImGui::TextDisabled("3D renderer pending — mesh data will be loaded when renderer is implemented");
    }
}

void InspectorPanel::drawUIWidget(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* w = world.get<UI::UIWidget>(e);
    if (!w) return;
    if (!ImGui::CollapsingHeader("UI Widget", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static const char* typeNames[] = { "Canvas", "Panel", "Button", "Label", "ProgressBar", "Checkbox", "Slider" };
    int current = static_cast<int>(w->type);
    ImGui::Combo("Type", &current, typeNames, 7);

    ImGui::Checkbox("Visible",       &w->visible);
    ImGui::Checkbox("Interactable",  &w->interactable);
    ImGui::DragInt("Sibling Order",  &w->siblingOrder);

    if (ImGui::TreeNode("Rect Transform")) {
        ImGui::DragFloat2("Anchor Min",  &w->transform.anchorMin.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Anchor Max",  &w->transform.anchorMax.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Offset Min",  &w->transform.offsetMin.x, 1.0f);
        ImGui::DragFloat2("Offset Max",  &w->transform.offsetMax.x, 1.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Style")) {
        ImGui::ColorEdit4("Background",  &w->style.backgroundColor.r);
        ImGui::ColorEdit4("Text Color",  &w->style.textColor.r);
        ImGui::ColorEdit4("Border",      &w->style.borderColor.r);
        ImGui::DragFloat("Border Width", &w->style.borderWidth, 0.5f, 0.0f, 20.0f);
        ImGui::DragFloat("Border Radius",&w->style.borderRadius, 0.5f, 0.0f, 50.0f);
        ImGui::DragFloat("Font Size",    &w->style.fontSize, 0.5f, 6.0f, 96.0f);
        ImGui::DragFloat2("Text Align",  &w->style.textAlignment.x, 0.01f, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    ctx.isDirty = true;
}

void InspectorPanel::drawUIButton(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* btn = world.get<UI::UIButton>(e);
    if (!btn) return;
    if (!ImGui::CollapsingHeader("UI Button", ImGuiTreeNodeFlags_DefaultOpen)) return;

    char buf[64];
    strncpy(buf, btn->labelText.cStr(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Label", buf, sizeof(buf))) {
        btn->labelText = buf;
        ctx.isDirty = true;
    }
    ImGui::ColorEdit4("Idle Color",    &btn->idleColor.r);
    ImGui::ColorEdit4("Hover Color",   &btn->hoverColor.r);
    ImGui::ColorEdit4("Pressed Color", &btn->pressedColor.r);
    ImGui::TextDisabled("Hovered: %s  Pressed: %s", btn->isHovered ? "yes" : "no", btn->isPressed ? "yes" : "no");
}

void InspectorPanel::drawUILabel(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* lbl = world.get<UI::UILabel>(e);
    if (!lbl) return;
    if (!ImGui::CollapsingHeader("UI Label", ImGuiTreeNodeFlags_DefaultOpen)) return;

    char buf[256];
    strncpy(buf, lbl->text.cStr(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputTextMultiline("Text", buf, sizeof(buf), ImVec2(-1, 60))) {
        lbl->text = buf;
        ctx.isDirty = true;
    }
    ImGui::Checkbox("Word Wrap", &lbl->wordWrap);
}

void InspectorPanel::drawUIProgressBar(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* pb = world.get<UI::UIProgressBar>(e);
    if (!pb) return;
    if (!ImGui::CollapsingHeader("UI Progress Bar", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::DragFloat("Min Value",     &pb->minValue, 1.0f);
    ImGui::DragFloat("Max Value",     &pb->maxValue, 1.0f);
    ImGui::DragFloat("Current Value", &pb->currentValue, 1.0f, pb->minValue, pb->maxValue);
    ImGui::Checkbox("Show Text",      &pb->showText);
    ImGui::ColorEdit4("Fill Color",   &pb->fillColor.r);

    f32 fraction = (pb->maxValue > pb->minValue)
        ? (pb->currentValue - pb->minValue) / (pb->maxValue - pb->minValue)
        : 0.0f;
    ImGui::ProgressBar(fraction, ImVec2(-1, 0));
    ctx.isDirty = true;
}

void InspectorPanel::drawUISlider(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* sl = world.get<UI::UISlider>(e);
    if (!sl) return;
    if (!ImGui::CollapsingHeader("UI Slider", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::DragFloat("Min Value",     &sl->minValue, 1.0f);
    ImGui::DragFloat("Max Value",     &sl->maxValue, 1.0f);
    ImGui::SliderFloat("Value",       &sl->currentValue, sl->minValue, sl->maxValue);
    ImGui::Checkbox("Snap To Int",    &sl->snapToInt);
    ctx.isDirty = true;
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
