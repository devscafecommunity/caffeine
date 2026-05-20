#include "editor/InspectorPanel.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/InspectorWidgets.hpp"
#include "audio/AudioComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/CameraComponents.hpp"
#include "script/CppScript.hpp"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <vector>

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
        drawCamera(world, e, ctx);
        drawScript(world, e, ctx);
        drawCppScript(world, e, ctx);
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
            ImGui::OpenPopup("add_component_v2");
            m_addComponentSearch[0] = '\0';
        }
        if (ImGui::BeginPopup("add_component_v2")) {
            ImGui::InputText("##search", m_addComponentSearch, sizeof(m_addComponentSearch));
            ImGui::Separator();
            const char* lastCategory = nullptr;
            for (const auto& entry : ComponentRegistry::instance().entries()) {
                if (entry.has(world, e)) continue;
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

        ImGui::EndChild();
    }
    ImGui::End();
}

// ── Transform drawer ─────────────────────────────────────────────

void InspectorPanel::drawTransform(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    bool enabled = !world.has<ECS::DisabledTag>(e);
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Transform", enabled, removeRequested)) return;

    if (!enabled) {
        if (!world.has<ECS::DisabledTag>(e)) world.add<ECS::DisabledTag>(e);
    } else {
        if (world.has<ECS::DisabledTag>(e)) world.remove<ECS::DisabledTag>(e);
    }

    if (world.has<ECS::Transform>(e)) {
        auto* t = world.get<ECS::Transform>(e);
        bool is2D = !world.has<ECS::MeshFilterComponent>(e) && !world.has<ECS::MeshRendererComponent>(e);
        if (Widgets::DragVec3("Position", t->position, 0.5f)) { ctx.isDirty = true; }
        if (is2D) {
            if (ImGui::DragFloat("Rotation", &t->rotation.z, 1.0f, -360.0f, 360.0f)) { ctx.isDirty = true; }
            float s[2] = { t->scale.x, t->scale.y };
            if (ImGui::DragFloat2("Scale", s, 0.05f, 0.01f, 100.0f)) {
                t->scale.x = s[0]; t->scale.y = s[1]; ctx.isDirty = true;
            }
        } else {
            if (Widgets::DragVec3("Rotation", t->rotation, 1.0f, -360.0f, 360.0f)) { ctx.isDirty = true; }
            if (Widgets::DragVec3("Scale", t->scale, 0.05f, 0.01f, 100.0f)) { ctx.isDirty = true; }
        }
    }
}

// ── Sprite drawer ────────────────────────────────────────────────

void InspectorPanel::drawSprite(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Sprite>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Sprite Renderer", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<ECS::Sprite>(e);
        ctx.isDirty = true;
        return;
    }

    auto* sprite = world.get<ECS::Sprite>(e);
    if (Widgets::AssetField("Texture", sprite->name, ".png;.jpg;.bmp", resolveProjectRoot(ctx)))
        ctx.isDirty = true;
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

// ── Stub drawers ─────────────────────────────────────────────────

// NOTE: Camera2D is a global singleton in render system, not an ECS component.
// If ECS-based camera selection is needed in the future, implement here.
void InspectorPanel::drawCamera(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    bool has2D = world.has<ECS::Camera2DComponent>(e);
    bool has3D = world.has<ECS::Camera3DComponent>(e);
    if (!has2D && !has3D) return;

    if (has2D) {
        bool enabled = true, removeRequested = false;
        if (!Widgets::ComponentHeader("Camera2D", enabled, removeRequested)) return;
        if (removeRequested) { world.remove<ECS::Camera2DComponent>(e); ctx.isDirty = true; return; }

        auto* cam = world.get<ECS::Camera2DComponent>(e);
        if (ImGui::DragFloat("Zoom",      &cam->zoom,     0.01f, 0.01f, 100.0f)) ctx.isDirty = true;
        if (ImGui::DragFloat("Near Clip", &cam->nearClip, 0.01f, 0.001f, 10.0f)) ctx.isDirty = true;
        if (ImGui::DragFloat("Far Clip",  &cam->farClip,  1.0f,  1.0f, 10000.0f)) ctx.isDirty = true;
    }

    if (has3D) {
        bool enabled = true, removeRequested = false;
        if (!Widgets::ComponentHeader("Camera3D", enabled, removeRequested)) return;
        if (removeRequested) { world.remove<ECS::Camera3DComponent>(e); ctx.isDirty = true; return; }

        auto* cam = world.get<ECS::Camera3DComponent>(e);
        if (ImGui::DragFloat("FOV",       &cam->fov,         0.5f,  10.0f, 170.0f)) ctx.isDirty = true;
        if (ImGui::DragFloat("Near Clip", &cam->nearClip,    0.01f, 0.001f, 10.0f)) ctx.isDirty = true;
        if (ImGui::DragFloat("Far Clip",  &cam->farClip,     1.0f,  1.0f, 10000.0f)) ctx.isDirty = true;
        if (ImGui::DragFloat("Aspect",    &cam->aspectRatio, 0.01f, 0.1f, 10.0f)) ctx.isDirty = true;
    }
}
void InspectorPanel::drawRigidBody2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::RigidBody2D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("RigidBody2D", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Physics2D::RigidBody2D>(e);
        ctx.isDirty = true;
        return;
    }

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
void InspectorPanel::drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Audio::AudioEmitter>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Audio Source", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Audio::AudioEmitter>(e);
        ctx.isDirty = true;
        return;
    }

    auto* emitter = world.get<Audio::AudioEmitter>(e);

    std::string clipStr(emitter->clipPath.cStr());
    if (Widgets::AssetField("Clip", clipStr, ".wav;.ogg;.mp3", resolveProjectRoot(ctx))) {
        emitter->clipPath = clipStr.c_str();
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

void InspectorPanel::drawCollider2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::Collider2D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Collider2D", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Physics2D::Collider2D>(e);
        ctx.isDirty = true;
        return;
    }

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
        if (ImGui::DragFloat2("Size", sz, 0.01f, 0.01f, 2000.0f)) {
            col->size.x = sz[0]; col->size.y = sz[1];
            ctx.isDirty = true;
        }
    } else {
        if (ImGui::DragFloat("Radius", &col->radius, 0.01f, 0.01f, 1000.0f)) {
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

    float colF[4] = {
        col->debugColor[0] / 255.0f,
        col->debugColor[1] / 255.0f,
        col->debugColor[2] / 255.0f,
        col->debugColor[3] / 255.0f
    };
    if (ImGui::ColorEdit4("Debug Color", colF)) {
        col->debugColor[0] = static_cast<u8>(colF[0] * 255.0f);
        col->debugColor[1] = static_cast<u8>(colF[1] * 255.0f);
        col->debugColor[2] = static_cast<u8>(colF[2] * 255.0f);
        col->debugColor[3] = static_cast<u8>(colF[3] * 255.0f);
        ctx.isDirty = true;
    }
}

void InspectorPanel::drawVelocity2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Velocity2D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Velocity2D", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<ECS::Velocity2D>(e);
        ctx.isDirty = true;
        return;
    }

    auto* v = world.get<ECS::Velocity2D>(e);
    float vel[2] = { v->x, v->y };
    if (ImGui::DragFloat2("Velocity", vel, 1.0f)) {
        v->x = vel[0]; v->y = vel[1];
        ctx.isDirty = true;
    }
}

void InspectorPanel::drawHealth(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Health>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Health", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<ECS::Health>(e);
        ctx.isDirty = true;
        return;
    }

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

void InspectorPanel::drawScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
#ifdef CF_HAS_SCRIPTING
    using namespace Script;
    auto* sc = world.get<ScriptComponent>(e);
    if (!sc) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Script", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Script::ScriptComponent>(e);
        ctx.isDirty = true;
        return;
    }

    static char pathBuf[512] = {};
    static std::string lastError;
    if (sc->scriptPath != std::string(pathBuf)) {
        std::strncpy(pathBuf, sc->scriptPath.c_str(), sizeof(pathBuf) - 1);
        pathBuf[sizeof(pathBuf)-1] = 0;
    }
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
#endif
}

void InspectorPanel::drawPersistent(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* pc = world.get<ECS::PersistentComponent>(e);
    if (!pc) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Persistent", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<ECS::PersistentComponent>(e);
        ctx.isDirty = true;
        return;
    }

    ImGui::Checkbox("Don't Destroy On Load", &pc->dontDestroyOnLoad);
    (void)ctx;
}

void InspectorPanel::drawMeshFilter(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* mf = world.get<ECS::MeshFilterComponent>(e);
    if (!mf) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Mesh Filter", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<ECS::MeshFilterComponent>(e);
        ctx.isDirty = true;
        return;
    }

    static const char* primitiveNames[] = { "Custom", "Cube", "Sphere", "Capsule", "Cylinder", "Plane" };
    int current = static_cast<int>(mf->primitive);
    if (ImGui::Combo("Primitive", &current, primitiveNames, 6)) {
        mf->primitive = static_cast<ECS::MeshPrimitive>(current);
        ctx.isDirty = true;
    }
    if (mf->primitive == ECS::MeshPrimitive::Custom) {
        if (Widgets::AssetField("Mesh", mf->customMeshPath, ".obj;.fbx;.gltf", resolveProjectRoot(ctx)))
            ctx.isDirty = true;
    } else {
        ImGui::TextDisabled("3D renderer pending — mesh data will be loaded when renderer is implemented");
    }
}

void InspectorPanel::drawUIWidget(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* w = world.get<UI::UIWidget>(e);
    if (!w) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Widget", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<UI::UIWidget>(e);
        ctx.isDirty = true;
        return;
    }

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

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Button", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<UI::UIButton>(e);
        ctx.isDirty = true;
        return;
    }

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

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Label", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<UI::UILabel>(e);
        ctx.isDirty = true;
        return;
    }

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

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Progress Bar", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<UI::UIProgressBar>(e);
        ctx.isDirty = true;
        return;
    }

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

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Slider", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<UI::UISlider>(e);
        ctx.isDirty = true;
        return;
    }

    ImGui::DragFloat("Min Value",     &sl->minValue, 1.0f);
    ImGui::DragFloat("Max Value",     &sl->maxValue, 1.0f);
    ImGui::SliderFloat("Value",       &sl->currentValue, sl->minValue, sl->maxValue);
    ImGui::Checkbox("Snap To Int",    &sl->snapToInt);
    ctx.isDirty = true;
}

std::filesystem::path InspectorPanel::resolveProjectRoot(const EditorContext& ctx) const {
    if (!ctx.currentScenePath.empty()) {
        return std::filesystem::path(ctx.currentScenePath).parent_path();
    }
    return {};
}

void InspectorPanel::drawCppScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Script::CppScriptComponent>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("C++ Script", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Script::CppScriptComponent>(e);
        ctx.isDirty = true;
        return;
    }

    auto* csc = world.get<Script::CppScriptComponent>(e);
    const auto& scriptNames = Script::CppScriptRegistry::instance().names();

    if (scriptNames.empty()) {
        ImGui::TextDisabled("No C++ scripts registered");
        ImGui::TextDisabled("Add scripts to scripts/ and rebuild");
        return;
    }

    static std::vector<const char*> namePtrs;
    namePtrs.clear();
    for (const auto& n : scriptNames) namePtrs.push_back(n.c_str());

    int current = -1;
    for (int i = 0; i < static_cast<int>(scriptNames.size()); ++i) {
        if (scriptNames[i] == csc->className) { current = i; break; }
    }

    if (ImGui::Combo("Class", &current, namePtrs.data(), static_cast<int>(namePtrs.size()))) {
        csc->className = scriptNames[static_cast<usize>(current)];
        csc->instance.reset();
        csc->initialized = false;
        ctx.isDirty = true;
    }

    if (!csc->className.empty()) {
        bool found = false;
        for (const auto& n : scriptNames) if (n == csc->className) { found = true; break; }
        if (!found) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Script '%s' not found — rebuild", csc->className.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), csc->instance ? "Active" : "Inactive (play to activate)");
        }
    }
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
