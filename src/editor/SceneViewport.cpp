#include "editor/SceneViewport.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/EditorContext.hpp"
#include "editor/TestInstrumentation.hpp"
#include "editor/TestUIMapper.hpp"
#include "editor/TestRequestHandler.hpp"
#include "audio/AudioComponents.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/LightingSystem.hpp"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <stb/stb_image.h>
#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;
constexpr f32 kRadToDeg = 180.0f / 3.14159265f;

Mat4 buildLocalMatrix(const ECS::Transform& t) {
    return Mat4::translation(t.position)
         * Mat4::rotationZ(t.rotation.z * kDegToRad)
         * Mat4::rotationY(t.rotation.y * kDegToRad)
         * Mat4::rotationX(t.rotation.x * kDegToRad)
         * Mat4::scale(t.scale.x, t.scale.y, t.scale.z);
}

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w).normalized().toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) return buildLocalMatrix(*t);
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
        outPosition = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        outPosition = t->position;
        return true;
    }
    if (auto* p3 = world.get<ECS::Position3D>(entity)) {
        outPosition = p3->position;
        return true;
    }
    return false;
}

Vec3 matrixAxis(const Mat4& m, int column, const Vec3& fallback) {
    Vec3 axis(m(0, column), m(1, column), m(2, column));
    const f32 lenSq = axis.lengthSquared();
    return (lenSq > 0.000001f) ? axis / std::sqrt(lenSq) : fallback;
}

Vec3 entityAxis(ECS::World& world, ECS::Entity entity, int column, const Vec3& fallback) {
    return matrixAxis(entityMatrix(world, entity), column, fallback);
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    return -1.0f * entityAxis(world, entity, 2, Vec3(0.0f, 0.0f, -1.0f));
}

ImU32 lightColor(const ECS::LightComponent& lc, bool selected) {
    const f32 alphaScale = selected ? 1.0f : 0.85f;
    return IM_COL32(
        static_cast<ImU8>(std::clamp(lc.color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.z, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.w * lc.intensity * alphaScale, 0.0f, 1.0f) * 255.0f));
}

} // namespace

static std::vector<ECS::Entity> getSceneEntities(ECS::World& world) {
    std::vector<ECS::Entity> entities;
    ECS::ComponentQuery q;
    world.forEach(q, [&](ECS::Entity e) {
        entities.push_back(e);
    });
    return entities;
}

static void processTestCommand(const std::string& cmd, ECS::World& world, EditorContext& ctx) {
    if (cmd.find("select_entity ") == 0) {
        try {
            u32 id = std::stoul(cmd.substr(13));
            ECS::Entity entity(id, &world);
            ctx.selectEntity(entity);
            TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
        } catch (...) {}
    }
    else if (cmd.find("multi_select ") == 0) {
        try {
            u32 id = std::stoul(cmd.substr(12));
            ECS::Entity entity(id, &world);
            ctx.toggleSelection(entity);
            TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
        } catch (...) {}
    }
    else if (cmd == "delete_selected") {
        if (ctx.selectedEntity.isValid()) {
            world.destroy(ctx.selectedEntity);
            ctx.selectedEntity = ECS::Entity::INVALID;
            TestInstrumentation::onSceneEntities(getSceneEntities(world));
        }
    }
    else if (cmd == "focus_selected") {
        if (ctx.selectedEntity.isValid()) {
            Vec3 pos;
            if (tryGetEntityPosition(world, ctx.selectedEntity, pos)) {
                ctx.camFocus = pos;
                ctx.camDistance = 5.0f;
                TestInstrumentation::onCameraFocused(pos, 5.0f);
            }
        }
    }
    else if (cmd == "get_scene") {
        TestInstrumentation::onSceneEntities(getSceneEntities(world));
    }
}

// ── Init / Shutdown ───────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneViewport::init(RHI::RenderDevice* device, Config cfg) {
    m_device = device;
    m_config = cfg;

    RHI::TextureDesc colorDesc;
    colorDesc.width  = cfg.width;
    colorDesc.height = cfg.height;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage  = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width  = cfg.width;
    depthDesc.height = cfg.height;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage  = RHI::TextureUsage::DepthStencil;
    m_depthTarget = device->createTexture(depthDesc);

     m_initialized = (m_colorTarget != nullptr);
     if (m_initialized) {
         m_lastCanvasWidth = cfg.width;
         m_lastCanvasHeight = cfg.height;
     }
     return m_initialized;
}

void SceneViewport::resizeCanvasIfNeeded(u32 newWidth, u32 newHeight) {
    if (!m_device || newWidth < 1 || newHeight < 1) return;
    if (m_lastCanvasWidth == newWidth && m_lastCanvasHeight == newHeight) return;
    
    if (m_colorTarget) m_device->destroyTexture(m_colorTarget);
    if (m_depthTarget) m_device->destroyTexture(m_depthTarget);
    
    RHI::TextureDesc colorDesc;
    colorDesc.width  = newWidth;
    colorDesc.height = newHeight;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage  = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = m_device->createTexture(colorDesc);
    
    RHI::TextureDesc depthDesc;
    depthDesc.width  = newWidth;
    depthDesc.height = newHeight;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage  = RHI::TextureUsage::DepthStencil;
    m_depthTarget = m_device->createTexture(depthDesc);
    
    m_lastCanvasWidth = newWidth;
    m_lastCanvasHeight = newHeight;
}

void SceneViewport::shutdown() {
     releaseSpriteTextures();
     if (!m_initialized || !m_device) return;
     m_device->destroyTexture(m_colorTarget);
     m_device->destroyTexture(m_depthTarget);
     m_colorTarget = nullptr;
     m_depthTarget = nullptr;
     m_initialized = false;
 }
#endif

// ── Main render entry point ───────────────────────────────────────

void SceneViewport::render(ECS::World& world, EditorContext& ctx) {
    // Make stdin non-blocking on first call in test mode
    static bool stdinConfigured = false;
    if (TestInstrumentation::isTestMode() && !stdinConfigured) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        stdinConfigured = true;
    }
    
    if (TestInstrumentation::isTestMode()) {
        static std::string buffer;
        int ch;
        while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
            buffer += static_cast<char>(ch);
        }
        if (ch == '\n' && !buffer.empty()) {
            TestRequestHandler::Request req;
            if (TestRequestHandler::tryParseRequest(buffer, req)) {
                ImVec2 viewportPos = ImGui::GetCursorScreenPos();
                ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                
                auto resp = TestRequestHandler::handleRequest(
                    req, world, ctx,
                    viewportPos.x, viewportPos.y,
                    viewportSize.x, viewportSize.y
                );
                
                std::cout << "REQUEST_RESPONSE: " << resp.toJson() << std::endl;
            }
            buffer.clear();
        }
    }
    
    if (!m_open) return;

    if (!ImGui::Begin("Scene Viewport", &m_open,
            (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
             ctx.viewMode == EditorContext::ViewMode::Isometric)
                ? ImGuiWindowFlags_NoNavInputs
                : ImGuiWindowFlags_None)) {
        ImGui::End();
        return;
    }

#ifdef CF_HAS_SDL3
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) {
        ImGui::End();
        return;
    }
    
    resizeCanvasIfNeeded((u32)viewportSize.x, (u32)viewportSize.y);

    if (m_initialized && m_colorTarget) {
        ImGui::Image((ImTextureID)(intptr_t)m_colorTarget->handle, viewportSize);
    } else {
        ImGui::Dummy(viewportSize);
    }
#else
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) {
        ImGui::End();
        return;
    }
    ImGui::Dummy(viewportSize);
#endif

    // ── Drag-drop target for assets ──
    if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
        ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);

        ECS::Entity entity = world.create();
        std::filesystem::path assetPath(asset->path);
        setEntityName(world, entity, assetPath.stem().string().c_str());

        // Convert mouse position to world coordinates
        ImVec2 mousePos  = ImGui::GetMousePos();
        ImVec2 viewportTL = ImGui::GetItemRectMin();
        f32 localX = (mousePos.x - viewportTL.x) - viewportSize.x * 0.5f;
        f32 localY = (mousePos.y - viewportTL.y) - viewportSize.y * 0.5f;
        f32 scale  = ctx.viewportZoom * 50.0f;
        f32 worldX = (localX - ctx.viewportPanX) / scale;
        f32 worldY = (localY - ctx.viewportPanY) / scale;
        auto t = ECS::Transform{};
        t.position.x = worldX;
        t.position.y = -worldY;
        world.add<ECS::Transform>(entity, t);

        if (asset->type == AssetType::Texture) {
            world.add<ECS::Sprite>(entity, asset->path, 0);
        }

        if (asset->type == AssetType::Audio) {
            auto& emitter = world.add<Audio::AudioEmitter>(entity);
            emitter.clipPath = assetPath.filename().string().c_str();
        }

        if (asset->type == AssetType::Mesh) {
            world.add<ECS::MeshFilterComponent>(entity, 
                ECS::MeshPrimitive::Custom, assetPath.string());
            world.add<ECS::MeshRendererComponent>(entity, 
                assetPath.string(), "");
        }

        ctx.selectedEntity = entity;
        ctx.endUndo(world);
    }

    bool hovered = ImGui::IsItemHovered();

    // Handle entity selection via raycasting (only in 3D mode, not during gizmo drag)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_gizmoDragging && 
        ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 vpMin = ImGui::GetItemRectMin();
        ImVec2 vpMax = ImGui::GetItemRectMax();
        
        bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                                mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);
        
        if (mouseInViewport) {
            ImVec2 vpSize = ImGui::GetContentRegionAvail();
            Vec2 screenClick(mousePos.x - vpMin.x, mousePos.y - vpMin.y);
            
            f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
            Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
            f32 aspect = vpSize.x / std::max(vpSize.y, 1.0f);
            Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, 10000.0f);
            Mat4 vp = proj * view;
            Mat4 vpInverse = vp.inverted();
            
            f32 ndcX = (2.0f * screenClick.x) / vpSize.x - 1.0f;
            f32 ndcY = 1.0f - (2.0f * screenClick.y) / vpSize.y;
            Vec4 ndcNear(ndcX, ndcY, -1.0f, 1.0f);
            Vec4 worldNear = vpInverse.transformVec4(ndcNear);
            
            if (std::abs(worldNear.w) > 0.0001f) {
                worldNear.x /= worldNear.w;
                worldNear.y /= worldNear.w;
                worldNear.z /= worldNear.w;
            }
            
            Vec3 rayOrigin = camPos;
            Vec3 rayDirection = (Vec3(worldNear.x, worldNear.y, worldNear.z) - camPos).normalized();
            
            ECS::Entity selectedEntity = raycastSelectEntity(rayOrigin, rayDirection, world);
            
            bool shiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
            
            if (selectedEntity.isValid()) {
                if (shiftPressed) {
                    ctx.toggleSelection(selectedEntity);
                } else {
                    ctx.selectEntity(selectedEntity);
                }
                TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
            } else {
                if (!shiftPressed) {
                    ctx.clearSelection();
                }
            }
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_T)) ctx.gizmoMode = EditorContext::GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) ctx.gizmoMode = EditorContext::GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) ctx.gizmoMode = EditorContext::GizmoMode::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) ctx.gizmoMode = EditorContext::GizmoMode::None;
        
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ctx.selectedEntity.isValid()) {
            ctx.beginUndo(EditorCommand::RemoveEntity, ctx.selectedEntity.id(), world);
            world.destroy(ctx.selectedEntity);
            ctx.selectedEntity = ECS::Entity::INVALID;
            ctx.endUndo(world);
            TestInstrumentation::onSceneEntities(getSceneEntities(world));
        }
    }

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ctx.selectedEntity.isValid() && 
        ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        Vec3 entityPos;
        if (tryGetEntityPosition(world, ctx.selectedEntity, entityPos)) {
            ctx.camFocus = entityPos;
            ctx.camDistance = 5.0f;
            TestInstrumentation::onCameraFocused(entityPos, 5.0f);
        }
    }

    bool leftDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if ((hovered || m_gizmoDragging) && ctx.selectedEntity.isValid() && ctx.gizmoMode != EditorContext::GizmoMode::None) {
        if (leftDragging && !m_gizmoDragging) {
            ctx.beginUndo(EditorCommand::SetField, ctx.selectedEntity.id(), world);
            m_gizmoDragging = true;
        }
        if (leftDragging) {
            handleGizmoInput(world, ctx, viewportSize);
        }
    }
    if (!leftDown && m_gizmoDragging) {
        ctx.endUndo(world);
        m_gizmoDragging = false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetItemRectMin();

    if (m_config.grid) {
        char modeStr[16];
        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate: strcpy(modeStr, "Translate"); break;
            case EditorContext::GizmoMode::Rotate:    strcpy(modeStr, "Rotate"); break;
            case EditorContext::GizmoMode::Scale:     strcpy(modeStr, "Scale"); break;
            default: strcpy(modeStr, "Select"); break;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "Gizmo: %s [T/E/R]  Grid: %s", modeStr, m_config.grid ? "ON" : "OFF");
        drawList->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(200, 200, 200, 200), buf);
    }

    {
        ImVec2 btnPos(origin.x + 8, origin.y + 28);
        ImGui::SetCursorScreenPos(btnPos);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        if (ctx.physicsDebugVisible) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.85f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button("Physics")) {
            ctx.physicsDebugVisible = !ctx.physicsDebugVisible;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle physics collider debug overlay");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ctx.snapToGrid) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 0.85f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button("Snap")) {
            ctx.snapToGrid = !ctx.snapToGrid;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle snap to grid (%.1f units)", ctx.snapGridSize);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        const bool texturedPreview = (m_meshPreviewMode == MeshPreviewMode::Textured);
        if (texturedPreview) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.75f, 0.78f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button(texturedPreview ? "Textured" : "Wireframe")) {
            m_meshPreviewMode = texturedPreview ? MeshPreviewMode::Wireframe : MeshPreviewMode::Textured;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 3D preview style (white/gray textured vs wireframe)");
        if (texturedPreview) {
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        const char* densityLabels[] = {"Low", "Medium", "High"};
        int wireDensity = static_cast<int>(m_wireframeDensity);
        ImGui::BeginDisabled(texturedPreview);
        ImGui::SetNextItemWidth(88.0f);
        if (ImGui::SliderInt("##wire_density", &wireDensity, 0, 2, densityLabels[wireDensity])) {
            m_wireframeDensity = static_cast<WireframeDensity>(wireDensity);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wireframe polygon density");
        ImGui::EndDisabled();

        ImGui::PopStyleVar();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        f32 btnW   = 32.0f;
        f32 margin = 8.0f;
        ImVec2 btnPos(origin.x + viewportSize.x - margin - btnW * 3.0f - 4.0f, origin.y + 8.0f);

        auto viewBtn = [&](const char* label, EditorContext::ViewMode mode) {
            bool active = (ctx.viewMode == mode);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.9f));
            else        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.75f));
            ImGui::SetCursorScreenPos(btnPos);
            if (ImGui::Button(label, ImVec2(btnW, 22.0f))) ctx.viewMode = mode;
            ImGui::PopStyleColor();
            btnPos.x += btnW + 2.0f;
        };

        viewBtn("2D",  EditorContext::ViewMode::Mode2D);
        viewBtn("3D",  EditorContext::ViewMode::Mode3D);
        viewBtn("Iso", EditorContext::ViewMode::Isometric);
        ImGui::PopStyleVar();
    }

    drawGrid(drawList, origin, viewportSize, ctx);
    drawSprites(world, ctx, origin, viewportSize);
    drawEmptyEntities(world, ctx, origin, viewportSize);
    drawPhysicsDebug(world, ctx, origin, viewportSize);
    drawCameraFrustums(world, ctx, origin, viewportSize);
    drawLightGizmos(world, ctx, origin, viewportSize);

    if (ctx.selectedEntity.isValid()) {
        drawGizmo(world, ctx, origin, viewportSize);
    }

    drawNavigationWidget(world, ctx, origin, viewportSize);

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        ctx.viewportPanX += delta.x;
         ctx.viewportPanY += delta.y;
         ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
     }

     // 2D View: Left mouse button drag to pan
     if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
         if (ctx.viewMode == EditorContext::ViewMode::Mode2D) {
             ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
             f32 s = ctx.viewportZoom * 50.0f;
             ctx.viewportPanX -= delta.x / s;
             ctx.viewportPanY -= delta.y / s;
             ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
         }
     }

      if (hovered && ImGui::IsWindowFocused()) {
          bool is3DIso = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                          ctx.viewMode == EditorContext::ViewMode::Isometric);
          if (is3DIso) {
             float speed = ctx.camDistance * 0.04f / ctx.viewportZoom;
             float sinY  = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
             float sinP  = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
             
             // WASD movement with full 3D orientation (yaw + pitch)
             if (ImGui::IsKeyDown(ImGuiKey_W)) {
                 // Forward: move in camera forward direction
                 ctx.camFocus.x += -sinY * cosP * speed;
                 ctx.camFocus.y +=  sinP * speed;
                 ctx.camFocus.z +=  cosY * cosP * speed;
             }
             if (ImGui::IsKeyDown(ImGuiKey_S)) {
                 // Backward: opposite of forward
                 ctx.camFocus.x -= -sinY * cosP * speed;
                 ctx.camFocus.y -=  sinP * speed;
                 ctx.camFocus.z -=  cosY * cosP * speed;
             }
             if (ImGui::IsKeyDown(ImGuiKey_A)) {
                 // Left: strafe perpendicular to forward
                 ctx.camFocus.x -= cosY * speed;
                 ctx.camFocus.z -= sinY * speed;
             }
             if (ImGui::IsKeyDown(ImGuiKey_D)) {
                 // Right: opposite of left
                 ctx.camFocus.x += cosY * speed;
                 ctx.camFocus.z += sinY * speed;
             }
         }
     }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
            ctx.camYaw   += delta.x * 0.005f;
            ctx.camPitch += delta.y * 0.005f;
            ctx.camPitch  = std::max(-1.5f, std::min(1.5f, ctx.camPitch));
        } else if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
            ctx.camYaw += delta.x * 0.005f;
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    }

     if (hovered) {
         f32 scroll = ImGui::GetIO().MouseWheel;
         if (scroll != 0) {
             if (ctx.viewMode == EditorContext::ViewMode::Mode3D || 
                 ctx.viewMode == EditorContext::ViewMode::Isometric) {
                 ctx.camDistance *= (scroll > 0) ? 0.8f : 1.25f;
                 ctx.camDistance = std::max(0.5f, std::min(1000.0f, ctx.camDistance));
             } else {
                 ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
                 ctx.viewportZoom = std::max(0.1f, std::min(10.0f, ctx.viewportZoom));
             }
         }
     }

    ImGui::End();
}

ImVec2 SceneViewport::projectToScreen(Vec3 p, ImVec2 origin, ImVec2 viewportSize,
                                       const EditorContext& ctx) {
    f32 cx = origin.x + viewportSize.x * 0.5f;
    f32 cy = origin.y + viewportSize.y * 0.5f;

    switch (ctx.viewMode) {
        case EditorContext::ViewMode::Mode2D: {
            f32 s = ctx.viewportZoom * 50.0f;
            return ImVec2(cx + (p.x + ctx.viewportPanX / s) * s,
                          cy + (-p.y + ctx.viewportPanY / s) * s);
        }
        case EditorContext::ViewMode::Mode3D: {
            f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);

            Vec3 camPos;
            camPos.x = ctx.camFocus.x + sinY * cosP * ctx.camDistance;
            camPos.y = ctx.camFocus.y - sinP * ctx.camDistance;
            camPos.z = ctx.camFocus.z - cosY * cosP * ctx.camDistance;

            Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));

            f32 aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);
            f32 fov = 1.0472f; // 60 degrees
            f32 zNear = 0.1f;
            f32 zFar = 10000.0f;
            Mat4 proj = Mat4::perspective(fov, aspect, zNear, zFar);

            Mat4 vp = proj * view;
            Vec4 clip = vp.transformVec4(Vec4(p.x, p.y, p.z, 1.0f));

            if (clip.w <= 0.001f) {
                return ImVec2(-10000.0f, -10000.0f);
            }

            f32 ndcX = clip.x / clip.w;
            f32 ndcY = clip.y / clip.w;

            f32 screenX = origin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x;
            f32 screenY = origin.y + (1.0f - ndcY) * 0.5f * viewportSize.y;
            return ImVec2(screenX, screenY);
        }
        case EditorContext::ViewMode::Isometric: {
            f32 s = ctx.viewportZoom * 50.0f;
            f32 cosA = std::cos(ctx.camYaw + 0.5236f);
            f32 sinA = std::sin(ctx.camYaw + 0.5236f);
            f32 iso_x = (p.x - p.y) * cosA * s;
            f32 iso_y = (p.x + p.y) * sinA * s * 0.5f - p.z * s * 0.866f;
            return ImVec2(cx + iso_x + ctx.viewportPanX,
                          cy - iso_y + ctx.viewportPanY);
        }
    }
    return ImVec2(cx, cy);
}

// ── Gizmo drawing ─────────────────────────────────────────────────

void SceneViewport::drawSprites(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ECS::ComponentQuery query;
    query.with<ECS::Transform>();
    query.with<ECS::Sprite>();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;
    const f32 minHalfSize = 8.0f;

    world.forEach<ECS::Transform, ECS::Sprite>(query, [&](ECS::Entity entity, ECS::Transform& pos, ECS::Sprite& sprite) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;

        Vec3 worldPosition = pos.position;
        f32 scaleX = std::max(0.1f, pos.scale.x);
        f32 scaleY = std::max(0.1f, pos.scale.y);
        f32 angle  = pos.rotation.z;

        if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
            worldPosition = Vec3(wt->matrix(0,3), wt->matrix(1,3), wt->matrix(2,3));
            // scaleX/Y = length of matrix column 0/1 (upper 3x3)
            scaleX = std::max(0.1f, sqrtf(wt->matrix(0,0)*wt->matrix(0,0) + wt->matrix(1,0)*wt->matrix(1,0)));
            scaleY = std::max(0.1f, sqrtf(wt->matrix(0,1)*wt->matrix(0,1) + wt->matrix(1,1)*wt->matrix(1,1)));
            angle  = atan2f(wt->matrix(1,0), wt->matrix(0,0));
        }

        ImVec2 screenPos = projectToScreen(worldPosition, origin, viewportSize, ctx);

        f32 halfW = std::max(minHalfSize, 0.5f * worldToScreen * scaleX);
        f32 halfH = std::max(minHalfSize, 0.5f * worldToScreen * scaleY);

        ImTextureRef texRef;
        bool hasTexture = false;
        if (!sprite.name.empty()) {
            const std::string spritePath = resolveSpritePath(sprite.name, ctx);
            if (!spritePath.empty()) {
                auto it = m_spriteTextureCache.find(spritePath);
                if (it == m_spriteTextureCache.end()) {
                    // Try to load the texture
                    int width = 0, height = 0, channels = 0;
                    unsigned char* pixels = stbi_load(spritePath.c_str(), &width, &height, &channels, 4);
                    
                    SpriteTextureCacheEntry entry;
                    if (pixels && width > 0 && height > 0) {
                        entry.width = width;
                        entry.height = height;
                        entry.texture = std::make_unique<ImTextureData>();
                        entry.texture->Create(ImTextureFormat_RGBA32, width, height);
                        std::memcpy(entry.texture->GetPixels(), pixels, static_cast<size_t>(width * height * 4));
                        entry.texture->SetStatus(ImTextureStatus_WantCreate);
                        ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
                        entry.loadFailed = false;
                    } else {
                        entry.loadFailed = true;
                    }
                    
                    if (pixels) {
                        stbi_image_free(pixels);
                    }
                    
                    auto [newIt, inserted] = m_spriteTextureCache.emplace(spritePath, std::move(entry));
                    it = newIt;
                }

                if (!it->second.loadFailed && it->second.texture) {
                    // Ensure texture is properly initialized
                    if (it->second.texture->Status == ImTextureStatus_WantCreate) {
                        ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
                    }
                    
                    ImTextureID texID = it->second.texture->GetTexID();
                    hasTexture = (texID != ImTextureID_Invalid);
                    
                    if (hasTexture) {
                        texRef = it->second.texture->GetTexRef();
                        if (it->second.width > 0 && it->second.height > 0) {
                            const f32 aspect = static_cast<f32>(it->second.width) / static_cast<f32>(it->second.height);
                            if (aspect > 1.0f) {
                                halfH = std::max(minHalfSize, halfW / aspect);
                            } else {
                                halfW = std::max(minHalfSize, halfH * aspect);
                            }
                        }
                    }
                }
            }
        }

        const f32 c = std::cos(angle);
        const f32 s = std::sin(angle);
        auto rotatePoint = [&](f32 x, f32 y) -> ImVec2 {
            return ImVec2(screenPos.x + x * c - y * s, screenPos.y + x * s + y * c);
        };

        const ImVec2 p1 = rotatePoint(-halfW, -halfH);
        const ImVec2 p2 = rotatePoint(halfW, -halfH);
        const ImVec2 p3 = rotatePoint(halfW, halfH);
        const ImVec2 p4 = rotatePoint(-halfW, halfH);

        const bool selected = (ctx.selectedEntity == entity);
        const ImU32 fill = selected ? IM_COL32(100, 170, 255, 80) : IM_COL32(180, 180, 200, 45);
        const ImU32 border = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(190, 190, 220, 200);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hasTexture) {
            dl->AddImageQuad(texRef, p1, p2, p3, p4);
        } else {
            // Draw checkerboard pattern for missing texture
            dl->AddQuadFilled(p1, p2, p3, p4, IM_COL32(64, 64, 64, 200));
            
            // Draw checkerboard pattern
            ImVec2 checkSize = ImVec2((p2.x - p1.x) / 4.0f, (p4.y - p1.y) / 4.0f);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if ((x + y) % 2 == 0) {
                        ImVec2 checkMin = ImVec2(p1.x + x * checkSize.x, p1.y + y * checkSize.y);
                        ImVec2 checkMax = ImVec2(checkMin.x + checkSize.x, checkMin.y + checkSize.y);
                        dl->AddRectFilled(checkMin, checkMax, IM_COL32(96, 96, 96, 200));
                    }
                }
            }
        }
        dl->AddQuad(p1, p2, p3, p4, border, selected ? 2.0f : 1.0f);

        if (!sprite.name.empty()) {
            std::filesystem::path labelPath(sprite.name);
            const std::string label = labelPath.filename().string();
            dl->AddText(ImVec2(screenPos.x - halfW, screenPos.y - halfH - 14.0f), IM_COL32(220, 220, 230, 230), label.c_str());
        }
    });
}

void SceneViewport::drawEmptyEntities(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 7.0f;

    struct DirLightEval { Vec3 dir; f32 intensity; Vec3 color; };
    struct PointLightEval { Vec3 pos; f32 intensity; f32 radius; Vec3 color; };
    struct SpotLightEval { Vec3 pos; Vec3 dir; f32 intensity; f32 radius; f32 cosHalfAngle; Vec3 color; };
    std::vector<DirLightEval> dirLights;
    std::vector<PointLightEval> pointLights;
    std::vector<SpotLightEval> spotLights;

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::DirectionalLightComponent&) {
                if (Scene::isEffectivelyDisabled(world, e)) return;
                Vec3 ldir = entityForward(world, e).normalized();
                dirLights.push_back({ldir, lc.intensity, Vec3(lc.color.x, lc.color.y, lc.color.z)});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();
        world.forEach<ECS::LightComponent, ECS::PointLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::PointLightComponent& pl) {
                if (Scene::isEffectivelyDisabled(world, e)) return;
                Vec3 p;
                if (!tryGetEntityPosition(world, e, p)) return;
                pointLights.push_back({p, lc.intensity, std::max(0.001f, pl.radius), Vec3(lc.color.x, lc.color.y, lc.color.z)});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::SpotLightComponent>();
        world.forEach<ECS::LightComponent, ECS::SpotLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::SpotLightComponent& sl) {
                if (Scene::isEffectivelyDisabled(world, e)) return;
                Vec3 p;
                if (!tryGetEntityPosition(world, e, p)) return;
                Vec3 d = entityForward(world, e).normalized();
                const f32 halfAngle = std::clamp(sl.angle * kDegToRad * 0.5f, 0.01f, 1.5533f);
                spotLights.push_back({p, d, lc.intensity, std::max(0.001f, sl.radius), std::cos(halfAngle), Vec3(lc.color.x, lc.color.y, lc.color.z)});
            });
    }

    auto lightColorAt = [&](const Vec3& p, const Vec3& n) -> Vec3 {
        const Vec3 nn = n.normalized();
        Vec3 diffuse(0.18f, 0.18f, 0.18f);

        for (const auto& l : dirLights) {
            const f32 ndotl = std::max(0.0f, nn.dot(-1.0f * l.dir));
            const f32 gain = ndotl * l.intensity * 0.65f;
            diffuse += l.color * gain;
        }
        for (const auto& l : pointLights) {
            Vec3 toLight = l.pos - p;
            f32 dist = std::max(0.001f, toLight.length());
            if (dist > l.radius) continue;
            Vec3 ldir = toLight / dist;
            f32 atten = 1.0f - (dist / l.radius);
            atten *= atten;
            const f32 ndotl = std::max(0.0f, nn.dot(ldir));
            diffuse += l.color * (ndotl * l.intensity * atten * 0.9f);
        }
        for (const auto& l : spotLights) {
            Vec3 toPoint = p - l.pos;
            f32 dist = std::max(0.001f, toPoint.length());
            if (dist > l.radius) continue;
            Vec3 fromLight = toPoint / dist;
            f32 cone = l.dir.dot(fromLight);
            if (cone < l.cosHalfAngle) continue;
            f32 spotAtten = (cone - l.cosHalfAngle) / std::max(0.0001f, 1.0f - l.cosHalfAngle);
            f32 rangeAtten = 1.0f - (dist / l.radius);
            rangeAtten *= rangeAtten;
            const f32 ndotl = std::max(0.0f, nn.dot(-1.0f * fromLight));
            diffuse += l.color * (ndotl * l.intensity * spotAtten * rangeAtten);
        }

        diffuse.x = std::clamp(diffuse.x, 0.06f, 1.65f);
        diffuse.y = std::clamp(diffuse.y, 0.06f, 1.65f);
        diffuse.z = std::clamp(diffuse.z, 0.06f, 1.65f);
        return diffuse;
    };

    auto toColor = [](const Vec3& v, f32 a = 0.94f) -> ImU32 {
        const f32 r = std::clamp(v.x, 0.0f, 1.0f);
        const f32 g = std::clamp(v.y, 0.0f, 1.0f);
        const f32 b = std::clamp(v.z, 0.0f, 1.0f);
        const ImU8 cr = static_cast<ImU8>(r * 255.0f);
        const ImU8 cg = static_cast<ImU8>(g * 255.0f);
        const ImU8 cb = static_cast<ImU8>(b * 255.0f);
        const ImU8 alpha = static_cast<ImU8>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        return IM_COL32(cr, cg, cb, alpha);
    };

    auto drawMarker = [&](ECS::Entity entity, const Vec3& worldPosition) {
        ImVec2 sp = projectToScreen(worldPosition, origin, viewportSize, ctx);

        const bool selected = (ctx.selectedEntity == entity);
        const ImU32 col     = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(180, 180, 200, 200);

        dl->AddQuadFilled(
            ImVec2(sp.x,     sp.y - r),
            ImVec2(sp.x + r, sp.y    ),
            ImVec2(sp.x,     sp.y + r),
            ImVec2(sp.x - r, sp.y    ),
            selected ? IM_COL32(110, 210, 255, 40) : IM_COL32(180, 180, 200, 30));
        dl->AddQuad(
            ImVec2(sp.x,     sp.y - r),
            ImVec2(sp.x + r, sp.y    ),
            ImVec2(sp.x,     sp.y + r),
            ImVec2(sp.x - r, sp.y    ),
            col, selected ? 2.0f : 1.0f);
        dl->AddLine(ImVec2(sp.x - r * 0.5f, sp.y), ImVec2(sp.x + r * 0.5f, sp.y), col, 1.5f);
        dl->AddLine(ImVec2(sp.x, sp.y - r * 0.5f), ImVec2(sp.x, sp.y + r * 0.5f), col, 1.5f);
    };

    auto drawSegment = [&](const Vec3& a, const Vec3& b, ImU32 col, float thickness) {
        dl->AddLine(projectToScreen(a, origin, viewportSize, ctx),
                    projectToScreen(b, origin, viewportSize, ctx),
                    col, thickness);
    };

    auto drawRing = [&](const Mat4& worldMatrix, const Vec3& axisA, const Vec3& axisB,
                        f32 radius, int segments, ImU32 col, float thickness) {
        Vec3 prev = worldMatrix.transformPoint(axisA * radius);
        for (int i = 1; i <= segments; ++i) {
            const f32 a = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
            const Vec3 local = axisA * (std::cos(a) * radius) + axisB * (std::sin(a) * radius);
            const Vec3 curr  = worldMatrix.transformPoint(local);
            drawSegment(prev, curr, col, thickness);
            prev = curr;
        }
    };

    auto drawMeshPreview = [&](ECS::Entity entity) {
        auto* mesh = world.get<ECS::MeshFilterComponent>(entity);
        if (!mesh) return false;

        const Mat4 worldMatrix = entityMatrix(world, entity);
        const bool selected = (ctx.selectedEntity == entity);
        const bool wireMode = (m_meshPreviewMode == MeshPreviewMode::Wireframe);
        const ImU32 wireCol = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(170, 190, 230, 210);
        const ImU32 polyCol = selected ? IM_COL32(95, 170, 255, 210) : IM_COL32(130, 150, 190, 170);
        const float thickness = selected ? 2.0f : 1.25f;
        const float polyThickness = selected ? 1.5f : 1.0f;

        if (wireMode && !selected) {
            return true;
        }

        const bool drawContour = wireMode || selected;
        const bool drawPolygons = wireMode;
        const int densityLevel = static_cast<int>(m_wireframeDensity);
        const int spherePolySegments = (densityLevel == 0) ? 8 : (densityLevel == 1 ? 12 : 18);
        const int sidePolySlices = (densityLevel == 0) ? 4 : (densityLevel == 1 ? 8 : 14);

        auto cameraDepth = [&](const Vec3& wp) -> f32 {
            if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
                f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                f32 rx = wp.x - ctx.camFocus.x;
                f32 ry = wp.y - ctx.camFocus.y;
                f32 rz = wp.z - ctx.camFocus.z;
                f32 vz = -sinY * rx + cosY * rz;
                f32 vz2 = -sinP * ry + cosP * vz;
                return vz2;
            }
            if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
                return wp.x + wp.y + wp.z;
            }
            return wp.z;
        };

        auto drawEdge = [&](const Vec3& a, const Vec3& b) {
            if (!drawContour) return;
            drawSegment(a, b, wireCol, thickness);
        };
        auto drawPoly = [&](const Vec3& a, const Vec3& b) {
            if (!drawPolygons) return;
            drawSegment(a, b, polyCol, polyThickness);
        };

        switch (mesh->primitive) {
            case ECS::MeshPrimitive::Cube: {
                const std::array<Vec3, 8> corners = {{
                    {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
                    { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
                    {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
                    { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
                }};
                const int edges[][2] = {
                    {0,1}, {1,2}, {2,3}, {3,0},
                    {4,5}, {5,6}, {6,7}, {7,4},
                    {0,4}, {1,5}, {2,6}, {3,7}
                };

                const int faces[][4] = {
                    {0,1,2,3}, // back
                    {4,5,6,7}, // front
                    {0,3,7,4}, // left
                    {1,2,6,5}, // right
                    {3,2,6,7}, // top
                    {0,1,5,4}, // bottom
                };

                const Vec3 faceNormalsLocal[] = {
                    {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}
                };

                std::array<Vec3, 8> wp;
                for (usize i = 0; i < corners.size(); ++i) {
                    wp[i] = worldMatrix.transformPoint(corners[i]);
                }

                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    struct FaceDraw {
                        int faceIndex;
                        f32 depth;
                    };
                    std::array<FaceDraw, 6> faceOrder{};
                    for (int fi = 0; fi < 6; ++fi) {
                        Vec3 center = (wp[faces[fi][0]] + wp[faces[fi][1]] + wp[faces[fi][2]] + wp[faces[fi][3]]) * 0.25f;
                        faceOrder[fi] = {fi, cameraDepth(center)};
                    }
                    std::sort(faceOrder.begin(), faceOrder.end(), [](const FaceDraw& a, const FaceDraw& b) {
                        return a.depth > b.depth;
                    });

                    for (const auto& fd : faceOrder) {
                        const int fi = fd.faceIndex;
                        Vec3 nWorld = matrixAxis(worldMatrix, (fi == 0 || fi == 1) ? 2 : (fi <= 3 ? 0 : 1), faceNormalsLocal[fi]);
                        if (fi == 0 || fi == 2 || fi == 5) nWorld = -1.0f * nWorld;

                        Vec3 center = (wp[faces[fi][0]] + wp[faces[fi][1]] + wp[faces[fi][2]] + wp[faces[fi][3]]) * 0.25f;
                        const Vec3 lit = lightColorAt(center, nWorld);
                        const f32 checker = (fi % 2 == 0) ? 0.86f : 0.74f;
                        const ImU32 fill = toColor(Vec3(lit.x * checker, lit.y * checker, lit.z * checker), 0.92f);

                        ImVec2 p0 = projectToScreen(wp[faces[fi][0]], origin, viewportSize, ctx);
                        ImVec2 p1 = projectToScreen(wp[faces[fi][1]], origin, viewportSize, ctx);
                        ImVec2 p2 = projectToScreen(wp[faces[fi][2]], origin, viewportSize, ctx);
                        ImVec2 p3 = projectToScreen(wp[faces[fi][3]], origin, viewportSize, ctx);
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                    }
                }

                for (const auto& edge : edges) {
                    drawEdge(wp[edge[0]], wp[edge[1]]);
                }
                if (drawPolygons) {
                    for (int fi = 0; fi < 6; ++fi) {
                        const auto& face = faces[fi];
                        if (densityLevel == 0) {
                            if ((fi % 2) == 0) drawPoly(wp[face[0]], wp[face[2]]);
                        } else if (densityLevel == 1) {
                            drawPoly(wp[face[0]], wp[face[2]]);
                        } else {
                            drawPoly(wp[face[0]], wp[face[2]]);
                            drawPoly(wp[face[1]], wp[face[3]]);
                        }
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Custom: {
                if (!mesh->customMeshPath.empty()) {
                    std::string meshPath = mesh->customMeshPath;
                    auto* loadedMesh = Assets::MeshCache::getInstance().getMesh(meshPath);
                    
                    if (!loadedMesh) {
                        meshPath = std::string("assets/raw/") + mesh->customMeshPath;
                        loadedMesh = Assets::MeshCache::getInstance().getMesh(meshPath);
                    }
                    
                    if (loadedMesh && !loadedMesh->vertices.empty() && !loadedMesh->indices.empty()) {
                        if (loadedMesh->baseColorTexture.empty() && loadedMesh->textureWidth == 0) {
                            std::string texturePath;
                            if (!mesh->customTexturePath.empty()) {
                                texturePath = mesh->customTexturePath;
                                if (texturePath.find("assets/raw") == std::string::npos) {
                                    texturePath = std::string("assets/raw/") + mesh->customTexturePath;
                                }
                            } else {
                                texturePath = meshPath;
                                size_t dotPos = texturePath.find_last_of('.');
                                if (dotPos != std::string::npos) {
                                    texturePath = texturePath.substr(0, dotPos) + ".png";
                                }
                            }
                            if (!texturePath.empty()) {
                                Assets::MeshLoader::loadPNGTexture(loadedMesh, texturePath.c_str());
                            }
                        }
                        
                        f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                        f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                        Vec3 camPos;
                        camPos.x = ctx.camFocus.x + sinY * cosP * ctx.camDistance;
                        camPos.y = ctx.camFocus.y - sinP * ctx.camDistance;
                        camPos.z = ctx.camFocus.z - cosY * cosP * ctx.camDistance;
                        Mat4 viewMat = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
                        f32 aspectR = viewportSize.x / std::max(viewportSize.y, 1.0f);
                        Mat4 projMat = Mat4::perspective(1.0472f, aspectR, 0.1f, 10000.0f);
                        Mat4 vpMat = projMat * viewMat;

                        Vec3 bMin = loadedMesh->bounds.min;
                        Vec3 bMax = loadedMesh->bounds.max;
                        Vec3 bbCorners[8] = {
                            {bMin.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMin.z},
                            {bMin.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMin.z},
                            {bMin.x, bMin.y, bMax.z}, {bMax.x, bMin.y, bMax.z},
                            {bMin.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMax.z}
                        };

                        bool anyVisible = false;
                        for (int ci = 0; ci < 8; ++ci) {
                            Vec3 wp = worldMatrix.transformPoint(bbCorners[ci]);
                            Vec4 clip = vpMat.transformVec4(Vec4(wp.x, wp.y, wp.z, 1.0f));
                            if (clip.w > 0.01f) {
                                f32 nx = clip.x / clip.w;
                                f32 ny = clip.y / clip.w;
                                if (nx >= -1.5f && nx <= 1.5f && ny >= -1.5f && ny <= 1.5f) {
                                    anyVisible = true;
                                    break;
                                }
                            }
                        }
                        if (!anyVisible) break;
                        
                        auto sampleTexture = [loadedMesh](const Vec2& uv) -> ImU32 {
                            if (loadedMesh->baseColorTexture.empty() || loadedMesh->textureWidth == 0) {
                                return IM_COL32(100, 150, 200, 180);
                            }
                            u32 x = (u32)(uv.x * (loadedMesh->textureWidth - 1));
                            u32 y = (u32)(uv.y * (loadedMesh->textureHeight - 1));
                            u32 idx = (y * loadedMesh->textureWidth + x) * 3;
                            if (idx + 2 < loadedMesh->baseColorTexture.size()) {
                                u8 r = loadedMesh->baseColorTexture[idx];
                                u8 g = loadedMesh->baseColorTexture[idx + 1];
                                u8 b = loadedMesh->baseColorTexture[idx + 2];
                                return IM_COL32(r, g, b, 180);
                            }
                            return IM_COL32(100, 150, 200, 180);
                        };
                        
                        for (size_t i = 0; i + 2 < loadedMesh->indices.size(); i += 3) {
                            u32 i0 = loadedMesh->indices[i];
                            u32 i1 = loadedMesh->indices[i + 1];
                            u32 i2 = loadedMesh->indices[i + 2];
                            
                            if (i0 >= loadedMesh->vertices.size() ||
                                i1 >= loadedMesh->vertices.size() ||
                                i2 >= loadedMesh->vertices.size()) continue;
                            
                            Vec3 v0 = loadedMesh->vertices[i0].position;
                            Vec3 v1 = loadedMesh->vertices[i1].position;
                            Vec3 v2 = loadedMesh->vertices[i2].position;
                            
                            Vec3 p0 = worldMatrix.transformPoint(v0);
                            Vec3 p1 = worldMatrix.transformPoint(v1);
                            Vec3 p2 = worldMatrix.transformPoint(v2);
                            
                            Vec4 c0 = vpMat.transformVec4(Vec4(p0.x, p0.y, p0.z, 1.0f));
                            Vec4 c1 = vpMat.transformVec4(Vec4(p1.x, p1.y, p1.z, 1.0f));
                            Vec4 c2 = vpMat.transformVec4(Vec4(p2.x, p2.y, p2.z, 1.0f));
                            if (c0.w <= 0.01f && c1.w <= 0.01f && c2.w <= 0.01f) continue;
                            
                            ImVec2 sp0 = projectToScreen(p0, origin, viewportSize, ctx);
                            ImVec2 sp1 = projectToScreen(p1, origin, viewportSize, ctx);
                            ImVec2 sp2 = projectToScreen(p2, origin, viewportSize, ctx);
                            
                            if (sp0.x < -5000.0f || sp1.x < -5000.0f || sp2.x < -5000.0f) continue;
                            
                            Vec2 uv0 = loadedMesh->vertices[i0].texcoord;
                            Vec2 uv1 = loadedMesh->vertices[i1].texcoord;
                            Vec2 uv2 = loadedMesh->vertices[i2].texcoord;
                            Vec2 uvAvg = Vec2((uv0.x + uv1.x + uv2.x) / 3.0f, (uv0.y + uv1.y + uv2.y) / 3.0f);
                            
                            ImU32 triColor = sampleTexture(uvAvg);
                            dl->AddTriangleFilled(sp0, sp1, sp2, triColor);
                        }
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Plane: {
                const Vec3 corners[] = {
                    {-0.5f, 0.0f, -0.5f}, { 0.5f, 0.0f, -0.5f},
                    { 0.5f, 0.0f,  0.5f}, {-0.5f, 0.0f,  0.5f}
                };
                Vec3 wp[4] = {
                    worldMatrix.transformPoint(corners[0]),
                    worldMatrix.transformPoint(corners[1]),
                    worldMatrix.transformPoint(corners[2]),
                    worldMatrix.transformPoint(corners[3])
                };

                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 nWorld = matrixAxis(worldMatrix, 1, Vec3::up());
                    Vec3 center = (wp[0] + wp[1] + wp[2] + wp[3]) * 0.25f;
                    const Vec3 lit = lightColorAt(center, nWorld);
                    const ImU32 fill = toColor(Vec3(lit.x * 0.80f, lit.y * 0.80f, lit.z * 0.80f), 0.88f);
                    dl->AddQuadFilled(projectToScreen(wp[0], origin, viewportSize, ctx),
                                      projectToScreen(wp[1], origin, viewportSize, ctx),
                                      projectToScreen(wp[2], origin, viewportSize, ctx),
                                      projectToScreen(wp[3], origin, viewportSize, ctx),
                                      fill);
                }

                for (int i = 0; i < 4; ++i) {
                    drawEdge(wp[i], wp[(i + 1) % 4]);
                }
                if (drawPolygons) {
                    drawPoly(wp[0], wp[2]);
                    if (densityLevel >= 2) drawPoly(wp[1], wp[3]);
                }
                break;
            }
            case ECS::MeshPrimitive::Sphere: {
                Vec3 center = worldMatrix.transformPoint(Vec3(0, 0, 0));
                Vec3 px = worldMatrix.transformPoint(Vec3(0.5f, 0, 0));
                ImVec2 cs = projectToScreen(center, origin, viewportSize, ctx);
                ImVec2 pxs = projectToScreen(px, origin, viewportSize, ctx);
                f32 rad = std::sqrt((pxs.x - cs.x) * (pxs.x - cs.x) + (pxs.y - cs.y) * (pxs.y - cs.y));
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    const Vec3 lit = lightColorAt(center, entityAxis(world, entity, 2, Vec3(0, 0, 1)));
                    dl->AddCircleFilled(cs, rad, toColor(Vec3(lit.x * 0.78f, lit.y * 0.78f, lit.z * 0.78f), 0.90f), 24);
                }
                if (drawContour) {
                    dl->AddCircle(cs, rad, wireCol, 32, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, 28, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 28, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, 28, wireCol, thickness);
                }
                if (drawPolygons) {
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, spherePolySegments, polyCol, polyThickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, spherePolySegments, polyCol, polyThickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, spherePolySegments, polyCol, polyThickness);
                }
                break;
            }
            case ECS::MeshPrimitive::Cylinder: {
                Mat4 topMatrix = worldMatrix * Mat4::translation(0.0f, 0.5f, 0.0f);
                Mat4 bottomMatrix = worldMatrix * Mat4::translation(0.0f, -0.5f, 0.0f);
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 cTop = topMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cBottom = bottomMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cMid = (cTop + cBottom) * 0.5f;
                    const Vec3 lit = lightColorAt(cMid, entityAxis(world, entity, 0, Vec3::right()));
                    dl->AddLine(projectToScreen(cTop, origin, viewportSize, ctx),
                                projectToScreen(cBottom, origin, viewportSize, ctx),
                                toColor(Vec3(lit.x * 0.72f, lit.y * 0.72f, lit.z * 0.72f), 0.75f), 18.0f * ctx.viewportZoom);
                }
                if (drawContour) {
                    drawRing(topMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(bottomMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                }
                for (int i = 0; i < 4; ++i) {
                    const f32 a = (3.14159265f * 0.5f) * static_cast<f32>(i);
                    const Vec3 rim(std::cos(a) * 0.5f, 0.0f, std::sin(a) * 0.5f);
                    drawEdge(topMatrix.transformPoint(rim), bottomMatrix.transformPoint(rim));
                }
                if (drawPolygons) {
                    for (int i = 0; i < sidePolySlices; ++i) {
                        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(sidePolySlices);
                        const f32 a1 = (2.0f * 3.14159265f * static_cast<f32>((i + 1) % sidePolySlices)) / static_cast<f32>(sidePolySlices);
                        const Vec3 t0(std::cos(a0) * 0.5f, 0.5f, std::sin(a0) * 0.5f);
                        const Vec3 b1(std::cos(a1) * 0.5f, -0.5f, std::sin(a1) * 0.5f);
                        drawPoly(worldMatrix.transformPoint(t0), worldMatrix.transformPoint(b1));
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Capsule: {
                Mat4 topMatrix = worldMatrix * Mat4::translation(0.0f, 0.25f, 0.0f);
                Mat4 bottomMatrix = worldMatrix * Mat4::translation(0.0f, -0.25f, 0.0f);
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 cTop = topMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cBottom = bottomMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cMid = (cTop + cBottom) * 0.5f;
                    const Vec3 lit = lightColorAt(cMid, entityAxis(world, entity, 1, Vec3::up()));
                    dl->AddLine(projectToScreen(cTop, origin, viewportSize, ctx),
                                projectToScreen(cBottom, origin, viewportSize, ctx),
                                toColor(Vec3(lit.x * 0.70f, lit.y * 0.70f, lit.z * 0.70f), 0.70f), 20.0f * ctx.viewportZoom);
                }
                if (drawContour) {
                    drawRing(topMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(bottomMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, 24, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                }
                for (int i = 0; i < 4; ++i) {
                    const f32 a = (3.14159265f * 0.5f) * static_cast<f32>(i);
                    const Vec3 rim(std::cos(a) * 0.5f, 0.0f, std::sin(a) * 0.5f);
                    drawEdge(topMatrix.transformPoint(rim), bottomMatrix.transformPoint(rim));
                }
                if (drawPolygons) {
                    for (int i = 0; i < sidePolySlices; ++i) {
                        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(sidePolySlices);
                        const f32 a1 = (2.0f * 3.14159265f * static_cast<f32>((i + 1) % sidePolySlices)) / static_cast<f32>(sidePolySlices);
                        const Vec3 t0(std::cos(a0) * 0.5f, 0.25f, std::sin(a0) * 0.5f);
                        const Vec3 b1(std::cos(a1) * 0.5f, -0.25f, std::sin(a1) * 0.5f);
                        drawPoly(worldMatrix.transformPoint(t0), worldMatrix.transformPoint(b1));
                    }
                }
                break;
            }
        }

        return true;
    };

    auto drawEntity = [&](ECS::Entity entity) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        if (world.has<ECS::LightComponent>(entity)) return;

        if (drawMeshPreview(entity)) return;

        Vec3 worldPosition;
        if (!tryGetEntityPosition(world, entity, worldPosition)) return;
        drawMarker(entity, worldPosition);
    };

    ECS::ComponentQuery transformQuery;
    transformQuery.with<ECS::Transform>();
    transformQuery.without<ECS::Sprite>();
    world.forEach<ECS::Transform>(transformQuery, [&](ECS::Entity entity, ECS::Transform&) {
        drawEntity(entity);
    });

    ECS::ComponentQuery pos3Query;
    pos3Query.with<ECS::Position3D>();
    pos3Query.without<ECS::Transform>();
    pos3Query.without<ECS::Sprite>();
    world.forEach<ECS::Position3D>(pos3Query, [&](ECS::Entity entity, ECS::Position3D&) {
        drawEntity(entity);
    });
}

void SceneViewport::drawLightGizmos(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();

        u32 directionalIndex = 0;
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::DirectionalLightComponent&) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 anchor;
                if (!tryGetEntityPosition(world, entity, anchor)) {
                    anchor = Vec3(ctx.camFocus.x, ctx.camFocus.y, ctx.camFocus.z)
                           + Vec3(8.0f + static_cast<f32>(directionalIndex) * 2.5f, 6.0f, 0.0f);
                }
                ++directionalIndex;

                const bool selected = (ctx.selectedEntity == entity);
                const Vec3 dir = entityForward(world, entity);
                const Vec3 tip = anchor + dir * 2.5f;
                const ImVec2 screenPos = projectToScreen(anchor, origin, viewportSize, ctx);
                const ImVec2 tipPos = projectToScreen(tip, origin, viewportSize, ctx);
                const ImU32 color = lightColor(lc, selected);

                const f32 sunRadius = 8.0f;
                const f32 rayLength = 12.0f;
                const int rayCount = 6;

                dl->AddCircleFilled(screenPos, sunRadius * 0.5f, color, 12);

                for (int i = 0; i < rayCount; ++i) {
                    f32 angle = (2.0f * 3.14159265f / rayCount) * i;
                    f32 x1 = sunRadius * std::cos(angle);
                    f32 y1 = sunRadius * std::sin(angle);
                    f32 x2 = (sunRadius + rayLength) * std::cos(angle);
                    f32 y2 = (sunRadius + rayLength) * std::sin(angle);
                    dl->AddLine(ImVec2(screenPos.x + x1, screenPos.y + y1),
                               ImVec2(screenPos.x + x2, screenPos.y + y2), color, 2.0f);
                }

                dl->AddLine(screenPos, tipPos, color, selected ? 2.5f : 1.5f);
                dl->AddCircleFilled(tipPos, 3.0f, color, 10);

                dl->AddText(ImVec2(screenPos.x + 12, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Dir");
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();

        world.forEach<ECS::LightComponent, ECS::PointLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::PointLightComponent& ptLight) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 position;
                if (!tryGetEntityPosition(world, entity, position)) return;

                ImVec2 screenPos = projectToScreen(position, origin, viewportSize, ctx);
                const bool selected = (ctx.selectedEntity == entity);
                ImU32 color = lightColor(lc, selected);

                Vec3 radiusTestPoint = position + entityAxis(world, entity, 0, Vec3::right()) * ptLight.radius;
                ImVec2 radiusScreenPoint = projectToScreen(radiusTestPoint, origin, viewportSize, ctx);
                f32 radiusScreenDist = std::sqrt(
                    (radiusScreenPoint.x - screenPos.x) * (radiusScreenPoint.x - screenPos.x) +
                    (radiusScreenPoint.y - screenPos.y) * (radiusScreenPoint.y - screenPos.y)
                );

                dl->AddCircleFilled(screenPos, 5.0f, color, 16);
                dl->AddCircle(screenPos, radiusScreenDist, color, 16, 1.5f);
                dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Pt");
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::SpotLightComponent>();

        world.forEach<ECS::LightComponent, ECS::SpotLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::SpotLightComponent& spotLight) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 position;
                if (!tryGetEntityPosition(world, entity, position)) return;

                const bool selected = (ctx.selectedEntity == entity);
                const Vec3 dir = entityForward(world, entity);
                Vec3 right = entityAxis(world, entity, 0, Vec3::right());
                if (std::abs(dir.dot(right)) > 0.95f) right = Vec3::up();
                Vec3 up = dir.cross(right).normalized();
                right = up.cross(dir).normalized();

                ImVec2 screenPos = projectToScreen(position, origin, viewportSize, ctx);
                ImU32 color = lightColor(lc, selected);

                Vec3 coneEnd = position + dir * spotLight.radius;
                const f32 coneRadius = spotLight.radius * std::sin(spotLight.angle * kDegToRad * 0.5f);
                Vec3 baseRight = coneEnd + right * coneRadius;
                Vec3 baseLeft  = coneEnd - right * coneRadius;
                Vec3 baseUp    = coneEnd + up * coneRadius;
                Vec3 baseDown  = coneEnd - up * coneRadius;

                dl->AddLine(screenPos, projectToScreen(baseRight, origin, viewportSize, ctx), color, selected ? 2.5f : 1.5f);
                dl->AddLine(screenPos, projectToScreen(baseLeft, origin, viewportSize, ctx), color, selected ? 2.5f : 1.5f);
                dl->AddLine(screenPos, projectToScreen(baseUp, origin, viewportSize, ctx), color, selected ? 2.0f : 1.25f);
                dl->AddLine(screenPos, projectToScreen(baseDown, origin, viewportSize, ctx), color, selected ? 2.0f : 1.25f);
                dl->AddLine(projectToScreen(baseRight, origin, viewportSize, ctx), projectToScreen(baseUp, origin, viewportSize, ctx), color, 1.25f);
                dl->AddLine(projectToScreen(baseUp, origin, viewportSize, ctx), projectToScreen(baseLeft, origin, viewportSize, ctx), color, 1.25f);
                dl->AddLine(projectToScreen(baseLeft, origin, viewportSize, ctx), projectToScreen(baseDown, origin, viewportSize, ctx), color, 1.25f);
                dl->AddLine(projectToScreen(baseDown, origin, viewportSize, ctx), projectToScreen(baseRight, origin, viewportSize, ctx), color, 1.25f);
                dl->AddCircleFilled(screenPos, 5.0f, color, 12);
                dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Sp");
            });
    }
}

void SceneViewport::createOrUpdateLightGizmoEntities(ECS::World& world) {
}

void SceneViewport::drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;
    auto* pos = world.get<ECS::Transform>(ctx.selectedEntity);
    auto* pos3 = world.get<ECS::Position3D>(ctx.selectedEntity);
    if (!pos && !pos3) {
        pos = &world.add<ECS::Transform>(ctx.selectedEntity);
    }

    Vec3 worldPos;
    if (!tryGetEntityPosition(world, ctx.selectedEntity, worldPos)) return;
    ImVec2 screenPos = projectToScreen(worldPos, origin, viewportSize, ctx);
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const float HL   = 30.0f * ctx.viewportZoom;
    const bool  is3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D);
    const bool  zDimmed = (ctx.viewMode == EditorContext::ViewMode::Mode2D);

    // vx   = cosY*ax + sinY*az
    // vy2  = cosP*ay + sinP*(-sinY*ax + cosY*az)
    // vz2  = -sinP*ay + cosP*(-sinY*ax + cosY*az)
    float sinY = 0, cosY = 1, sinP = 0, cosP = 1;
    if (is3D) { sinY=std::sin(ctx.camYaw); cosY=std::cos(ctx.camYaw); sinP=std::sin(ctx.camPitch); cosP=std::cos(ctx.camPitch); }

    auto proj2D = [&](float ax, float ay, float az) -> ImVec2 {
        if (!is3D) return ImVec2(ax, -ay);
        float vx  = cosY*ax + sinY*az;
        float vzc = -sinY*ax + cosY*az;
        float vy2 = cosP*ay + sinP*vzc;
        return ImVec2(vx, -vy2);
    };
    auto vdepth = [&](float ax, float ay, float az) -> float {
        float vzc = -sinY*ax + cosY*az;
        return -sinP*ay + cosP*vzc;
    };

    ImVec2 rawX = proj2D(1,0,0), rawY = proj2D(0,1,0), rawZ = proj2D(0,0,1);

    if (is3D) {
        const Mat4 worldMatrix = entityMatrix(world, ctx.selectedEntity);
        rawX = proj2D(worldMatrix(0,0), worldMatrix(1,0), worldMatrix(2,0));
        rawY = proj2D(worldMatrix(0,1), worldMatrix(1,1), worldMatrix(2,1));
        rawZ = proj2D(worldMatrix(0,2), worldMatrix(1,2), worldMatrix(2,2));
    }
    m_axisRawDirs[0] = rawX; m_axisRawDirs[1] = rawY; m_axisRawDirs[2] = rawZ;
    m_gizmoScreenOrigin = screenPos;

    auto axisEnd = [&](ImVec2 raw) -> ImVec2 {
        float mag = std::sqrt(raw.x*raw.x + raw.y*raw.y);
        float len = HL * std::max(mag, 0.4f);
        if (mag < 0.001f) return screenPos;
        return ImVec2(screenPos.x + raw.x/mag*len, screenPos.y + raw.y/mag*len);
    };
    auto axisAlpha = [](ImVec2 raw) -> float {
        float mag = std::sqrt(raw.x*raw.x + raw.y*raw.y);
        return 0.4f + 0.6f * mag;
    };

    ImVec2 endX = axisEnd(rawX), endY = axisEnd(rawY), endZ = axisEnd(rawZ);
    float  alpX = axisAlpha(rawX), alpY = axisAlpha(rawY), alpZ = axisAlpha(rawZ);

    struct DrawEntry { int id; float depth; };
    DrawEntry order[3] = {{1,vdepth(1,0,0)},{2,vdepth(0,1,0)},{3,vdepth(0,0,1)}};
    std::sort(order, order+3, [](const DrawEntry& a, const DrawEntry& b){ return a.depth > b.depth; });

    int keyAxis = 0;
    if      (ImGui::IsKeyDown(ImGuiKey_X)) keyAxis = 1;
    else if (ImGui::IsKeyDown(ImGuiKey_Y)) keyAxis = 2;
    else if (ImGui::IsKeyDown(ImGuiKey_Z)) keyAxis = 3;

    ImVec2 mouse = ImGui::GetMousePos();
    bool mouseInVP = mouse.x >= origin.x && mouse.x <= origin.x+viewportSize.x &&
                     mouse.y >= origin.y && mouse.y <= origin.y+viewportSize.y;

    if (!m_gizmoDragging && mouseInVP && ImGui::IsWindowHovered()) {
        if (keyAxis != 0) {
            m_hoveredAxis = keyAxis;
        } else {
            m_hoveredAxis = 0;
            float cdist = std::sqrt((mouse.x-screenPos.x)*(mouse.x-screenPos.x)+(mouse.y-screenPos.y)*(mouse.y-screenPos.y));
            if (cdist < 9.f) {
                m_hoveredAxis = 4;
            } else if (ctx.gizmoMode == EditorContext::GizmoMode::Rotate) {
                auto ringHit = [&](ImVec2 b1, ImVec2 b2) -> bool {
                    const int N = 48;
                    for (int i = 0; i < N; ++i) {
                        float a = 6.28318f * i / N;
                        float px = screenPos.x + HL*(std::cos(a)*b1.x + std::sin(a)*b2.x);
                        float py = screenPos.y + HL*(std::cos(a)*b1.y + std::sin(a)*b2.y);
                        float dx = mouse.x-px, dy = mouse.y-py;
                        if (dx*dx+dy*dy < 64.f) return true;
                    }
                    return false;
                };
                if      (ringHit(proj2D(0,1,0), proj2D(0,0,1))) m_hoveredAxis = 1;
                else if (ringHit(proj2D(1,0,0), proj2D(0,0,1))) m_hoveredAxis = 2;
                else if (ringHit(proj2D(1,0,0), proj2D(0,1,0))) m_hoveredAxis = 3;
            } else {
                auto ptLineDist = [&](ImVec2 b, ImVec2 e) -> float {
                    float dx=e.x-b.x, dy=e.y-b.y, l2=dx*dx+dy*dy;
                    if (l2 < 0.0001f) return std::sqrt((mouse.x-b.x)*(mouse.x-b.x)+(mouse.y-b.y)*(mouse.y-b.y));
                    float t = std::max(0.f,std::min(1.f,((mouse.x-b.x)*dx+(mouse.y-b.y)*dy)/l2));
                    float px=b.x+t*dx, py=b.y+t*dy;
                    return std::sqrt((mouse.x-px)*(mouse.x-px)+(mouse.y-py)*(mouse.y-py));
                };
                if      (ptLineDist(screenPos, endX) < 8.f) m_hoveredAxis = 1;
                else if (ptLineDist(screenPos, endY) < 8.f) m_hoveredAxis = 2;
                else if (!zDimmed && ptLineDist(screenPos, endZ) < 8.f) m_hoveredAxis = 3;
            }
        }
    }

    const u32 COL_X = IM_COL32(255,50,50,255), COL_Y = IM_COL32(50,255,50,255), COL_Z = IM_COL32(50,100,255,255);
    const u32 COL_HOVER = IM_COL32(255,220,0,255), COL_DRAG = IM_COL32(255,255,255,255);

    auto axisColor = [&](int axId, float alpha) -> u32 {
        int activeAx = m_gizmoDragging ? m_gizmoDragAxis : (keyAxis ? keyAxis : m_hoveredAxis);
        if (activeAx == axId) return m_gizmoDragging ? COL_DRAG : COL_HOVER;
        u32 base = (axId==1) ? COL_X : (axId==2) ? COL_Y : COL_Z;
        return (base & 0x00FFFFFFu) | (u32(std::min(alpha,1.f)*255.f) << 24);
    };

    auto drawArrow = [&](ImVec2 from, ImVec2 to, u32 col) {
        float dx=to.x-from.x, dy=to.y-from.y, d=std::sqrt(dx*dx+dy*dy);
        dl->AddLine(from, to, col, 3.f);
        if (d < 1.f) { dl->AddCircleFilled(from, 4.f, col, 12); return; }
        float ux=dx/d, uy=dy/d;
        ImVec2 tip(to.x+ux*8.f, to.y+uy*8.f);
        dl->AddTriangleFilled(tip, ImVec2(to.x-uy*5.f,to.y+ux*5.f), ImVec2(to.x+uy*5.f,to.y-ux*5.f), col);
    };
    auto drawScaleBox = [&](ImVec2 from, ImVec2 to, u32 col) {
        dl->AddLine(from, to, col, 2.f);
        dl->AddRectFilled(ImVec2(to.x-5.f,to.y-5.f), ImVec2(to.x+5.f,to.y+5.f), col);
    };
    auto drawRing = [&](ImVec2 b1, ImVec2 b2, u32 col) {
        const int N = 64;
        for (int i = 0; i < N; ++i) {
            float a0=6.28318f*i/N, a1=6.28318f*(i+1)/N;
            ImVec2 p0(screenPos.x+HL*(std::cos(a0)*b1.x+std::sin(a0)*b2.x), screenPos.y+HL*(std::cos(a0)*b1.y+std::sin(a0)*b2.y));
            ImVec2 p1(screenPos.x+HL*(std::cos(a1)*b1.x+std::sin(a1)*b2.x), screenPos.y+HL*(std::cos(a1)*b1.y+std::sin(a1)*b2.y));
            dl->AddLine(p0, p1, col, 2.f);
        }
    };

    for (int i = 0; i < 3; ++i) {
        int axId = order[i].id;
        if (axId == 3 && zDimmed) continue;
        ImVec2 end  = (axId==1) ? endX : (axId==2) ? endY : endZ;
        float  alp  = (axId==1) ? alpX : (axId==2) ? alpY : alpZ;
        u32    col  = axisColor(axId, alp);
        if      (ctx.gizmoMode == EditorContext::GizmoMode::Translate) drawArrow(screenPos, end, col);
        else if (ctx.gizmoMode == EditorContext::GizmoMode::Scale)     drawScaleBox(screenPos, end, col);
        else if (ctx.gizmoMode == EditorContext::GizmoMode::Rotate) {
            ImVec2 b1 = (axId==1) ? proj2D(0,1,0) : proj2D(1,0,0);
            ImVec2 b2 = (axId==3) ? proj2D(0,1,0) : proj2D(0,0,1);
            drawRing(b1, b2, axisColor(axId, 1.f));
        }
    }

    if (ctx.gizmoMode == EditorContext::GizmoMode::None)
        dl->AddCircle(screenPos, 6.f, IM_COL32(255,255,255,180), 12, 2.f);

    if (world.has<Audio::AudioEmitter>(ctx.selectedEntity)) {
        auto* emitter = world.get<Audio::AudioEmitter>(ctx.selectedEntity);
        if (emitter->spatial && emitter->maxDistance > 0.0f) {
            f32 w2s = ctx.viewportZoom * 50.0f;
            f32 fullVolumeRadius = emitter->maxDistance * 0.5f;
            char buf[64];
            dl->AddCircle(screenPos, fullVolumeRadius * w2s, IM_COL32(0, 255, 255, 80), 48, 2.0f);
            snprintf(buf, sizeof(buf), "near %.0f", fullVolumeRadius);
            dl->AddText(ImVec2(screenPos.x + fullVolumeRadius * w2s + 4, screenPos.y - 8), IM_COL32(0, 255, 255, 180), buf);
            dl->AddCircle(screenPos, emitter->maxDistance * w2s, IM_COL32(50, 130, 255, 60), 64, 2.0f);
            snprintf(buf, sizeof(buf), "max %.0f", emitter->maxDistance);
            dl->AddText(ImVec2(screenPos.x + emitter->maxDistance * w2s + 4, screenPos.y - 8), IM_COL32(50, 130, 255, 180), buf);
            dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 20), IM_COL32(180, 180, 255, 220), "S");
        }
    }
}

void SceneViewport::drawPhysicsDebug(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.physicsDebugVisible) return;
    using namespace Physics2D;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;

    auto worldToScreen_fn = [&](f32 wx, f32 wy) -> ImVec2 {
        return projectToScreen({wx, wy, 0.0f}, origin, viewportSize, ctx);
    };

    ECS::ComponentQuery q;
    q.with<Collider2D>();
    q.with<ECS::Transform>();

    world.forEach<Collider2D, ECS::Transform>(q,
        [&](ECS::Entity entity, Collider2D& col, ECS::Transform& pos) {
            f32 cx = pos.position.x + col.offset.x;
            f32 cy = pos.position.y + col.offset.y;
            ImU32 color = IM_COL32(col.debugColor[0], col.debugColor[1], col.debugColor[2], col.debugColor[3]);
            if (col.shape == ColliderShape::AABB) {
                f32 hw = col.size.x * 0.5f * worldToScreen;
                f32 hh = col.size.y * 0.5f * worldToScreen;
                ImVec2 sc = worldToScreen_fn(cx, cy);
                dl->AddRect(ImVec2(sc.x - hw, sc.y - hh),
                            ImVec2(sc.x + hw, sc.y + hh),
                            color, 0.0f, 0, 1.5f);
            } else if (col.shape == ColliderShape::Circle) {
                ImVec2 sc = worldToScreen_fn(cx, cy);
                dl->AddCircle(sc, col.radius * worldToScreen, color, 32, 1.5f);
            }
        });
}

void SceneViewport::drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    if (!m_config.grid) return;

    if (ctx.viewMode != EditorContext::ViewMode::Mode2D) {
        drawGrid3D(drawList, origin, viewportSize, ctx);
        return;
    }

    f32 baseSpacing = m_config.gridSpacing;
    f32 scaledSpacing = baseSpacing * ctx.viewportZoom;
    const f32 minPixelSpacing = 12.0f;
    while (scaledSpacing < minPixelSpacing) {
        scaledSpacing *= 2.0f;
    }
    
    f32 centerX = origin.x + viewportSize.x * 0.5f;
    f32 centerY = origin.y + viewportSize.y * 0.5f;
    f32 offsetX = ctx.viewportPanX;
    f32 offsetY = ctx.viewportPanY;
    f32 axisX = centerX + offsetX;
    f32 axisY = centerY + offsetY;
    
    ImU32 gridColor = IM_COL32(100, 100, 120, 80);
    ImU32 axisColor = IM_COL32(200, 100, 100, 150);

    f32 startX = axisX - std::floor((axisX - origin.x) / scaledSpacing) * scaledSpacing;
    f32 startY = axisY - std::floor((axisY - origin.y) / scaledSpacing) * scaledSpacing;

    for (f32 x = startX; x <= origin.x + viewportSize.x; x += scaledSpacing) {
        if (std::fabs(x - axisX) < 1.0f) {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), gridColor, 0.5f);
        }
    }
    
    for (f32 y = startY; y <= origin.y + viewportSize.y; y += scaledSpacing) {
        if (std::fabs(y - axisY) < 1.0f) {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), gridColor, 0.5f);
        }
    }
    
    drawList->AddCircle(ImVec2(centerX, centerY), 8.0f, IM_COL32(255, 200, 0, 200), 12, 2.0f);
}

void SceneViewport::drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    ImU32 gridColor  = IM_COL32(100, 100, 120, 60);
    ImU32 axisColorX = IM_COL32(220, 60, 60, 200);
    ImU32 axisColorZ = IM_COL32(60, 60, 220, 200);

    f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
    f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);

    Vec3 camPos;
    camPos.x = ctx.camFocus.x + sinY * cosP * ctx.camDistance;
    camPos.y = ctx.camFocus.y - sinP * ctx.camDistance;
    camPos.z = ctx.camFocus.z - cosY * cosP * ctx.camDistance;

    Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
    f32 aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);
    f32 fov = 1.0472f;
    f32 zNear = 0.1f;
    f32 zFar = 10000.0f;
    Mat4 proj = Mat4::perspective(fov, aspect, zNear, zFar);
    Mat4 vp = proj * view;

    auto project = [&](Vec3 worldPt, ImVec2& screenOut) -> bool {
        Vec4 clip = vp.transformVec4(Vec4(worldPt.x, worldPt.y, worldPt.z, 1.0f));
        if (clip.w <= 0.01f) return false;
        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;
        if (ndcX < -1.5f || ndcX > 1.5f || ndcY < -1.5f || ndcY > 1.5f) return false;
        screenOut.x = origin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x;
        screenOut.y = origin.y + (1.0f - ndcY) * 0.5f * viewportSize.y;
        return true;
    };

    auto drawLine3D = [&](Vec3 a, Vec3 b, ImU32 color, float thickness) {
        ImVec2 sa, sb;
        bool va = project(a, sa);
        bool vb = project(b, sb);
        if (!va && !vb) return;
        if (va && vb) {
            dl->AddLine(sa, sb, color, thickness);
            return;
        }
        Vec4 clipA = vp.transformVec4(Vec4(a.x, a.y, a.z, 1.0f));
        Vec4 clipB = vp.transformVec4(Vec4(b.x, b.y, b.z, 1.0f));
        f32 wMin = 0.01f;
        if (clipA.w < wMin && clipB.w < wMin) return;
        if (clipA.w < wMin) {
            f32 t = (wMin - clipA.w) / (clipB.w - clipA.w);
            a = Vec3(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z));
        } else if (clipB.w < wMin) {
            f32 t = (wMin - clipB.w) / (clipA.w - clipB.w);
            b = Vec3(b.x + t * (a.x - b.x), b.y + t * (a.y - b.y), b.z + t * (a.z - b.z));
        }
        if (project(a, sa) && project(b, sb)) {
            dl->AddLine(sa, sb, color, thickness);
        }
    };

    float visibleRange = ctx.camDistance;
    float spacing = 1.0f;
    while (visibleRange / spacing > 30.0f) spacing *= 2.0f;
    while (visibleRange / spacing < 5.0f && spacing > 0.5f) spacing *= 0.5f;
    spacing = std::max(spacing, 0.5f);

    float renderDist = visibleRange * 2.0f;
    int maxLines = 120;
    if ((renderDist * 2.0f / spacing) > maxLines) {
        renderDist = (maxLines * spacing) / 2.0f;
    }

    float camX = ctx.camFocus.x;
    float camZ = ctx.camFocus.z;
    int startX = (int)(std::floor((camX - renderDist) / spacing)) * (int)spacing;
    int endX   = (int)(std::ceil((camX + renderDist) / spacing)) * (int)spacing;
    int startZ = (int)(std::floor((camZ - renderDist) / spacing)) * (int)spacing;
    int endZ   = (int)(std::ceil((camZ + renderDist) / spacing)) * (int)spacing;

    float lineDist = renderDist;

    if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
        for (int x = startX; x <= endX; x += (int)spacing) {
            if (x == 0) continue;
            drawLine3D({(f32)x, (f32)(camZ - lineDist), 0.f}, {(f32)x, (f32)(camZ + lineDist), 0.f}, gridColor, 0.5f);
        }
        for (int z = startZ; z <= endZ; z += (int)spacing) {
            if (z == 0) continue;
            drawLine3D({(f32)(camX - lineDist), (f32)z, 0.f}, {(f32)(camX + lineDist), (f32)z, 0.f}, gridColor, 0.5f);
        }
        float axisLen = visibleRange * 100.0f;
        drawLine3D({0.f, -axisLen, 0.f}, {0.f, axisLen, 0.f}, axisColorX, 2.5f);
        drawLine3D({-axisLen, 0.f, 0.f}, {axisLen, 0.f, 0.f}, axisColorZ, 2.5f);
    } else {
        for (int x = startX; x <= endX; x += (int)spacing) {
            if (x == 0) continue;
            drawLine3D({(f32)x, 0.f, (f32)(camZ - lineDist)}, {(f32)x, 0.f, (f32)(camZ + lineDist)}, gridColor, 0.5f);
        }
        for (int z = startZ; z <= endZ; z += (int)spacing) {
            if (z == 0) continue;
            drawLine3D({(f32)(camX - lineDist), 0.f, (f32)z}, {(f32)(camX + lineDist), 0.f, (f32)z}, gridColor, 0.5f);
        }
        float axisLen = visibleRange * 100.0f;
        drawLine3D({0.f, 0.f, -axisLen}, {0.f, 0.f, axisLen}, axisColorZ, 2.5f);
        drawLine3D({-axisLen, 0.f, 0.f}, {axisLen, 0.f, 0.f}, axisColorX, 2.5f);
    }
}

void SceneViewport::drawNavigationWidget(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padding = 12.0f;
    const float buttonWidth = 62.0f;
    const float widgetSize = 84.0f;

    ImVec2 widgetMin(
        origin.x + viewportSize.x - padding - buttonWidth - 6.0f - widgetSize,
        origin.y + viewportSize.y - padding - widgetSize
    );
    ImVec2 widgetMax(widgetMin.x + widgetSize, widgetMin.y + widgetSize);

    dl->AddRectFilled(widgetMin, widgetMax, IM_COL32(18, 20, 26, 190), 6.0f);
    dl->AddRect(widgetMin, widgetMax, IM_COL32(90, 100, 130, 180), 6.0f, 0, 1.0f);

    bool is3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                 ctx.viewMode == EditorContext::ViewMode::Isometric);

    ImVec2 center(widgetMin.x + widgetSize * 0.5f, widgetMin.y + widgetSize * 0.5f);
    const float axisLen = 22.0f;

    {
        float sinY = std::sin(ctx.camYaw),  cosY = std::cos(ctx.camYaw);
        float sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);

        auto axisScreenDir = [&](float wx, float wy, float wz) -> ImVec2 {
            float sx  =  cosY * wx + sinY * wz;
            float sy  =  wy;
            float sz  = -sinY * wx + cosY * wz;
            float sy2 =  cosP * sy + sinP * sz;
            float len = std::sqrt(sx * sx + sy2 * sy2);
            if (len < 0.001f) return ImVec2(0.f, 0.f);
            return ImVec2(sx / len * axisLen, -sy2 / len * axisLen);
        };

        ImVec2 xDir = axisScreenDir(1.f, 0.f, 0.f);
        ImVec2 yDir = axisScreenDir(0.f, 1.f, 0.f);
        ImVec2 zDir = axisScreenDir(0.f, 0.f, 1.f);

        dl->AddLine(center, ImVec2(center.x + xDir.x, center.y + xDir.y), IM_COL32(255, 70, 70, 255), 2.0f);
        dl->AddText(ImVec2(center.x + xDir.x + 3.f, center.y + xDir.y - 8.f), IM_COL32(255, 90, 90, 255), "X");

        dl->AddLine(center, ImVec2(center.x + yDir.x, center.y + yDir.y), IM_COL32(70, 255, 90, 255), 2.0f);
        dl->AddText(ImVec2(center.x + yDir.x - 4.f, center.y + yDir.y - 14.f), IM_COL32(90, 255, 110, 255), "Y");

        if (is3D) {
            dl->AddLine(center, ImVec2(center.x + zDir.x, center.y + zDir.y), IM_COL32(90, 140, 255, 255), 2.0f);
            dl->AddText(ImVec2(center.x + zDir.x - 12.f, center.y + zDir.y - 6.f), IM_COL32(120, 165, 255, 255), "Z");
        }
    }

    dl->AddCircleFilled(center, 2.8f, IM_COL32(240, 240, 240, 255));
    dl->AddText(ImVec2(widgetMin.x + 6.0f, widgetMin.y + widgetSize - 18.0f), IM_COL32(220, 220, 230, 220), is3D ? "3D" : "2D");

    ImVec2 btnPos(widgetMax.x + 6.0f, widgetMin.y + widgetSize - 24.0f);
    ImGui::SetCursorScreenPos(btnPos);
    if (ImGui::Button(m_projectionMode == ProjectionMode::Perspective ? "Persp" : "Ortho", ImVec2(buttonWidth, 24.0f))) {
        toggleProjectionMode();
    }
}

f32 SceneViewport::rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                                     const Vec3& aabbMin, const Vec3& aabbMax) {
    f32 t_enter = 0.0f;
    f32 t_exit = 1e10f;
    
    // Test X slab
    if (std::abs(rayDir.x) > 1e-6f) {
        f32 t0 = (aabbMin.x - rayOrigin.x) / rayDir.x;
        f32 t1 = (aabbMax.x - rayOrigin.x) / rayDir.x;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.x < aabbMin.x || rayOrigin.x > aabbMax.x) {
            return -1.0f;
        }
    }
    
    // Test Y slab
    if (std::abs(rayDir.y) > 1e-6f) {
        f32 t0 = (aabbMin.y - rayOrigin.y) / rayDir.y;
        f32 t1 = (aabbMax.y - rayOrigin.y) / rayDir.y;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.y < aabbMin.y || rayOrigin.y > aabbMax.y) {
            return -1.0f;
        }
    }
    
    // Test Z slab
    if (std::abs(rayDir.z) > 1e-6f) {
        f32 t0 = (aabbMin.z - rayOrigin.z) / rayDir.z;
        f32 t1 = (aabbMax.z - rayOrigin.z) / rayDir.z;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.z < aabbMin.z || rayOrigin.z > aabbMax.z) {
            return -1.0f;
        }
    }
    
    if (t_enter <= t_exit && t_enter >= 0.0f) {
        return t_enter;
    }
    
    return -1.0f;
}

ECS::Entity SceneViewport::raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                               ECS::World& world) {
    ECS::Entity closestEntity = ECS::Entity::INVALID;
    f32 closestT = 1e10f;
    
    ECS::ComponentQuery query;
    query.with<ECS::Transform>();
    
    world.forEach<ECS::Transform>(query, [&](ECS::Entity entity, ECS::Transform& transform) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        
        Vec3 aabbMin = transform.position;
        Vec3 aabbMax = transform.position;
        
        if (auto* meshFilter = world.get<ECS::MeshFilterComponent>(entity)) {
            if (!meshFilter->customMeshPath.empty()) {
                auto* mesh = Assets::MeshCache::getInstance().getMesh(meshFilter->customMeshPath);
                if (!mesh) {
                    std::string fullPath = std::string("assets/raw/") + meshFilter->customMeshPath;
                    mesh = Assets::MeshCache::getInstance().getMesh(fullPath);
                }
                
                if (mesh && !mesh->vertices.empty()) {
                    Vec3 meshMin = mesh->bounds.min;
                    Vec3 meshMax = mesh->bounds.max;
                    
                    aabbMin = transform.position + Vec3(meshMin.x * transform.scale.x, 
                                                         meshMin.y * transform.scale.y, 
                                                         meshMin.z * transform.scale.z);
                    aabbMax = transform.position + Vec3(meshMax.x * transform.scale.x, 
                                                         meshMax.y * transform.scale.y, 
                                                         meshMax.z * transform.scale.z);
                    
                    if (aabbMin.x > aabbMax.x) std::swap(aabbMin.x, aabbMax.x);
                    if (aabbMin.y > aabbMax.y) std::swap(aabbMin.y, aabbMax.y);
                    if (aabbMin.z > aabbMax.z) std::swap(aabbMin.z, aabbMax.z);
                } else {
                    if (aabbMin.x >= aabbMax.x || aabbMin.y >= aabbMax.y || aabbMin.z >= aabbMax.z) {
                        Vec3 toEntity = transform.position - rayOrigin;
                        Vec3 proj = rayDir * toEntity.dot(rayDir);
                        f32 distToRay = (toEntity - proj).length();
                        
                        if (distToRay < 0.1f) {
                            f32 t = proj.length();
                            if (t >= 0.0f && t < closestT) {
                                closestT = t;
                                closestEntity = entity;
                            }
                        }
                        return;
                    }
                }
            }
        }
        
        f32 t = rayIntersectsAABB(rayOrigin, rayDir, aabbMin, aabbMax);
        
        if (t >= 0.0f && t < closestT) {
            closestT = t;
            closestEntity = entity;
        }
    });
    
    return closestEntity;
}

void SceneViewport::handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;

    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

    int keyAxis = 0;
    if      (ImGui::IsKeyDown(ImGuiKey_X)) keyAxis = 1;
    else if (ImGui::IsKeyDown(ImGuiKey_Y)) keyAxis = 2;
    else if (ImGui::IsKeyDown(ImGuiKey_Z)) keyAxis = 3;

    int axis = keyAxis ? keyAxis : m_hoveredAxis;
    m_gizmoDragAxis = axis;

    auto* pos = world.get<ECS::Transform>(ctx.selectedEntity);
    if (!pos) {
        ECS::Transform initial;
        if (auto* p3 = world.get<ECS::Position3D>(ctx.selectedEntity)) initial.position = p3->position;
        if (auto* s3 = world.get<ECS::Scale3D>(ctx.selectedEntity))    initial.scale = s3->scale;
        if (auto* r3 = world.get<ECS::Rotation3D>(ctx.selectedEntity)) {
            const Vec3 eulerRad = Quat(r3->quaternion.x, r3->quaternion.y, r3->quaternion.z, r3->quaternion.w)
                                      .normalized()
                                      .toEuler();
            initial.rotation = Vec3(eulerRad.x * kRadToDeg, eulerRad.y * kRadToDeg, eulerRad.z * kRadToDeg);
        }
        pos = &world.add<ECS::Transform>(ctx.selectedEntity, initial);
    }

    const float scale = ctx.viewportZoom * 50.0f;

    auto projectDelta = [&](int axIdx) -> float {
        ImVec2 raw = m_axisRawDirs[axIdx - 1];
        float mag2 = raw.x*raw.x + raw.y*raw.y;
        if (mag2 < 0.0001f) return 0.f;
        return (delta.x*raw.x + delta.y*raw.y) / mag2 / scale;
    };

    switch (ctx.gizmoMode) {
        case EditorContext::GizmoMode::Translate:
            if (axis == 1)                           pos->position.x += projectDelta(1);
            else if (axis == 2)                      pos->position.y += projectDelta(2);
            else if (axis == 3)                      pos->position.z += projectDelta(3);
            else {
                pos->position.x += delta.x / scale;
                pos->position.y -= delta.y / scale;
            }
            if (ctx.snapToGrid && ctx.snapGridSize > 0.f) {
                pos->position.x = roundf(pos->position.x / ctx.snapGridSize) * ctx.snapGridSize;
                pos->position.y = roundf(pos->position.y / ctx.snapGridSize) * ctx.snapGridSize;
            }
            break;
        case EditorContext::GizmoMode::Rotate:
            if      (axis == 1) pos->rotation.x += delta.y * 0.01f;
            else if (axis == 2) pos->rotation.y += delta.x * 0.01f;
            else                pos->rotation.z += delta.x * 0.01f;
            break;
        case EditorContext::GizmoMode::Scale:
            if (axis == 1) { pos->scale.x = std::max(0.01f, pos->scale.x * (1.f + projectDelta(1) * 0.5f)); }
            else if (axis == 2) { pos->scale.y = std::max(0.01f, pos->scale.y * (1.f + projectDelta(2) * 0.5f)); }
            else if (axis == 3) { pos->scale.z = std::max(0.01f, pos->scale.z * (1.f + projectDelta(3) * 0.5f)); }
            else {
                pos->scale.x = std::max(0.01f, pos->scale.x * (1.f + delta.x * 0.005f));
                pos->scale.y = std::max(0.01f, pos->scale.y * (1.f + delta.y * 0.005f));
            }
            break;
        case EditorContext::GizmoMode::None: break;
    }

    if (auto* p3 = world.get<ECS::Position3D>(ctx.selectedEntity)) {
        p3->position = pos->position;
    }
    if (auto* s3 = world.get<ECS::Scale3D>(ctx.selectedEntity)) {
        s3->scale = pos->scale;
    }
    if (auto* r3 = world.get<ECS::Rotation3D>(ctx.selectedEntity)) {
        const Quat q = Quat::fromEuler(pos->rotation.x * kDegToRad,
                                       pos->rotation.y * kDegToRad,
                                       pos->rotation.z * kDegToRad).normalized();
        r3->quaternion = Vec4(q.x, q.y, q.z, q.w);
    }

    ctx.isDirty = true;
}

std::string SceneViewport::resolveSpritePath(const std::string& spriteName, const EditorContext& ctx) const {
    if (spriteName.empty()) {
        return "";
    }

    // 1. Try as-is (absolute path or relative to cwd)
    {
        std::filesystem::path path(spriteName);
        if (std::filesystem::exists(path)) {
            return spriteName;
        }
    }

    // 2. Derive project root from current scene path (if available)
    std::filesystem::path projectRoot;
    if (!ctx.currentScenePath.empty()) {
        projectRoot = std::filesystem::path(ctx.currentScenePath).parent_path();
    } else {
        projectRoot = std::filesystem::current_path();
    }

    // Only the filename (without directory)
    std::string filename = std::filesystem::path(spriteName).filename().string();

    // 3. Search in common asset directories relative to project root
    std::vector<std::filesystem::path> searchDirs = {
        projectRoot,
        projectRoot / "assets",
        projectRoot / "assets" / "raw",
        projectRoot / "assets" / "processed",
        projectRoot / "assets" / "textures",
    };

    for (const auto& dir : searchDirs) {
        // Try the full relative path
        auto candidate = dir / spriteName;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        // Try just the filename
        candidate = dir / filename;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }

    // 4. Return as-is and let stbi_load report the error
    return spriteName;
}

void SceneViewport::releaseSpriteTextures() {
    m_spriteTextureCache.clear();
}

void SceneViewport::drawCameraFrustums(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const f32 worldToScreen = ctx.viewportZoom * 50.0f;

    auto w2s = [&](f32 wx, f32 wy) -> ImVec2 {
        return projectToScreen({wx, wy, 0.0f}, origin, viewportSize, ctx);
    };

    ECS::ComponentQuery q;
    q.with<ECS::Camera2DComponent>();
    q.with<ECS::Transform>();

    world.forEach<ECS::Camera2DComponent, ECS::Transform>(q,
        [&](ECS::Entity, ECS::Camera2DComponent& cam, ECS::Transform& pos) {
        const f32 halfW = 8.0f / cam.zoom;
        const f32 halfH = 4.5f / cam.zoom;

        ImVec2 tl = w2s(pos.position.x - halfW, pos.position.y + halfH);
        ImVec2 tr = w2s(pos.position.x + halfW, pos.position.y + halfH);
        ImVec2 br = w2s(pos.position.x + halfW, pos.position.y - halfH);
        ImVec2 bl = w2s(pos.position.x - halfW, pos.position.y - halfH);

        const ImU32 col = IM_COL32(255, 220, 50, 220);
        dl->AddQuad(tl, tr, br, bl, col, 1.5f);

        const f32 cLen = 8.0f;
        dl->AddLine(tl, ImVec2(tl.x + cLen, tl.y), col, 1.5f);
        dl->AddLine(tl, ImVec2(tl.x, tl.y + cLen), col, 1.5f);
        dl->AddLine(tr, ImVec2(tr.x - cLen, tr.y), col, 1.5f);
        dl->AddLine(tr, ImVec2(tr.x, tr.y + cLen), col, 1.5f);
        dl->AddLine(br, ImVec2(br.x - cLen, br.y), col, 1.5f);
        dl->AddLine(br, ImVec2(br.x, br.y - cLen), col, 1.5f);
        dl->AddLine(bl, ImVec2(bl.x + cLen, bl.y), col, 1.5f);
        dl->AddLine(bl, ImVec2(bl.x, bl.y - cLen), col, 1.5f);

        dl->AddText(ImVec2(tl.x + 4.0f, tl.y - 16.0f), col, "Camera");
    });
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
