#include "editor/InspectorPanel.hpp"
#include "editor/PluginSystem.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include "editor/EntitySnapshotExport.hpp"
#include "editor/EditorPanelUtils.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/FilePicker.hpp"
#include "editor/InspectorWidgets.hpp"
#include "assets/PrefabSerializer.hpp"
#include "editor/PrefabSystem.hpp"
#include "ecs/PrefabComponents.hpp"
#include "audio/AudioComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "animation/AnimationPlayer.hpp"
#include "animation/SkinLibrary.hpp"
#include "effects/EffectSystem.hpp"
#include "effects/EffectTypes.hpp"
#include "navigation/NavVolume.hpp"
#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshMaterialExport.hpp"
#include "editor/EditorPaths.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/EnvironmentEffectsComponents.hpp"
#include "render/RenderFeatures.hpp"
#include "editor/ComponentTypeRegistry.hpp"
#include "ecs/TerrainComponents.hpp"
#include "core/WorldUnits.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainResolution.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "math/Quat.hpp"
#include "script/CppScript.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>
#include <iostream>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

// ── Drawer registry ──────────────────────────────────────────────

void InspectorPanel::registerDrawer(u32 componentTypeId, ComponentDrawer drawer) {
    m_drawers.set(componentTypeId, std::move(drawer));
}

void InspectorPanel::unregisterDrawer(u32 componentTypeId) {
    m_drawers.remove(componentTypeId);
}

// ── Main render ──────────────────────────────────────────────────

void InspectorPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;
    editorPanelApplyDetach(m_detached, ImVec2(420, 640));
    if (ImGui::Begin("Inspector", &m_open)) {
        editorPanelDetachTabButton(m_detached);
        if (!ctx.selectedEntity.isValid()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No entity selected");
            ImGui::TextDisabled("Click an entity in the Hierarchy to inspect it.");
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

        ECS::Entity e(ctx.selectedEntity.id(), &world);

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

         if (ImGui::Button("Copy snapshot")) {
             const std::string snapshot = exportEntitySnapshot(world, e);
             ImGui::SetClipboardText(snapshot.c_str());
         }
         ImGui::SameLine();
         if (ImGui::Button("Export .txt")) {
             const std::string snapshot = exportEntitySnapshot(world, e);
             const std::filesystem::path out =
                 std::filesystem::path("exports") / ("entity_" + std::to_string(e.id()) + ".txt");
             std::error_code ec;
             std::filesystem::create_directories(out.parent_path(), ec);
             std::ofstream file(out);
             if (file) {
                 file << snapshot;
                 ctx.pushTransientStatus("Exported " + out.string(), false);
             }
         }
         if (ImGui::IsItemHovered()) {
             ImGui::SetTooltip("Copy position, rotation, camera and mesh params to clipboard or file");
         }

         ImGui::Separator();
         if (ImGui::Button("Save as Prefab", ImVec2(-1, 0))) {
             ctx.browse.requestFilesystem(EditorContext::BrowseSession::Kind::SaveFile,
                                          "Save Entity as Prefab", "prefabs/", e.id());
         }
         if (ctx.browse.resultReady && ctx.browse.tag == e.id()) {
             savePrefab(world, e, ctx.browse.result);
             ctx.browse.clear();
         }

         ImGui::Separator();
        ImGui::BeginChild("components");

        drawPrefabInstance(world, e, ctx);
        drawTransform(world, e, ctx);
        drawSprite(world, e, ctx);
        drawCamera(world, e, ctx);
        drawScript(world, e, ctx);
        drawCppScript(world, e, ctx);
        drawRigidBody2D(world, e, ctx);
        drawCollider2D(world, e, ctx);
        drawRigidBody3D(world, e, ctx);
        drawCollider3D(world, e, ctx);
        drawAudioSource(world, e, ctx);
        drawPersistent(world, e, ctx);
        drawMeshFilter(world, e, ctx);
        drawUIWidget(world, e, ctx);
        drawUIButton(world, e, ctx);
        drawUILabel(world, e, ctx);
        drawUIProgressBar(world, e, ctx);
        drawUISlider(world, e, ctx);
        drawLight(world, e, ctx);
        drawSkybox(world, e, ctx);
        drawEnvironmentEffects(world, e, ctx);
        drawTerrain(world, e, ctx);
        drawPostProcess(world, e, ctx);
        drawForwardRenderFeatures(world, e, ctx);
        drawAnimationPlayer(world, e, ctx);
        drawSpriteSheet(world, e, ctx);
        drawSkinnedPose(world, e, ctx);
        drawEffect(world, e, ctx);
        drawNavAgent(world, e, ctx);

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
                if (!PluginManager::instance().isContributionEnabled(entry.plugin)) continue;
                if (m_addComponentSearch[0] != '\0') {
                    std::string haystack = entry.name + " " + entry.category + " " + entry.keywords;
                    std::string query = m_addComponentSearch;
                    std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
                    if (haystack.find(query) == std::string::npos) continue;
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
    constexpr f32 kDegToRad = 3.14159265f / 180.0f;
    constexpr f32 kRadToDeg = 180.0f / 3.14159265f;

    bool enabled = !world.has<ECS::DisabledTag>(e);
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Transform", enabled, removeRequested, "arrows-diagonal-rotated")) return;

    if (!enabled) {
        if (!world.has<ECS::DisabledTag>(e)) world.add<ECS::DisabledTag>(e);
    } else {
        if (world.has<ECS::DisabledTag>(e)) world.remove<ECS::DisabledTag>(e);
    }

    if (world.has<ECS::Transform>(e)) {
        auto* t = world.get<ECS::Transform>(e);
        bool is2D = (ctx.viewMode == EditorContext::ViewMode::Mode2D);
        bool changed = false;
        if (Widgets::DragVec3("Position", t->position, 0.5f)) { changed = true; }
        if (is2D) {
            if (ImGui::DragFloat("Rotation", &t->rotation.z, 1.0f, -360.0f, 360.0f)) { changed = true; }
            Vec2 scale2D(t->scale.x, t->scale.y);
            if (Widgets::DragVec2("Scale", scale2D, 0.05f, 0.01f, 100.0f)) {
                t->scale.x = scale2D.x;
                t->scale.y = scale2D.y;
                changed = true;
            }
        } else {
            if (Widgets::DragVec3("Rotation", t->rotation, 1.0f, -360.0f, 360.0f)) { changed = true; }
            ImGui::Checkbox("Keep proportions", &ctx.uniformScale);
            const Vec3 oldScale = t->scale;
            if (Widgets::DragVec3("Scale", t->scale, 0.05f, 0.01f, 100.0f)) {
                if (ctx.uniformScale) {
                    const f32 dx = std::abs(t->scale.x - oldScale.x);
                    const f32 dy = std::abs(t->scale.y - oldScale.y);
                    const f32 dz = std::abs(t->scale.z - oldScale.z);
                    f32 factor = 1.0f;
                    if (dx >= dy && dx >= dz && oldScale.x != 0.0f) factor = t->scale.x / oldScale.x;
                    else if (dy >= dx && dy >= dz && oldScale.y != 0.0f) factor = t->scale.y / oldScale.y;
                    else if (oldScale.z != 0.0f) factor = t->scale.z / oldScale.z;
                    t->scale = oldScale * factor;
                }
                changed = true;
            }
        }

        if (changed) {
            if (auto* p3 = world.get<ECS::Position3D>(e)) {
                p3->position = t->position;
            }
            if (auto* s3 = world.get<ECS::Scale3D>(e)) {
                s3->scale = t->scale;
            }
            if (auto* r3 = world.get<ECS::Rotation3D>(e)) {
                const Quat q = Quat::fromEuler(t->rotation.x * kDegToRad,
                                               t->rotation.y * kDegToRad,
                                               t->rotation.z * kDegToRad).normalized();
                r3->quaternion = Vec4(q.x, q.y, q.z, q.w);
            }
            PrefabSystem::RecordOverride(world, e, "Transform", "position",
                                         PrefabSystem::SerializeVec3(t->position));
            ctx.markDirty();
        }
    } else if (auto* p3 = world.get<ECS::Position3D>(e)) {
        const bool posOverride = PrefabSystem::IsOverridden(world, e, "Position3D", "position");
        if (posOverride) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
        if (Widgets::DragVec3("Position", p3->position, 0.5f)) {
            PrefabSystem::RecordOverride(world, e, "Position3D", "position",
                                         PrefabSystem::SerializeVec3(p3->position));
            ctx.markDirty();
        }
        if (posOverride) ImGui::PopStyleColor();

        Vec3 eulerDeg(0.0f, 0.0f, 0.0f);
        if (auto* r3 = world.get<ECS::Rotation3D>(e)) {
            const Vec3 eulerRad = Quat(r3->quaternion.x, r3->quaternion.y, r3->quaternion.z, r3->quaternion.w)
                                      .normalized()
                                      .toEuler();
            eulerDeg = Vec3(eulerRad.x * kRadToDeg, eulerRad.y * kRadToDeg, eulerRad.z * kRadToDeg);
        }

        if (Widgets::DragVec3("Rotation", eulerDeg, 1.0f, -360.0f, 360.0f)) {
            auto& r3 = world.add<ECS::Rotation3D>(e);
            const Quat q = Quat::fromEuler(eulerDeg.x * kDegToRad,
                                           eulerDeg.y * kDegToRad,
                                           eulerDeg.z * kDegToRad).normalized();
            r3.quaternion = Vec4(q.x, q.y, q.z, q.w);
            ctx.markDirty();
        }

        Vec3 scale(1.0f, 1.0f, 1.0f);
        if (auto* s3 = world.get<ECS::Scale3D>(e)) {
            scale = s3->scale;
        }
        ImGui::Checkbox("Keep proportions", &ctx.uniformScale);
        const Vec3 oldScale = scale;
        if (Widgets::DragVec3("Scale", scale, 0.05f, 0.01f, 100.0f)) {
            if (ctx.uniformScale) {
                const f32 dx = std::abs(scale.x - oldScale.x);
                const f32 dy = std::abs(scale.y - oldScale.y);
                const f32 dz = std::abs(scale.z - oldScale.z);
                f32 factor = 1.0f;
                if (dx >= dy && dx >= dz && oldScale.x != 0.0f) factor = scale.x / oldScale.x;
                else if (dy >= dx && dy >= dz && oldScale.y != 0.0f) factor = scale.y / oldScale.y;
                else if (oldScale.z != 0.0f) factor = scale.z / oldScale.z;
                scale = oldScale * factor;
            }
            world.add<ECS::Scale3D>(e).scale = scale;
            ctx.markDirty();
        }
    }
}

// ── Sprite drawer ────────────────────────────────────────────────

void InspectorPanel::drawSprite(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::Sprite>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Sprite Renderer", enabled, removeRequested, "beer")) return;
    if (removeRequested) {
        world.remove<ECS::Sprite>(e);
        ctx.markDirty();
        return;
    }

    auto* sprite = world.get<ECS::Sprite>(e);
    if (Widgets::AssetField(ctx, "Texture", sprite->name, ".png;.jpg;.bmp"))
        ctx.markDirty();
    int frame = static_cast<int>(sprite->frameIndex);
    if (ImGui::DragInt("Frame", &frame, 1, 0, 1000)) {
        sprite->frameIndex = static_cast<u32>(frame > 0 ? frame : 0);
        ctx.markDirty();
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
        if (!Widgets::ComponentHeader("Camera2D", enabled, removeRequested, "account")) return;
        if (removeRequested) { world.remove<ECS::Camera2DComponent>(e); ctx.markDirty(); return; }

        auto* cam = world.get<ECS::Camera2DComponent>(e);
        if (ImGui::DragFloat("Zoom",      &cam->zoom,     0.01f, 0.01f, 100.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Near Clip", &cam->nearClip, 0.01f, 0.001f, 10.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Far Clip",  &cam->farClip,  1.0f,  1.0f, 10000.0f)) ctx.markDirty();
    }

    if (has3D) {
        bool enabled = true, removeRequested = false;
        if (!Widgets::ComponentHeader("Camera3D", enabled, removeRequested, "account")) return;
        if (removeRequested) { world.remove<ECS::Camera3DComponent>(e); ctx.markDirty(); return; }

        auto* cam = world.get<ECS::Camera3DComponent>(e);
        if (ImGui::DragFloat("FOV",       &cam->fov,         0.5f,  10.0f, 170.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Near Clip", &cam->nearClip,    0.01f, 0.001f, 10.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Far Clip",  &cam->farClip,     1.0f,  1.0f, 10000.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Aspect",    &cam->aspectRatio, 0.01f, 0.1f, 10.0f)) ctx.markDirty();
    }
}
void InspectorPanel::drawRigidBody2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::RigidBody2D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("RigidBody2D", enabled, removeRequested, "arrows-vertical")) return;
    if (removeRequested) {
        world.remove<Physics2D::RigidBody2D>(e);
        ctx.markDirty();
        return;
    }

    auto* rb = world.get<Physics2D::RigidBody2D>(e);

    ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.1f, 1000.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::DragFloat("Friction", &rb->friction, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::DragFloat("Linear Damping", &rb->linearDamping, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::Checkbox("Is Kinematic", &rb->isKinematic);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::Checkbox("Lock Rotation", &rb->lockRotation);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::Checkbox("Is Sleeping", &rb->isSleeping);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
}
void InspectorPanel::drawRigidBody3D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics3D::RigidBody3D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("RigidBody3D", enabled, removeRequested, "arrows-vertical")) return;
    if (removeRequested) {
        world.remove<Physics3D::RigidBody3D>(e);
        ctx.markDirty();
        return;
    }

    auto* rb = world.get<Physics3D::RigidBody3D>(e);
    ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.001f, 10000.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
    ImGui::DragFloat("Friction", &rb->friction, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
    ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    const char* types[] = {"Dynamic", "Kinematic", "Static"};
    int type = static_cast<int>(rb->bodyType);
    if (ImGui::Combo("Body Type", &type, types, 3)) {
        rb->bodyType = static_cast<Physics3D::BodyType3D>(type);
        ctx.markDirty();
    }
    if (ImGui::Checkbox("Lock Rotation", &rb->lockRotation)) ctx.markDirty();
}

void InspectorPanel::drawCollider3D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics3D::Collider3D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Collider3D", enabled, removeRequested, "box")) return;
    if (removeRequested) {
        world.remove<Physics3D::Collider3D>(e);
        ctx.markDirty();
        return;
    }

    auto* col = world.get<Physics3D::Collider3D>(e);
    const char* shapes[] = {"Box", "Sphere", "Capsule"};
    int shape = static_cast<int>(col->shape);
    if (ImGui::Combo("Shape", &shape, shapes, 3)) {
        col->shape = static_cast<Physics3D::ColliderShape3D>(shape);
        ctx.markDirty();
    }
    if (col->shape == Physics3D::ColliderShape3D::Sphere) {
        ImGui::DragFloat("Radius", &col->halfExtents.x, 0.01f, 0.01f, 100.0f, "%.2f");
    } else if (col->shape == Physics3D::ColliderShape3D::Capsule) {
        ImGui::DragFloat("Radius", &col->halfExtents.x, 0.01f, 0.01f, 100.0f, "%.2f");
        ImGui::DragFloat("Height", &col->halfExtents.y, 0.01f, 0.02f, 100.0f, "%.2f");
    } else {
        ImGui::DragFloat3("Half Extents", &col->halfExtents.x, 0.01f, 0.01f, 100.0f, "%.2f");
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
    if (ImGui::Checkbox("Trigger", &col->isTrigger)) ctx.markDirty();
}

void InspectorPanel::drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Audio::AudioEmitter>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Audio Source", enabled, removeRequested, "bell")) return;
    if (removeRequested) {
        world.remove<Audio::AudioEmitter>(e);
        ctx.markDirty();
        return;
    }

    auto* emitter = world.get<Audio::AudioEmitter>(e);

    std::string clipStr(emitter->clipPath.cStr());
    ImGui::PushID("audio-clip");
    if (Widgets::AssetField(ctx, "Clip", clipStr, ".wav;.ogg;.mp3")) {
        emitter->clipPath = clipStr.c_str();
        ctx.markDirty();
    }
    ImGui::PopID();

    ImGui::SliderFloat("Volume", &emitter->volume, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::DragFloat("Max Distance", &emitter->maxDistance, 1.0f, 0.0f, 2000.0f, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();

    ImGui::Checkbox("Loop", &emitter->loop);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
    ImGui::Checkbox("Play on Spawn", &emitter->playOnSpawn);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
    ImGui::Checkbox("Spatial", &emitter->spatial);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markDirty();
}

void InspectorPanel::drawCollider2D(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Physics2D::Collider2D>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Collider2D", enabled, removeRequested, "alert-square")) return;
    if (removeRequested) {
        world.remove<Physics2D::Collider2D>(e);
        ctx.markDirty();
        return;
    }

    auto* col = world.get<Physics2D::Collider2D>(e);

    const char* shapes[] = { "AABB", "Circle" };
    int shapeIdx = (col->shape == Physics2D::ColliderShape::Circle) ? 1 : 0;
    if (ImGui::Combo("Shape", &shapeIdx, shapes, 2)) {
        col->shape = (shapeIdx == 1) ? Physics2D::ColliderShape::Circle
                                     : Physics2D::ColliderShape::AABB;
        ctx.markDirty();
    }

    if (col->shape == Physics2D::ColliderShape::AABB) {
        Vec2 size(col->size.x, col->size.y);
        if (Widgets::DragVec2("Size", size, 0.01f, 0.01f, 2000.0f)) {
            col->size.x = size.x;
            col->size.y = size.y;
            ctx.markDirty();
        }
    } else {
        if (ImGui::DragFloat("Radius", &col->radius, 0.01f, 0.01f, 1000.0f)) {
            ctx.markDirty();
        }
    }

    Vec2 offset(col->offset.x, col->offset.y);
    if (Widgets::DragVec2("Offset", offset, 0.5f)) {
        col->offset.x = offset.x;
        col->offset.y = offset.y;
        ctx.markDirty();
    }

    if (ImGui::Checkbox("Is Static",    &col->isStatic))   ctx.markDirty();
    if (ImGui::Checkbox("Is Trigger",   &col->isTrigger))  ctx.markDirty();
    if (ImGui::Checkbox("Is One Way",   &col->isOneWay))   ctx.markDirty();

    int layer = static_cast<int>(col->layer);
    if (ImGui::DragInt("Layer", &layer, 1, 0, 31)) {
        col->layer = static_cast<u32>(layer);
        ctx.markDirty();
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
        ctx.markDirty();
    }
}

void InspectorPanel::drawScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
#ifdef CF_HAS_SCRIPTING
    using namespace Script;
    auto* sc = world.get<ScriptComponent>(e);
    if (!sc) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Script", enabled, removeRequested, "at")) return;
    if (removeRequested) {
        world.remove<Script::ScriptComponent>(e);
        ctx.markDirty();
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
    if (ctx.scriptEngine && !sc->scriptPath.empty()) {
        if (!ctx.scriptEngine->isLoaded(sc->scriptPath)) {
            std::string err;
            ctx.scriptEngine->loadScript(sc->scriptPath, &err);
        }
        auto vars = ctx.scriptEngine->listExposedVars(sc->scriptPath);
        if (!vars.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Exposed");
            for (auto& var : vars) {
                ImGui::PushID(var.name.c_str());
                if (var.kind == Script::ScriptEngine::ExposedVar::Kind::Boolean) {
                    if (ImGui::Checkbox(var.name.c_str(), &var.boolean)) {
                        ctx.scriptEngine->setExposedVar(sc->scriptPath, var);
                    }
                } else if (var.kind == Script::ScriptEngine::ExposedVar::Kind::Number) {
                    float v = static_cast<float>(var.number);
                    if (ImGui::DragFloat(var.name.c_str(), &v, 0.05f)) {
                        var.number = static_cast<double>(v);
                        ctx.scriptEngine->setExposedVar(sc->scriptPath, var);
                    }
                } else {
                    char buf[256];
                    std::strncpy(buf, var.string.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = 0;
                    if (ImGui::InputText(var.name.c_str(), buf, sizeof(buf))) {
                        var.string = buf;
                        ctx.scriptEngine->setExposedVar(sc->scriptPath, var);
                    }
                }
                ImGui::PopID();
            }
        }
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
    if (!Widgets::ComponentHeader("Persistent", enabled, removeRequested, "backup-restore")) return;
    if (removeRequested) {
        world.remove<ECS::PersistentComponent>(e);
        ctx.markDirty();
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
    if (!Widgets::ComponentHeader("Mesh Filter", enabled, removeRequested, "arrows-diagonal")) return;
    if (removeRequested) {
        world.remove<ECS::MeshFilterComponent>(e);
        ctx.markDirty();
        return;
    }

    auto syncMaterial = [&](const std::string& path) {
        mf->customMaterialPath = path;
        if (!world.has<ECS::MeshRendererComponent>(e)) world.add<ECS::MeshRendererComponent>(e);
        if (auto* renderer = world.get<ECS::MeshRendererComponent>(e)) {
            renderer->materialPath = path;
        }
        ctx.markDirty();
        ctx.assetBrowserDirty = true;
    };

    static const char* primitiveNames[] = {
        "Custom", "Cube", "Sphere", "Capsule", "Cylinder", "Plane", "Cone", "Pyramid", "Torus"};
    int current = static_cast<int>(mf->primitive);
    Widgets::setWidthForLabel("Primitive");
    if (ImGui::Combo("Primitive", &current, primitiveNames, IM_ARRAYSIZE(primitiveNames))) {
        mf->primitive = static_cast<ECS::MeshPrimitive>(current);
        if (world.has<ECS::MeshGeometryComponent>(e)) world.remove<ECS::MeshGeometryComponent>(e);
        ctx.meshElementSelection.clear();
        if (Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(e)) {
            Effects::VolumetricShape shape = Effects::VolumetricShape::Sphere;
            if (effect->kind == static_cast<u8>(Effects::EffectKind::VolumetricLight) &&
                Effects::volumetricShapeForMesh(mf->primitive, shape)) {
                effect->pad1 = static_cast<u8>(shape);
            }
        }
        ctx.markDirty();
    }
    if (mf->primitive != ECS::MeshPrimitive::Custom) {
        if (!world.has<ECS::Scale3D>(e)) {
            world.add<ECS::Scale3D>(e);
        }
        if (auto* scale = world.get<ECS::Scale3D>(e)) {
            Widgets::setWidthForLabel("Shape Size");
            if (ImGui::DragFloat3("Shape Size", &scale->scale.x, 0.05f, 0.01f, 1000.0f, "%.2f")) {
                ctx.markDirty();
            }
            ImGui::TextDisabled("Non-uniform scale stretches the primitive.");
            ImGui::TextDisabled("Viewport: Vertex, Edge and Face edit the geometry.");
        }
    }
    if (mf->primitive == ECS::MeshPrimitive::Custom) {
        if (Widgets::AssetField(ctx, "Mesh", mf->customMeshPath, ".obj;.fbx;.gltf;.glb")) {
            ctx.markDirty();
            if (mf->customMaterialPath.empty()) {
                const std::string projectRoot = resolveProjectRoot(ctx).string();
                if (Assets::Mesh3D* mesh =
                        Assets::MeshCache::getInstance().getMesh(mf->customMeshPath, projectRoot)) {
                    std::filesystem::path meshFile(mf->customMeshPath);
                    const auto root = resolveProjectRoot(ctx);
                    if (!meshFile.is_absolute() && !root.empty()) meshFile = root / meshFile;
                    const auto materials = Assets::exportMeshMaterials(*mesh, meshFile, root, false);
                    if (!materials.empty()) syncMaterial(materials.front());
                }
            }
        }
        if (world.has<ECS::TerrainComponent>(e)) {
            ImGui::TextDisabled("Texture managed by Terrain component");
        } else if (Widgets::AssetField(ctx, "Albedo Texture", mf->customTexturePath, ".png;.jpg;.jpeg")) {
            ctx.markDirty();
        }
        if (!world.has<ECS::TerrainComponent>(e)) {
            if (Widgets::AssetField(ctx, "Normal Map", mf->customNormalPath, ".png;.jpg;.jpeg")) {
                ctx.markDirty();
            }
            if (ImGui::SliderFloat("Shininess", &mf->shininess, 1.0f, 128.0f)) {
                ctx.markDirty();
            }
        }
        if (!world.has<ECS::TerrainComponent>(e) && !mf->customTexturePath.empty()) {
            std::string projectRoot = resolveProjectRoot(ctx).string();
            bool textureFound = false;
            auto checkTexturePath = [&](const std::filesystem::path& path) {
                if (path.empty()) return;
                for (const std::string& candidate :
                     Assets::MeshCache::buildCandidatePaths(path.string(), projectRoot)) {
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) && !ec) {
                        textureFound = true;
                        return;
                    }
                }
            };
            checkTexturePath(mf->customTexturePath);
            if (!textureFound && EditorPaths::isReady()) {
                checkTexturePath(EditorPaths::resolve(mf->customTexturePath));
            }
            if (!textureFound) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                   "Texture not found: %s", mf->customTexturePath.c_str());
            }
        }
        if (!mf->customMeshPath.empty() && ImGui::Button("Extract Materials")) {
            const std::string projectRoot = resolveProjectRoot(ctx).string();
            if (Assets::Mesh3D* mesh =
                    Assets::MeshCache::getInstance().getMesh(mf->customMeshPath, projectRoot)) {
                std::filesystem::path meshFile(mf->customMeshPath);
                const auto root = resolveProjectRoot(ctx);
                if (!meshFile.is_absolute() && !root.empty()) meshFile = root / meshFile;
                const auto materials = Assets::exportMeshMaterials(*mesh, meshFile, root, true);
                if (!materials.empty()) syncMaterial(materials.front());
            }
        }
        if (!mf->customMeshPath.empty()) {
            const std::string ext = std::filesystem::path(mf->customMeshPath).extension().string();
            if (ext == ".fbx" || ext == ".FBX") {
                ImGui::TextDisabled("FBX needs an OBJ/glTF export (same name) for preview.");
            }
            if (ext == ".gltf" || ext == ".GLTF") {
                ImGui::TextDisabled("A separate glTF needs the .bin and textures in the same folder.");
            }

            std::string projectRoot = resolveProjectRoot(ctx).string();
            auto& meshCache = Assets::MeshCache::getInstance();
            if (!meshCache.getMesh(mf->customMeshPath, projectRoot)) {
                const std::string& err = meshCache.getLastError();
                if (!err.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", err.c_str());
                }
            }
        }
    }

    if (!world.has<ECS::TerrainComponent>(e)) {
        if (Widgets::AssetField(ctx, "Material", mf->customMaterialPath, ".mat")) {
            syncMaterial(mf->customMaterialPath);
        }
        if (!mf->customMaterialPath.empty() && ImGui::Button("Edit Material")) {
            ctx.materialToOpen = mf->customMaterialPath;
        }
    }

    if (!world.has<ECS::MeshRendererComponent>(e)) {
        world.add<ECS::MeshRendererComponent>(e);
    }
    if (auto* mr = world.get<ECS::MeshRendererComponent>(e)) {
        ImGui::Separator();
        if (ImGui::Checkbox("Cast Shadows", &mr->castShadows)) ctx.markDirty();
        if (ImGui::Checkbox("Receive Shadows", &mr->receiveShadows)) ctx.markDirty();
    }
}

void InspectorPanel::drawUIWidget(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* w = world.get<UI::UIWidget>(e);
    if (!w) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Widget", enabled, removeRequested, "align-left")) return;
    if (removeRequested) {
        world.remove<UI::UIWidget>(e);
        ctx.markDirty();
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
    ctx.markDirty();
}

void InspectorPanel::drawUIButton(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* btn = world.get<UI::UIButton>(e);
    if (!btn) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Button", enabled, removeRequested, "arrow-down-square")) return;
    if (removeRequested) {
        world.remove<UI::UIButton>(e);
        ctx.markDirty();
        return;
    }

    char buf[64];
    strncpy(buf, btn->labelText.cStr(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Label", buf, sizeof(buf))) {
        btn->labelText = buf;
        ctx.markDirty();
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
    if (!Widgets::ComponentHeader("UI Label", enabled, removeRequested, "align-left")) return;
    if (removeRequested) {
        world.remove<UI::UILabel>(e);
        ctx.markDirty();
        return;
    }

    char buf[256];
    strncpy(buf, lbl->text.cStr(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputTextMultiline("Text", buf, sizeof(buf), ImVec2(-1, 60))) {
        lbl->text = buf;
        ctx.markDirty();
    }
    ImGui::Checkbox("Word Wrap", &lbl->wordWrap);
}

void InspectorPanel::drawUIProgressBar(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* pb = world.get<UI::UIProgressBar>(e);
    if (!pb) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Progress Bar", enabled, removeRequested, "arrows-horizontal")) return;
    if (removeRequested) {
        world.remove<UI::UIProgressBar>(e);
        ctx.markDirty();
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
    ctx.markDirty();
}

void InspectorPanel::drawUISlider(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    auto* sl = world.get<UI::UISlider>(e);
    if (!sl) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("UI Slider", enabled, removeRequested, "arrows-horizontal")) return;
    if (removeRequested) {
        world.remove<UI::UISlider>(e);
        ctx.markDirty();
        return;
    }

    ImGui::DragFloat("Min Value",     &sl->minValue, 1.0f);
    ImGui::DragFloat("Max Value",     &sl->maxValue, 1.0f);
    ImGui::SliderFloat("Value",       &sl->currentValue, sl->minValue, sl->maxValue);
    ImGui::Checkbox("Snap To Int",    &sl->snapToInt);
    ctx.markDirty();
}

std::filesystem::path InspectorPanel::resolveProjectRoot(const EditorContext& ctx) const {
    if (!ctx.projectRootPath.empty()) return ctx.projectRootPath;
    if (!ctx.currentScenePath.empty()) {
        return std::filesystem::path(ctx.currentScenePath).parent_path();
    }
    return {};
}

void InspectorPanel::drawCppScript(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Script::CppScriptComponent>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("C++ Script", enabled, removeRequested, "at")) return;
    if (removeRequested) {
        world.remove<Script::CppScriptComponent>(e);
        ctx.markDirty();
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
        ctx.markDirty();
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

    if (csc->instance) {
        std::vector<Script::ExposedScriptField> fields;
        csc->instance->gatherExposedFields(fields);
        if (!fields.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Properties");
            for (const auto& field : fields) {
                if (!field.ptr || !field.name) continue;
                ImGui::PushID(field.name);
                if (field.type == Script::ExposedScriptField::Type::Float) {
                    ImGui::DragFloat(field.name, static_cast<float*>(field.ptr), 0.05f);
                } else if (field.type == Script::ExposedScriptField::Type::Int) {
                    ImGui::DragInt(field.name, static_cast<int*>(field.ptr));
                } else if (field.type == Script::ExposedScriptField::Type::Bool) {
                    ImGui::Checkbox(field.name, static_cast<bool*>(field.ptr));
                }
                ImGui::PopID();
            }
        }
    }
}

// ── Light drawer ─────────────────────────────────────────────────

void InspectorPanel::drawSkybox(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::SkyboxComponent>(e)) return;

    auto* sky = world.get<ECS::SkyboxComponent>(e);
    if (!sky) return;

    bool enabled = sky->enabled;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Skybox", enabled, removeRequested, "weather-sunny")) return;
    if (removeRequested) {
        world.remove<ECS::SkyboxComponent>(e);
        ctx.markDirty();
        return;
    }
    if (enabled != sky->enabled) {
        sky->enabled = enabled;
        ctx.markDirty();
    }

    if (sky->customTexturePath[0] == '\0' &&
        sky->presetIndex >= 0 && sky->presetIndex < ECS::kSkyboxPresetCount) {
        const char* file = ECS::kSkyboxPresetFiles[sky->presetIndex];
        const char* slash = std::strrchr(file, '/');
        const char* name = slash ? slash + 1 : file;
        const std::string relative = std::string("assets/raw/sky/") + name;
        std::strncpy(sky->customTexturePath, relative.c_str(), sizeof(sky->customTexturePath) - 1);
        ctx.markDirty();
    }

    std::string texture = sky->customTexturePath;
    ImGui::PushID("skybox");
    if (Widgets::AssetField(ctx, "Texture", texture, ".png;.jpg;.jpeg;.hdr")) {
        std::memset(sky->customTexturePath, 0, sizeof(sky->customTexturePath));
        std::strncpy(sky->customTexturePath, texture.c_str(), sizeof(sky->customTexturePath) - 1);
        sky->customTexturePath[sizeof(sky->customTexturePath) - 1] = '\0';
        if (sky->customTexturePath[0] == '\0') sky->presetIndex = -1;
        ctx.markDirty();
    }
    ImGui::PopID();
    ImGui::TextDisabled("Empty texture uses the clear sky.");

    if (ImGui::DragFloat("Exposure", &sky->exposure, 0.01f, 0.0f, 8.0f, "%.2f")) {
        ctx.markDirty();
    }
}

void InspectorPanel::drawEnvironmentEffects(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    ECS::EnvironmentEffectsComponent* fx = world.get<ECS::EnvironmentEffectsComponent>(e);
    if (!fx) return;
    bool enabled = fx->enabled != 0;
    bool removeRequested = false;
    const bool open = Widgets::ComponentHeader("Environment Effects", enabled, removeRequested, "weather-sunny");
    if (enabled != (fx->enabled != 0)) {
        fx->enabled = enabled ? 1 : 0;
        ctx.markDirty();
    }
    if (removeRequested) {
        world.remove<ECS::EnvironmentEffectsComponent>(e);
        ctx.markDirty();
        return;
    }
    if (!open) return;

    const char* kinds[] = {"World Light Simulation"};
    int kind = static_cast<int>(fx->kind);
    if (ImGui::Combo("Effect", &kind, kinds, 1)) {
        fx->kind = static_cast<u8>(kind);
        ctx.markDirty();
    }
    const char* modes[] = {"Day / Night", "Zone"};
    int mode = static_cast<int>(fx->mode);
    if (ImGui::Combo("Mode", &mode, modes, 2)) {
        fx->mode = static_cast<u8>(mode);
        ctx.markDirty();
    }
    bool driveLights = fx->driveExistingLights != 0;
    if (ImGui::Checkbox("Drive sun directional", &driveLights)) {
        fx->driveExistingLights = driveLights ? 1 : 0;
        ctx.markDirty();
    }
    ImGui::TextDisabled("Day / Night uses one Directional Light as the sun. Other lights stay as fill.");
    bool castShadows = fx->castShadows != 0;
    if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
        fx->castShadows = castShadows ? 1 : 0;
        ctx.markDirty();
    }
    bool envLight = fx->environmentLighting != 0;
    if (ImGui::Checkbox("Environment Lighting", &envLight)) {
        fx->environmentLighting = envLight ? 1 : 0;
        ctx.markDirty();
    }
    bool ao = fx->ambientOcclusion != 0;
    if (ImGui::Checkbox("Ambient Occlusion", &ao)) {
        fx->ambientOcclusion = ao ? 1 : 0;
        ctx.markDirty();
    }
    bool volumes = fx->volumetricsEnabled != 0;
    if (ImGui::Checkbox("Volumetrics", &volumes)) {
        fx->volumetricsEnabled = volumes ? 1 : 0;
        ctx.markDirty();
    }
    ImGui::TextDisabled("Sky visibility %.0f%%  ·  enclosed rooms kill sky fill.",
                        fx->skyVisibility * 100.0f);
    if (fx->mode == static_cast<u8>(ECS::WorldLightMode::DayNight)) {
        if (ImGui::SliderFloat("Time of Day", &fx->timeOfDay, 0.0f, 24.0f, "%.2f h")) ctx.markDirty();
        if (ImGui::DragFloat("Day Length", &fx->dayLengthSeconds, 1.0f, 0.0f, 3600.0f, "%.0f s")) {
            ctx.markDirty();
        }
        ImGui::TextDisabled("0 s = scrub only. Play advances the clock.");
        if (ImGui::DragFloat("Sun Intensity", &fx->sunIntensity, 0.01f, 0.0f, 8.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Moon Intensity", &fx->moonIntensity, 0.01f, 0.0f, 4.0f)) ctx.markDirty();
        float sun[3] = {fx->sunColor.x, fx->sunColor.y, fx->sunColor.z};
        if (ImGui::ColorEdit3("Sun Color", sun)) {
            fx->sunColor = Vec3(sun[0], sun[1], sun[2]);
            ctx.markDirty();
        }
        float moon[3] = {fx->moonColor.x, fx->moonColor.y, fx->moonColor.z};
        if (ImGui::ColorEdit3("Moon Color", moon)) {
            fx->moonColor = Vec3(moon[0], moon[1], moon[2]);
            ctx.markDirty();
        }
        if (ImGui::SliderFloat("Ambient Day", &fx->ambientDay, 0.0f, 1.5f)) ctx.markDirty();
        if (ImGui::SliderFloat("Ambient Night", &fx->ambientNight, 0.0f, 1.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Shadow Strength", &fx->shadowStrength, 0.0f, 1.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Indoor Darkness", &fx->indoorDarkness, 0.0f, 0.98f)) ctx.markDirty();
        if (ImGui::SliderFloat("AO Day", &fx->aoDay, 0.0f, 1.5f)) ctx.markDirty();
        if (ImGui::SliderFloat("AO Night", &fx->aoNight, 0.0f, 2.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Volumetric Density", &fx->volumetricDensity, 0.001f, 0.0f, 0.25f, "%.3f")) {
            ctx.markDirty();
        }
        ImGui::TextDisabled("Sun shafts need an opening and Cast Shadows. Point / spot lights glow in the volume.");
    } else {
        if (ImGui::DragFloat("Radius", &fx->zoneRadius, 0.1f, 0.25f, 250.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Intensity", &fx->zoneIntensity, 0.01f, 0.0f, 8.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Falloff", &fx->zoneFalloff, 0.2f, 4.0f)) ctx.markDirty();
        float zone[3] = {fx->zoneColor.x, fx->zoneColor.y, fx->zoneColor.z};
        if (ImGui::ColorEdit3("Zone Color", zone)) {
            fx->zoneColor = Vec3(zone[0], zone[1], zone[2]);
            ctx.markDirty();
        }
        ImGui::TextDisabled("Tints 3D ambient and 2D sprites inside the radius.");
    }
}

void InspectorPanel::drawTerrain(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::TerrainComponent>(e)) return;

    auto* terrain = world.get<ECS::TerrainComponent>(e);
    if (!terrain) return;

    ImGui::PushID(static_cast<int>(e.id()));
    const std::string projectRoot = resolveProjectRoot(ctx).string();

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Terrain", enabled, removeRequested, "terrain")) {
        ImGui::PopID();
        return;
    }
    if (removeRequested) {
        Terrain::TerrainCache::instance().removeEntity(e);
        world.remove<ECS::TerrainComponent>(e);
        ctx.markDirty();
        ImGui::PopID();
        return;
    }

    int resX = static_cast<int>(terrain->resolutionX);
    int resZ = static_cast<int>(terrain->resolutionZ);
    if (ImGui::SliderInt("Resolution X", &resX, 17, 513)) {
        terrain->resolutionX = static_cast<u32>(resX);
        terrain->dataRevision++;
        Terrain::TerrainCache::instance().syncEntity(world, e);
        ctx.markDirty();
    }
    if (ImGui::SliderInt("Resolution Z", &resZ, 17, 513)) {
        terrain->resolutionZ = static_cast<u32>(resZ);
        terrain->dataRevision++;
        Terrain::TerrainCache::instance().syncEntity(world, e);
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Heightmap vertex count. Existing heights are resampled when changed.");
    }
    int splatScale = static_cast<int>(terrain->splatResolutionScale);
    if (ImGui::SliderInt("Splat Resolution Scale", &splatScale, 1, 8)) {
        terrain->splatResolutionScale = static_cast<u32>(splatScale);
        terrain->splatRevision++;
        Terrain::TerrainCache::instance().syncEntity(world, e);
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Splat resolution: %u x %u (height scale x%d)",
                          Terrain::splatResolutionX(*terrain),
                          Terrain::splatResolutionZ(*terrain),
                          splatScale);
    }
    ImGui::TextDisabled("1 world unit = 1 meter");
    if (ImGui::DragFloat("World Size X (m)", &terrain->worldSizeX, 1.0f, 16.0f, 10000.0f, "%.1f m")) {
        terrain->dataRevision++;
        ctx.markDirty();
    }
    if (ImGui::DragFloat("World Size Z (m)", &terrain->worldSizeZ, 1.0f, 16.0f, 10000.0f, "%.1f m")) {
        terrain->dataRevision++;
        ctx.markDirty();
    }
    if (ImGui::DragFloat("Max Height (m)", &terrain->maxHeight, 0.5f, 4.0f, 2000.0f, "%.1f m")) {
        terrain->dataRevision++;
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        const f32 suggested = Caffeine::WorldUnits::suggestedHeightM(
            std::max(terrain->worldSizeX, terrain->worldSizeZ));
        ImGui::SetTooltip(
            "Local relief in meters. Heightmap 0–1 maps into this range without stretching.\n"
            "Suggested for this footprint: ~%.0f m (about 12%% of width). 1000 m tall on a 1000 m "
            "map is a crater wall, not a valley.",
            suggested);
    }
    if (ImGui::Checkbox("Cast Shadows", &terrain->castShadows)) {
        ctx.markDirty();
    }
    if (ImGui::Checkbox("Receive Shadows", &terrain->receiveShadows)) {
        ctx.markDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Surface");
    if (ImGui::InputText("Normal Map", terrain->normalMapPath, sizeof(terrain->normalMapPath))) {
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(e, nullptr);
#endif
        ctx.markDirty();
    }
    if (ImGui::SliderFloat("Shininess", &terrain->shininess, 1.0f, 128.0f)) {
        ctx.markDirty();
    }
    if (ImGui::InputText("Albedo Texture", terrain->texturePath, sizeof(terrain->texturePath))) {
        Terrain::TerrainCache::instance().repairTexturePaths(*terrain);
        Terrain::TerrainCache::instance().syncTextureToFilter(world, e, *terrain);
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(e, nullptr);
#endif
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Engine asset path or project-relative texture");
    }
    if (terrain->texturePath[0] != '\0' &&
        Assets::MeshCache::resolveTexturePath(terrain->texturePath, projectRoot).empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Texture not found: %s", terrain->texturePath);
    }
    if (ImGui::Button("Reset Default Textures")) {
        const ECS::TerrainComponent defaults;
        std::strncpy(terrain->texturePath, defaults.texturePath, sizeof(terrain->texturePath) - 1);
        terrain->texturePath[sizeof(terrain->texturePath) - 1] = '\0';
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            std::strncpy(terrain->splatLayerPaths[i], defaults.splatLayerPaths[i],
                          sizeof(terrain->splatLayerPaths[i]) - 1);
            terrain->splatLayerPaths[i][sizeof(terrain->splatLayerPaths[i]) - 1] = '\0';
        }
        Terrain::TerrainCache::instance().syncTextureToFilter(world, e, *terrain);
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(e, nullptr);
#endif
        terrain->splatRevision++;
        ctx.markDirty();
    }
    if (ImGui::DragFloat("Tile Size", &terrain->textureTileSize, 0.25f, 1.0f, 128.0f, "%.1f")) {
        terrain->dataRevision++;
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("World units covered by one texture repeat");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Optimization");
    if (ImGui::Checkbox("Chunked LOD", &terrain->useChunks)) {
        terrain->dataRevision++;
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Split terrain into chunks with distance-based LOD and frustum culling");
    }
    if (terrain->useChunks) {
        int chunkVerts = static_cast<int>(terrain->chunkVertexCount);
        if (ImGui::SliderInt("Chunk Resolution", &chunkVerts, 9, 129)) {
            terrain->chunkVertexCount = static_cast<u32>(chunkVerts);
            terrain->dataRevision++;
            ctx.markDirty();
        }
        int maxLod = static_cast<int>(terrain->maxLodLevels);
        if (ImGui::SliderInt("LOD Levels", &maxLod, 1, 6)) {
            terrain->maxLodLevels = static_cast<u32>(maxLod);
            terrain->dataRevision++;
            ctx.markDirty();
        }
        if (ImGui::DragFloat("LOD Distance", &terrain->lodDistanceScale, 1.0f, 32.0f, 4096.0f, "%.0f")) {
            terrain->dataRevision++;
            ctx.markDirty();
        }
        if (ImGui::SliderFloat("LOD Hysteresis", &terrain->lodHysteresis, 0.0f, 0.9f, "%.2f")) {
            ctx.markDirty();
        }
        if (ImGui::Checkbox("Frustum Cull", &terrain->frustumCull)) {
            ctx.markDirty();
        }
        if (ImGui::Checkbox("Show Chunk Debug", &ctx.terrainShowChunkDebug)) {
            ctx.markDirty();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Splatmap");
    if (ImGui::Checkbox("Use Splatmap", &terrain->useSplatmap)) {
        Terrain::TerrainCache::instance().syncTextureToFilter(world, e, *terrain);
        ctx.markDirty();
    }
    if (terrain->useSplatmap) {
        const char* layerLabels[] = {"Grass", "Rock", "Sand", "Dirt"};
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            char label[32];
            snprintf(label, sizeof(label), "%s Texture", layerLabels[i]);
            if (ImGui::InputText(label, terrain->splatLayerPaths[i],
                                 sizeof(terrain->splatLayerPaths[i]))) {
                Terrain::TerrainCache::instance().repairTexturePaths(*terrain);
                Terrain::TerrainCache::instance().syncTextureToFilter(world, e, *terrain);
#ifdef CF_HAS_SDL3
                Terrain::TerrainGpuTextureCache::instance().invalidateEntity(e, nullptr);
#endif
                terrain->splatRevision++;
                ctx.markDirty();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", terrain->splatLayerPaths[i]);
            }
            if (terrain->splatLayerPaths[i][0] != '\0' &&
                Assets::MeshCache::resolveTexturePath(terrain->splatLayerPaths[i], projectRoot)
                    .empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                   "Texture not found: %s", terrain->splatLayerPaths[i]);
            }
        }
        if (ImGui::DragFloat("Splat Tile Size", &terrain->splatTileSize, 0.25f, 1.0f, 128.0f, "%.1f")) {
            ctx.markDirty();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Collision");
    int collisionStep = static_cast<int>(terrain->collisionSampleStep);
    if (ImGui::SliderInt("Collision Sample Step", &collisionStep, 1, 16)) {
        terrain->collisionSampleStep = static_cast<u32>(collisionStep);
        terrain->dataRevision++;
        Terrain::TerrainCache::instance().syncEntity(world, e);
        ctx.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher = fewer collision triangles (render mesh unchanged)");
    }
    if (ImGui::Checkbox("Build Collision Mesh", &terrain->buildCollisionMesh)) {
        terrain->dataRevision++;
        Terrain::TerrainCache::instance().syncEntity(world, e);
        ctx.markDirty();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Sculpt/paint: Terrain Editor. Generation: Terrain Generator plugin.");
    ImGui::PopID();
}

void InspectorPanel::drawForwardRenderFeatures(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::ForwardRenderFeaturesComponent>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Forward Render Features", enabled, removeRequested, "sparkles")) return;
    if (removeRequested) {
        world.remove<ECS::ForwardRenderFeaturesComponent>(e);
        ctx.markDirty();
        return;
    }

    auto* fx = world.get<ECS::ForwardRenderFeaturesComponent>(e);
    if (!fx) return;

    ImGui::PushID("forwardrender");
    ImGui::TextWrapped(
        "Scene-owned forward pass: instancing, IBL, occlusion, reflections, volumetrics. "
        "Not stored in editor preferences — use this component, Lua, or C++.");
    if (ImGui::Checkbox("Component enabled", &fx->enabled)) ctx.markDirty();

    if (ImGui::CollapsingHeader("Instancing", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Enabled", &fx->instancing.enabled)) ctx.markDirty();
        int maxBatch = static_cast<int>(fx->instancing.maxInstancesPerBatch);
        if (ImGui::SliderInt("Max / batch", &maxBatch, 2, 256)) {
            fx->instancing.maxInstancesPerBatch = static_cast<u32>(maxBatch);
            ctx.markDirty();
        }
    }
    if (ImGui::CollapsingHeader("IBL", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Diffuse IBL", &fx->ibl.enabled)) ctx.markDirty();
        ImGui::TextDisabled("Sky as fill light. Metals still reflect the sky via Specular.");
        if (ImGui::SliderFloat("Diffuse", &fx->ibl.diffuse, 0.0f, 2.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Specular", &fx->ibl.specular, 0.0f, 2.0f)) ctx.markDirty();
    }
    if (ImGui::CollapsingHeader("Occlusion")) {
        if (ImGui::Checkbox("Coarse CPU occlusion", &fx->occlusion.enabled)) ctx.markDirty();
        int maxOcc = static_cast<int>(fx->occlusion.maxOccluders);
        if (ImGui::SliderInt("Max occluders", &maxOcc, 1, 16)) {
            fx->occlusion.maxOccluders = static_cast<u32>(maxOcc);
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Min radius", &fx->occlusion.minRadius, 0.05f, 0.1f, 20.0f)) ctx.markDirty();
    }
    if (ImGui::CollapsingHeader("Reflections")) {
        if (ImGui::Checkbox("Enabled", &fx->reflections.enabled)) ctx.markDirty();
        const char* modes[] = {"Off", "Planar", "Screen-space", "Probe"};
        int mode = static_cast<int>(fx->reflections.mode);
        if (ImGui::Combo("Mode", &mode, modes, 4)) {
            fx->reflections.mode = static_cast<Render::ReflectionMode>(mode);
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Plane Y", &fx->reflections.planeY, 0.05f, -50.0f, 50.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Intensity", &fx->reflections.intensity, 0.0f, 1.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Resolution scale", &fx->reflections.resolutionScale, 0.25f, 1.0f)) {
            ctx.markDirty();
        }
        int steps = static_cast<int>(fx->reflections.ssrMaxSteps);
        if (ImGui::SliderInt("SSR steps", &steps, 1, 12)) {
            fx->reflections.ssrMaxSteps = static_cast<u32>(steps);
            ctx.markDirty();
        }
        if (ImGui::DragFloat("SSR distance", &fx->reflections.ssrMaxDistance, 0.25f, 0.5f, 40.0f)) {
            ctx.markDirty();
        }
        int probe = static_cast<int>(fx->reflections.probeResolution);
        if (ImGui::SliderInt("Probe resolution", &probe, 16, 128)) {
            fx->reflections.probeResolution = static_cast<u32>(probe);
            ctx.markDirty();
        }
        if (ImGui::Checkbox("Only when camera settled", &fx->reflections.expensiveOnlyWhenSettled)) {
            ctx.markDirty();
        }
    }
    if (ImGui::CollapsingHeader("Volumetrics")) {
        if (ImGui::Checkbox("Enabled", &fx->volumetrics.enabled)) ctx.markDirty();
        const char* qualities[] = {"Off", "Low", "Medium", "High"};
        int quality = static_cast<int>(fx->volumetrics.quality);
        if (ImGui::Combo("Quality", &quality, qualities, 4)) {
            fx->volumetrics.quality = static_cast<Render::VolumetricQuality>(quality);
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Density", &fx->volumetrics.density, 0.001f, 0.0f, 0.2f, "%.3f")) {
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Height", &fx->volumetrics.height, 0.1f, 0.5f, 80.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Anisotropy", &fx->volumetrics.anisotropy, 0.0f, 0.9f)) ctx.markDirty();
        if (ImGui::Checkbox("Sample shadows", &fx->volumetrics.sampleShadows)) ctx.markDirty();
        ImGui::TextDisabled("High + Sample shadows uses the sun cascade along the view ray.");
    }
    ImGui::PopID();
}

void InspectorPanel::drawPostProcess(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::PostProcessComponent>(e)) return;

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Post Process", enabled, removeRequested, "sparkles")) return;
    if (removeRequested) {
        world.remove<ECS::PostProcessComponent>(e);
        ctx.markDirty();
        return;
    }

    auto* fx = world.get<ECS::PostProcessComponent>(e);
    if (!fx) return;

    ImGui::PushID("postprocess");
    const u32 typeId = ComponentTypeRegistry::instance().lookup("PostProcess");
    if (PluginManager::instance().isComponentDrawerEnabled(typeId)) {
        if (const ComponentDrawer* drawer = m_drawers.get(typeId)) {
            (*drawer)(fx);
            ImGui::PopID();
            return;
        }
    }
    ImGui::TextDisabled("Core post stack. These fields drive the Caffeine renderer.");
    if (ImGui::Checkbox("Enabled", &fx->enabled)) ctx.markDirty();
    if (ImGui::DragFloat("Exposure", &fx->colorGrading.exposure, 0.01f, 0.0f, 16.0f)) ctx.markDirty();
    if (ImGui::DragFloat("Contrast", &fx->colorGrading.contrast, 0.01f, 0.0f, 3.0f)) ctx.markDirty();
    if (ImGui::DragFloat("Saturation", &fx->colorGrading.saturation, 0.01f, 0.0f, 3.0f)) ctx.markDirty();
    if (ImGui::Checkbox("Bloom", &fx->bloom.enabled)) ctx.markDirty();
    if (fx->bloom.enabled && ImGui::DragFloat("Bloom Intensity", &fx->bloom.intensity, 0.01f, 0.0f, 4.0f)) {
        ctx.markDirty();
    }
    if (ImGui::Checkbox("Ambient Occlusion", &fx->ambientOcclusion.enabled)) ctx.markDirty();
    if (fx->ambientOcclusion.enabled &&
        ImGui::DragFloat("AO Intensity", &fx->ambientOcclusion.intensity, 0.01f, 0.0f, 4.0f)) {
        ctx.markDirty();
    }
    ImGui::PopID();
}

void InspectorPanel::drawLight(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<ECS::LightComponent>(e)) return;

    const char* lightLabel = "Light";
    if (world.has<ECS::DirectionalLightComponent>(e)) lightLabel = "Directional Light";
    else if (world.has<ECS::PointLightComponent>(e))  lightLabel = "Point Light";
    else if (world.has<ECS::SpotLightComponent>(e))   lightLabel = "Spot Light";

    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader(lightLabel, enabled, removeRequested, "bell-alert")) return;
    if (removeRequested) {
        world.remove<ECS::LightComponent>(e);
        if (world.has<ECS::DirectionalLightComponent>(e)) world.remove<ECS::DirectionalLightComponent>(e);
        if (world.has<ECS::PointLightComponent>(e))       world.remove<ECS::PointLightComponent>(e);
        if (world.has<ECS::SpotLightComponent>(e))        world.remove<ECS::SpotLightComponent>(e);
        ctx.markDirty();
        return;
    }

    auto* light = world.get<ECS::LightComponent>(e);

    float col[4] = { light->color.x, light->color.y, light->color.z, light->color.w };
    if (ImGui::ColorEdit4("Color", col)) {
        light->color = Vec4(col[0], col[1], col[2], col[3]);
        ctx.markDirty();
    }

    if (ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100.0f, "%.2f")) {
        ctx.markDirty();
    }

    if (world.has<ECS::DirectionalLightComponent>(e)) {
        auto* dir = world.get<ECS::DirectionalLightComponent>(e);
        if (ImGui::DragFloat("Shadow Distance", &dir->shadowDistance, 1.0f, 1.0f, 10000.0f)) ctx.markDirty();
        if (ImGui::Checkbox("Cast Shadows", &dir->castShadows)) ctx.markDirty();
    }

    if (world.has<ECS::PointLightComponent>(e)) {
        auto* pl = world.get<ECS::PointLightComponent>(e);
        if (ImGui::DragFloat("Radius", &pl->radius, 0.5f, 0.1f, 1000.0f)) ctx.markDirty();
        if (ImGui::Checkbox("Cast Shadows", &pl->castShadows)) ctx.markDirty();
    }

    if (world.has<ECS::SpotLightComponent>(e)) {
        auto* sl = world.get<ECS::SpotLightComponent>(e);
        if (ImGui::DragFloat("Radius", &sl->radius, 0.5f, 0.1f, 1000.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Angle", &sl->angle, 0.5f, 1.0f, 179.0f, "%.1f deg")) ctx.markDirty();
        if (ImGui::Checkbox("Cast Shadows", &sl->castShadows)) ctx.markDirty();
    }
}

void InspectorPanel::drawPrefabInstance(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    const ECS::Entity instanceRoot = PrefabSystem::FindInstanceRoot(world, e);
    if (!instanceRoot.isValid()) return;

    auto* inst = world.get<ECS::PrefabInstance>(instanceRoot);
    if (!inst) return;

    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Prefab Instance");
    ImGui::TextDisabled("%s", inst->prefabPath.c_str());
    if (!inst->overrides.empty()) {
        ImGui::TextDisabled("%zu override(s)", inst->overrides.size());
    }

    if (ImGui::Button("Revert All", ImVec2(-1, 0))) {
        ctx.beginUndo(EditorCommand::SetField, instanceRoot.id(), world);
        PrefabSystem::RevertOverrides(world, instanceRoot);
        ctx.selectEntity(instanceRoot);
        ctx.endUndo(world);
        ctx.markDirty();
    }
    if (ImGui::Button("Apply to Prefab", ImVec2(-1, 0))) {
        ctx.beginUndo(EditorCommand::SetField, instanceRoot.id(), world);
        if (PrefabSystem::ApplyOverridesToPrefab(world, instanceRoot)) {
            ctx.selectEntity(instanceRoot);
        }
        ctx.endUndo(world);
        ctx.markDirty();
    }
    ImGui::Separator();
}

void InspectorPanel::savePrefab(ECS::World& world, ECS::Entity e, const std::filesystem::path& path) {
    if (!e.isValid()) {
        std::cerr << "Error: Invalid entity. Cannot save prefab.\n";
        return;
    }

    if (path.empty()) {
        std::cerr << "Error: Invalid path. Cannot save prefab.\n";
        return;
    }

    const bool success = PrefabSystem::CreateFromEntity(world, e, path);

    if (success) {
        std::cout << "Prefab saved successfully: " << path.string() << "\n";
    } else {
        std::cerr << "Error: Failed to save prefab to " << path.string() << "\n";
    }
}

void InspectorPanel::drawEffect(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(e);
    if (!effect) return;
    bool enabled = effect->enabled != 0;
    bool removeRequested = false;
    const bool open = Widgets::ComponentHeader("Effect", enabled, removeRequested, "beer");
    if (enabled != (effect->enabled != 0)) {
        effect->enabled = enabled ? 1 : 0;
        ctx.markDirty();
    }
    if (removeRequested) {
        world.remove<Effects::EffectComponent>(e);
        ctx.markDirty();
        return;
    }
    if (!open) return;
    const char* kinds[] = {"Particles", "Volumetric Light", "Reflective Surface", "Material Shade", "Fog"};
    int kind = static_cast<int>(effect->kind);
    if (ImGui::Combo("Kind", &kind, kinds, 5)) {
        effect->kind = static_cast<u8>(kind);
        if (effect->kind == static_cast<u8>(Effects::EffectKind::ReflectiveSurface) && effect->reflection <= 0.0f) {
            effect->reflection = 0.85f;
        }
        if (effect->kind == static_cast<u8>(Effects::EffectKind::Fog)) {
            Effects::configureFog(*effect, static_cast<Effects::EffectDomain>(effect->domain),
                                  static_cast<Effects::EffectFogStyle>(effect->pad0 > 3 ? 0 : effect->pad0));
        }
        ctx.markDirty();
    }
    const char* spaces[] = {"World", "Camera"};
    int space = static_cast<int>(effect->space);
    if (ImGui::Combo("Space", &space, spaces, 2)) {
        effect->space = static_cast<u8>(space);
        ctx.markDirty();
    }
    const char* domains[] = {"2D and 3D", "2D", "3D"};
    int domain = static_cast<int>(effect->domain);
    if (ImGui::Combo("Domain", &domain, domains, 3)) {
        effect->domain = static_cast<u8>(domain);
        ctx.markDirty();
    }
    const char* qualities[] = {"Performance", "Balanced", "Quality"};
    int quality = static_cast<int>(effect->quality);
    if (ImGui::Combo("Quality", &quality, qualities, 3)) {
        effect->quality = static_cast<u8>(quality);
        ctx.markDirty();
    }
    char path[260] = {};
    std::strncpy(path, effect->materialPath, sizeof(path) - 1);
    if (ImGui::InputText("Material", path, sizeof(path))) {
        std::memset(effect->materialPath, 0, sizeof(effect->materialPath));
        std::strncpy(effect->materialPath, path, sizeof(effect->materialPath) - 1);
        ctx.markDirty();
    }
    if (effect->kind == static_cast<u8>(Effects::EffectKind::Particles)) {
        if (ImGui::DragFloat("Rate", &effect->rate, 0.5f, 0.0f, 400.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Lifetime", &effect->lifetime, 0.02f, 0.05f, 20.0f)) ctx.markDirty();
        int cap = effect->maxParticles;
        if (ImGui::DragInt("Max", &cap, 1, 1, 2048)) {
            effect->maxParticles = cap;
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Start Size", &effect->startSize, 0.01f, 0.0f, 8.0f)) ctx.markDirty();
        if (ImGui::DragFloat("End Size", &effect->endSize, 0.01f, 0.0f, 8.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Gravity Y", &effect->gravity.y, 0.05f, -20.0f, 20.0f)) ctx.markDirty();
    } else if (effect->kind == static_cast<u8>(Effects::EffectKind::VolumetricLight)) {
        Effects::ensureVolumetricMesh(world, e, *effect);
        Effects::pullVolumetricSizeFromMesh(world, e, *effect);
        auto pushSize = [&]() { Effects::pushVolumetricSizeToMesh(world, e, *effect); };
        const char* volumeShapes[] = {"Sphere", "Cone", "Box", "Cylinder", "Window"};
        int shape = static_cast<int>(Effects::volumetricShapeOf(*effect));
        if (ImGui::Combo("Format", &shape, volumeShapes, 5)) {
            const Vec3 color = effect->lightColor;
            Effects::configureVolumetricLight(*effect, static_cast<Effects::VolumetricShape>(shape));
            effect->lightColor = color;
            Effects::adoptVolumetricMesh(world, e, *effect);
            ctx.markDirty();
        }
        ImGui::TextDisabled("The volume is the mesh. Move, rotate and scale it.");
        const Effects::VolumetricShape volume = Effects::volumetricShapeOf(*effect);
        if (volume == Effects::VolumetricShape::Sphere) {
            if (ImGui::DragFloat("Radius", &effect->radius, 0.05f, 0.1f, 40.0f)) {
                pushSize();
                ctx.markDirty();
            }
        } else {
            if (ImGui::DragFloat("Length", &effect->radius, 0.05f, 0.2f, 40.0f)) {
                pushSize();
                ctx.markDirty();
            }
            if (volume == Effects::VolumetricShape::Cone) {
                f32 baseRadius = effect->endSize * 0.5f;
                if (ImGui::DragFloat("Radius", &baseRadius, 0.01f, 0.05f, 12.0f)) {
                    effect->endSize = baseRadius * 2.0f;
                    pushSize();
                    ctx.markDirty();
                }
            } else if (volume == Effects::VolumetricShape::Cylinder) {
                if (ImGui::DragFloat("Diameter", &effect->startSize, 0.01f, 0.05f, 8.0f)) {
                    effect->endSize = effect->startSize;
                    pushSize();
                    ctx.markDirty();
                }
            } else {
                if (ImGui::DragFloat("Width", &effect->startSize, 0.01f, 0.05f, 12.0f)) {
                    pushSize();
                    ctx.markDirty();
                }
                if (ImGui::DragFloat("Height", &effect->endSize, 0.01f, 0.05f, 12.0f)) {
                    pushSize();
                    ctx.markDirty();
                }
            }
        }
        if (ImGui::DragFloat("Density", &effect->density, 0.005f, 0.0f, 2.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Anisotropy", &effect->anisotropy, 0.0f, 0.9f)) ctx.markDirty();
        if (ImGui::DragFloat("Intensity", &effect->intensity, 0.05f, 0.0f, 16.0f)) ctx.markDirty();
        if (ImGui::ColorEdit3("Light", &effect->lightColor.x)) ctx.markDirty();
    } else if (effect->kind == static_cast<u8>(Effects::EffectKind::Fog)) {
        const char* looks[] = {"Fog", "Dust", "Mist", "Smoke"};
        int style = static_cast<int>(effect->pad0 > 3 ? 0 : effect->pad0);
        if (ImGui::Combo("Look", &style, looks, 4)) {
            Effects::configureFog(*effect, static_cast<Effects::EffectDomain>(effect->domain),
                                  static_cast<Effects::EffectFogStyle>(style));
            ctx.markDirty();
        }
        if (ImGui::DragFloat("Radius", &effect->radius, 0.05f, 0.1f, 40.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Density", &effect->density, 0.005f, 0.0f, 2.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Intensity", &effect->intensity, 0.02f, 0.0f, 4.0f)) ctx.markDirty();
        if (ImGui::ColorEdit3("Color", &effect->lightColor.x)) ctx.markDirty();
    } else if (effect->kind == static_cast<u8>(Effects::EffectKind::ReflectiveSurface)) {
        if (ImGui::SliderFloat("Reflection", &effect->reflection, 0.0f, 1.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Roughness", &effect->roughness, 0.02f, 1.0f)) ctx.markDirty();
        if (ImGui::SliderFloat("Metallic", &effect->metallic, 0.0f, 1.0f)) ctx.markDirty();
        const char* modes[] = {"Inherit", "Probe", "Planar", "Screen"};
        int mode = static_cast<int>(effect->reflectMode);
        if (ImGui::Combo("Reflect Mode", &mode, modes, 4)) {
            effect->reflectMode = static_cast<u8>(mode);
            ctx.markDirty();
        }
        ImGui::TextDisabled("Performance 8 steps, balanced 24, quality 48. The scene reflection mode still chooses the technique.");
    } else {
        if (ImGui::ColorEdit3("Tint", &effect->tint.x)) ctx.markDirty();
        if (ImGui::DragFloat("Emission", &effect->emissionBoost, 0.05f, 0.0f, 8.0f)) ctx.markDirty();
        if (ImGui::DragFloat("Roughness Bias", &effect->roughnessBias, 0.01f, -1.0f, 1.0f)) ctx.markDirty();
    }
}

void InspectorPanel::drawSpriteSheet(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    Animation::SpriteSheet* sheet = world.get<Animation::SpriteSheet>(e);
    if (!sheet) return;
    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Sprite Sheet", enabled, removeRequested, "beer")) return;
    if (removeRequested) {
        world.remove<Animation::SpriteSheet>(e);
        ctx.markDirty();
        return;
    }
    int columns = static_cast<int>(sheet->columns);
    int rows = static_cast<int>(sheet->rows);
    int frames = static_cast<int>(sheet->frameCount);
    if (ImGui::DragInt("Columns", &columns, 1, 1, 64)) {
        sheet->columns = static_cast<u32>(columns);
        ctx.markDirty();
    }
    if (ImGui::DragInt("Rows", &rows, 1, 1, 64)) {
        sheet->rows = static_cast<u32>(rows);
        ctx.markDirty();
    }
    if (ImGui::DragInt("Frame Count", &frames, 1, 1, 4096)) {
        sheet->frameCount = static_cast<u32>(frames);
        ctx.markDirty();
    }
    ImGui::TextDisabled("The sprite frame picks the cell. Clips and the animator advance that index.");
}

void InspectorPanel::drawSkinnedPose(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    Animation::SkinnedPose* pose = world.get<Animation::SkinnedPose>(e);
    if (!pose) return;
    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Skeleton", enabled, removeRequested, "arrows-diagonal")) return;
    if (removeRequested) {
        world.remove<Animation::SkinnedPose>(e);
        ctx.markDirty();
        return;
    }
    std::string meshPath = pose->meshPath;
    ImGui::PushID("skinned-mesh");
    if (Widgets::AssetField(ctx, "Mesh", meshPath, ".gltf;.glb;.fbx;.obj")) {
        std::memset(pose->meshPath, 0, sizeof(pose->meshPath));
        std::strncpy(pose->meshPath, meshPath.c_str(), sizeof(pose->meshPath) - 1);
        pose->meshPath[sizeof(pose->meshPath) - 1] = '\0';
        pose->loaded = false;
        pose->humanoid = {};
        pose->clipIndex = 0;
        pose->time = 0.0f;
        ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(e);
        if (!filter) {
            world.add<ECS::MeshFilterComponent>(e);
            filter = world.get<ECS::MeshFilterComponent>(e);
        }
        if (filter) {
            filter->primitive = ECS::MeshPrimitive::Custom;
            filter->customMeshPath = pose->meshPath;
        }
        Assets::MeshCache::getInstance().getMesh(pose->meshPath, ctx.projectRootPath.string());
        ctx.markDirty();
    }
    ImGui::PopID();
    if (pose->meshPath[0] != '\0') {
        ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(e);
        if (!filter) {
            world.add<ECS::MeshFilterComponent>(e);
            filter = world.get<ECS::MeshFilterComponent>(e);
        }
        if (filter && filter->customMeshPath.empty()) {
            filter->primitive = ECS::MeshPrimitive::Custom;
            filter->customMeshPath = pose->meshPath;
        }
    }
    if (pose->meshPath[0] == '\0') {
        if (const ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(e)) {
            if (!filter->customMeshPath.empty()) {
                std::strncpy(pose->meshPath, filter->customMeshPath.c_str(), sizeof(pose->meshPath) - 1);
                pose->meshPath[sizeof(pose->meshPath) - 1] = '\0';
            }
        }
    }
    Assets::Mesh3D* boundMesh = nullptr;
    if (pose->meshPath[0] != '\0') {
        boundMesh = Assets::MeshCache::getInstance().getMesh(pose->meshPath, ctx.projectRootPath.string());
    }
    const std::string resolved = Assets::MeshCache::getInstance().getResolvedPath();
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    if (!skin && !resolved.empty()) skin = Animation::findImportedSkin(resolved);
    if (!skin && boundMesh && !resolved.empty()) {
        Assets::MeshLoader::ensureGltfSkin(resolved, boundMesh);
        skin = Animation::findImportedSkin(resolved);
        if (!skin) skin = Animation::findImportedSkin(pose->meshPath);
    }
    if (!skin) {
        ImGui::TextDisabled("No skin on this mesh. The file needs glTF skins / joints.");
        if (!Assets::MeshCache::getInstance().getLastError().empty()) {
            ImGui::TextWrapped("%s", Assets::MeshCache::getInstance().getLastError().c_str());
        }
    } else if (skin->synthesized) {
        ImGui::TextDisabled("Fitted humanoid (%d bones). This file has no authored glTF skin.",
                            static_cast<int>(skin->boneNames.size()));
    } else if (skin->clipNames.empty()) {
        ImGui::TextDisabled("Skin found (%d bones). This file has no animation clips.",
                            static_cast<int>(skin->boneNames.size()));
    } else {
        const char* preview = skin->clipNames[static_cast<size_t>(
            std::clamp(pose->clipIndex, 0, static_cast<i32>(skin->clipNames.size()) - 1))].c_str();
        if (ImGui::BeginCombo("Clip", preview)) {
            for (int i = 0; i < static_cast<int>(skin->clipNames.size()); ++i) {
                if (ImGui::Selectable(skin->clipNames[static_cast<size_t>(i)].c_str(), pose->clipIndex == i)) {
                    pose->clipIndex = i;
                    pose->time = 0.0f;
                    pose->playing = true;
                    ctx.markDirty();
                }
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::DragFloat("Time", &pose->time, 0.01f, 0.0f, 1000.0f)) ctx.markDirty();
    if (ImGui::DragFloat("Speed", &pose->speed, 0.01f, 0.0f, 8.0f)) ctx.markDirty();
    if (ImGui::Checkbox("Playing", &pose->playing)) ctx.markDirty();
    if (ImGui::Checkbox("Loop", &pose->loop)) ctx.markDirty();
    if (!skin || skin->boneNames.empty()) return;

    if (!ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::Checkbox("Show bones", &ctx.showBones)) {}
    ImGui::TextDisabled("%d bones%s", static_cast<int>(skin->boneNames.size()),
                        pose->humanoid.matched ? "  ·  humanoid" : "");
    if (ctx.skeletonEntityId == e.id() && ctx.skeletonBone >= 0 &&
        ctx.skeletonBone < static_cast<i32>(skin->skeleton.boneCount())) {
        const int boneIndex = ctx.skeletonBone;
        if (boneIndex < static_cast<int>(skin->boneNames.size())) {
            ImGui::Text("Bone: %s", skin->boneNames[static_cast<size_t>(boneIndex)].c_str());
        }
        if (pose->offsets.size() < skin->skeleton.boneCount()) pose->offsets.resize(skin->skeleton.boneCount());
        Animation::BonePose& bone = pose->offsets[static_cast<size_t>(boneIndex)];
        constexpr f32 kRadToDeg = 57.2957795f;
        constexpr f32 kDegToRad = 0.0174532925f;
        float rotation[3] = {bone.rotation.x * kRadToDeg, bone.rotation.y * kRadToDeg, bone.rotation.z * kRadToDeg};
        if (ImGui::DragFloat3("Rotation", rotation, 1.0f)) {
            bone.rotation = Vec3(rotation[0] * kDegToRad, rotation[1] * kDegToRad, rotation[2] * kDegToRad);
            pose->sampleKeys = false;
            ctx.markDirty();
        }
        if (ImGui::DragFloat3("Translation", &bone.translation.x, 0.01f)) {
            pose->sampleKeys = false;
            ctx.markDirty();
        }
        ImGui::TextDisabled("The joint gizmo does the same. Key on the timeline records it, Play repeats it.");
    }

    if (ImGui::Button(ctx.remapBones ? "Done remapping" : "Remap Bones")) {
        ctx.remapBones = !ctx.remapBones;
        ctx.skeletonEntityId = e.id();
        if (ctx.remapBones) {
            ctx.showBones = true;
            if (ctx.remapHumanoidSlot < 0 || ctx.remapHumanoidSlot >= Animation::kHumanoidBodyCount) {
                ctx.remapHumanoidSlot = 0;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto-map")) {
        pose->humanoid = Animation::matchHumanoid(*skin);
        pose->humanoid.manual = true;
        pose->loaded = true;
        ctx.markDirty();
    }
    if (skin->synthesized && boundMesh) {
        ImGui::SameLine();
        if (ImGui::Button("Refit to mesh")) {
            const std::string refitPath = !resolved.empty() ? resolved : std::string(pose->meshPath);
            if (Animation::synthesizeHumanoidSkin(refitPath, *boundMesh, true)) {
                if (const Animation::ImportedSkin* fitted = Animation::findImportedSkin(refitPath)) {
                    pose->humanoid = Animation::matchHumanoid(*fitted);
                    pose->humanoid.manual = true;
                    pose->loaded = true;
                    ctx.showBones = true;
                    ctx.markDirty();
                }
            }
        }
    }
    ImGui::TextDisabled("Remap: pick a slot, then click a bone here or in the viewport.");

    if (ctx.remapBones) {
        ImGui::Separator();
        ImGui::TextUnformatted("Remap Bones");
        const int boneCount = static_cast<int>(skin->boneNames.size());
        for (int slot = 0; slot < Animation::kHumanoidBodyCount; ++slot) {
            const auto kind = static_cast<Animation::HumanoidBone>(slot);
            const i32 bone = pose->humanoid.bones[slot];
            const char* boneName = "None";
            if (bone >= 0 && bone < boneCount) boneName = skin->boneNames[static_cast<size_t>(bone)].c_str();
            const std::string row = std::string(Animation::humanoidBoneName(kind)) + "  →  " + boneName;
            ImGui::PushID(slot);
            if (ImGui::Selectable(row.c_str(), ctx.remapHumanoidSlot == slot)) {
                ctx.remapHumanoidSlot = slot;
                ctx.skeletonEntityId = e.id();
            }
            ImGui::PopID();
        }
        if (ctx.skeletonBone >= 0 && ctx.remapHumanoidSlot >= 0 &&
            ImGui::Button("Assign selected bone")) {
            Animation::assignHumanoidBone(pose->humanoid,
                                          static_cast<Animation::HumanoidBone>(ctx.remapHumanoidSlot),
                                          ctx.skeletonBone);
            pose->loaded = true;
            ctx.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear this slot") && ctx.remapHumanoidSlot >= 0 &&
            ctx.remapHumanoidSlot < Animation::kHumanoidBodyCount) {
            pose->humanoid.bones[ctx.remapHumanoidSlot] = -1;
            pose->humanoid.manual = true;
            Animation::refreshHumanoidMatched(pose->humanoid);
            ctx.markDirty();
        }
        if (ImGui::Button("Clear all mappings")) {
            pose->humanoid = {};
            pose->humanoid.manual = true;
            ctx.markDirty();
        }
        ImGui::Separator();
    }

    auto mapHumanoidSlot = [&](int slot) {
        const auto kind = static_cast<Animation::HumanoidBone>(slot);
        const char* slotName = Animation::humanoidBoneName(kind);
        i32& bone = pose->humanoid.bones[slot];
        const int boneCount = static_cast<int>(skin->boneNames.size());
        const char* preview = "None";
        if (bone >= 0 && bone < boneCount) preview = skin->boneNames[static_cast<size_t>(bone)].c_str();
        ImGui::PushID(slot);
        if (ImGui::BeginCombo(slotName, preview)) {
            if (ImGui::Selectable("None", bone < 0)) {
                bone = -1;
                pose->humanoid.manual = true;
                Animation::refreshHumanoidMatched(pose->humanoid);
                ctx.markDirty();
            }
            for (int index = 0; index < boneCount; ++index) {
                ImGui::PushID(index);
                const bool chosen = bone == index;
                if (ImGui::Selectable(skin->boneNames[static_cast<size_t>(index)].c_str(), chosen)) {
                    Animation::assignHumanoidBone(pose->humanoid, kind, index);
                    pose->loaded = true;
                    ctx.skeletonEntityId = e.id();
                    ctx.skeletonBone = index;
                    ctx.markDirty();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    };

    if (ImGui::TreeNode("Humanoid mapping")) {
        for (int slot = 0; slot < Animation::kHumanoidBodyCount; ++slot) mapHumanoidSlot(slot);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Fingers")) {
        if (ImGui::Button("Auto-map fingers")) {
            const Animation::HumanoidRig mapped = Animation::matchHumanoid(*skin);
            for (int slot = Animation::kHumanoidBodyCount; slot < static_cast<int>(Animation::HumanoidBone::Count);
                 ++slot) {
                pose->humanoid.bones[slot] = mapped.bones[slot];
            }
            pose->humanoid.manual = true;
            pose->loaded = true;
            Animation::refreshHumanoidMatched(pose->humanoid);
            ctx.markDirty();
        }
        ImGui::SameLine();
        if (ctx.skeletonBone >= 0 && ImGui::Button("Assign selected")) {
            int empty = -1;
            for (int slot = Animation::kHumanoidBodyCount; slot < static_cast<int>(Animation::HumanoidBone::Count);
                 ++slot) {
                if (pose->humanoid.bones[slot] < 0) {
                    empty = slot;
                    break;
                }
            }
            if (empty >= 0) {
                Animation::assignHumanoidBone(pose->humanoid, static_cast<Animation::HumanoidBone>(empty),
                                              ctx.skeletonBone);
                pose->loaded = true;
                ctx.markDirty();
            }
        }
        int fingerCount = 0;
        for (int slot = Animation::kHumanoidBodyCount; slot < static_cast<int>(Animation::HumanoidBone::Count); ++slot) {
            mapHumanoidSlot(slot);
            if (pose->humanoid.bones[slot] >= 0) ++fingerCount;
        }
        if (fingerCount == 0) {
            ImGui::TextDisabled("No fingers on this skeleton. Assign a joint to a slot, or re-export with finger bones.");
        } else {
            ImGui::TextDisabled("%d finger joints mapped. Slots can be set by hand.", fingerCount);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bones")) {
        const int count = static_cast<int>(std::max(skin->boneNames.size(), skin->skeleton.bones.size()));
        std::vector<std::vector<int>> children(static_cast<size_t>(count));
        std::vector<int> roots;
        for (int index = 0; index < count; ++index) {
            int parent = -1;
            if (index < static_cast<int>(skin->skeleton.bones.size())) {
                parent = skin->skeleton.bones[static_cast<size_t>(index)].parentIndex;
            }
            if (parent >= 0 && parent < count && parent != index) children[static_cast<size_t>(parent)].push_back(index);
            else roots.push_back(index);
        }
        const auto drawNode = [&](auto&& self, int index) -> void {
            if (Animation::poseOmitsBone(*pose, index)) {
                for (int child : children[static_cast<size_t>(index)]) self(self, child);
                return;
            }
            std::string label = (index < static_cast<int>(skin->boneNames.size()) &&
                                 !skin->boneNames[static_cast<size_t>(index)].empty())
                                    ? skin->boneNames[static_cast<size_t>(index)]
                                    : ("Bone " + std::to_string(index));
            if (Animation::isFingerBone(*pose, *skin, index)) {
                label += "  · finger";
            }
            label += "##bone";
            label += std::to_string(index);
            const bool hasChild = !children[static_cast<size_t>(index)].empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChild) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (ctx.skeletonEntityId == e.id() && ctx.skeletonBone == index) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                ctx.skeletonEntityId = e.id();
                ctx.skeletonBone = index;
                if (ctx.remapBones && ctx.remapHumanoidSlot >= 0 &&
                    ctx.remapHumanoidSlot < Animation::kHumanoidBodyCount) {
                    Animation::assignHumanoidBone(pose->humanoid,
                                                  static_cast<Animation::HumanoidBone>(ctx.remapHumanoidSlot),
                                                  index);
                    pose->loaded = true;
                    ctx.markDirty();
                    for (int slot = ctx.remapHumanoidSlot + 1; slot < Animation::kHumanoidBodyCount; ++slot) {
                        if (pose->humanoid.bones[slot] < 0) {
                            ctx.remapHumanoidSlot = slot;
                            break;
                        }
                    }
                }
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Edit")) {
                    ctx.skeletonEntityId = e.id();
                    ctx.skeletonBone = index;
                }
                if (ImGui::BeginMenu("Set as finger")) {
                    for (int slot = Animation::kHumanoidBodyCount;
                         slot < static_cast<int>(Animation::HumanoidBone::Count); ++slot) {
                        const auto kind = static_cast<Animation::HumanoidBone>(slot);
                        if (ImGui::MenuItem(Animation::humanoidBoneName(kind), nullptr,
                                            pose->humanoid.bones[slot] == index)) {
                            Animation::assignHumanoidBone(pose->humanoid, kind, index);
                            pose->loaded = true;
                            ctx.markDirty();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Not a finger")) {
                    for (int slot = Animation::kHumanoidBodyCount;
                         slot < static_cast<int>(Animation::HumanoidBone::Count); ++slot) {
                        if (pose->humanoid.bones[slot] == index) pose->humanoid.bones[slot] = -1;
                    }
                    pose->humanoid.manual = true;
                    ctx.markDirty();
                }
                if (ImGui::MenuItem("Reset joint")) {
                    if (pose->offsets.size() < skin->skeleton.boneCount()) pose->offsets.resize(skin->skeleton.boneCount());
                    pose->offsets[static_cast<size_t>(index)] = {};
                    ctx.markDirty();
                }
                if (ImGui::MenuItem("Delete joint")) {
                    if (!Animation::poseOmitsBone(*pose, index)) pose->removedBones.push_back(index);
                    if (pose->offsets.size() < skin->skeleton.boneCount()) pose->offsets.resize(skin->skeleton.boneCount());
                    pose->offsets[static_cast<size_t>(index)] = {};
                    pose->keys.erase(std::remove_if(pose->keys.begin(), pose->keys.end(),
                                                    [index](const Animation::BoneKeyframe& key) { return key.bone == index; }),
                                     pose->keys.end());
                    if (ctx.skeletonBone == index) ctx.skeletonBone = -1;
                    ctx.markDirty();
                }
                ImGui::EndPopup();
            }
            if (open && hasChild) {
                for (int child : children[static_cast<size_t>(index)]) self(self, child);
                ImGui::TreePop();
            }
        };
        for (int root : roots) drawNode(drawNode, root);
        if (!pose->removedBones.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Deleted joints");
            for (size_t removed = 0; removed < pose->removedBones.size();) {
                const i32 index = pose->removedBones[removed];
                const char* name = (index >= 0 && index < static_cast<int>(skin->boneNames.size()))
                                       ? skin->boneNames[static_cast<size_t>(index)].c_str()
                                       : "Bone";
                ImGui::PushID(index + 100000);
                ImGui::TextUnformatted(name);
                ImGui::SameLine();
                if (ImGui::SmallButton("Restore")) {
                    pose->removedBones.erase(pose->removedBones.begin() + static_cast<std::ptrdiff_t>(removed));
                    ctx.markDirty();
                    ImGui::PopID();
                    continue;
                }
                ImGui::PopID();
                ++removed;
            }
        }
        ImGui::TreePop();
    }
}

void InspectorPanel::drawAnimationPlayer(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Animation::AnimationPlayer>(e)) return;
    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Animation Player", enabled, removeRequested, "arrows-horizontal")) return;
    if (removeRequested) {
        world.remove<Animation::AnimationPlayer>(e);
        ctx.markDirty();
        return;
    }
    auto* player = world.get<Animation::AnimationPlayer>(e);
    if (!player) return;
    ImGui::PushID("anim_player");
    char path[260] = {};
    std::strncpy(path, player->clipPath, sizeof(path) - 1);
    if (ImGui::InputText("Clip", path, sizeof(path))) {
        Animation::setPlayerPath(*player, path);
        player->loaded = false;
        ctx.markDirty();
    }
    if (ImGui::DragFloat("Speed", &player->speed, 0.05f, 0.0f, 8.0f)) ctx.markDirty();
    if (ImGui::Checkbox("Playing", &player->playing)) ctx.markDirty();
    if (ImGui::Checkbox("Loop", &player->loop)) ctx.markDirty();
    ImGui::PopID();
}

void InspectorPanel::drawNavAgent(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Navigation::NavAgent>(e)) return;
    bool enabled = true;
    bool removeRequested = false;
    if (!Widgets::ComponentHeader("Nav Agent", enabled, removeRequested)) return;
    if (removeRequested) {
        world.remove<Navigation::NavAgent>(e);
        ctx.markDirty();
        return;
    }
    auto* agent = world.get<Navigation::NavAgent>(e);
    if (!agent) return;
    ImGui::PushID("nav_agent");
    int mode = static_cast<int>(agent->mode);
    const char* modes[] = {"Idle", "Patrol", "Follow", "Scripted"};
    if (ImGui::Combo("Behaviour", &mode, modes, 4)) {
        agent->mode = static_cast<Navigation::NavMode>(mode);
        agent->planDirty = true;
        ctx.markDirty();
    }
    if (ImGui::DragFloat("Speed", &agent->speed, 0.05f, 0.0f, 40.0f)) ctx.markDirty();
    if (ImGui::DragFloat3("Destination", &agent->destination.x, 0.1f)) {
        agent->hasDestination = true;
        agent->planDirty = true;
        ctx.markDirty();
    }
    ImGui::TextDisabled("Patrol points %u", agent->patrolCount);
    ImGui::PopID();
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
