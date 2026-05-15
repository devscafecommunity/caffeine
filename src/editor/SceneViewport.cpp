#include "editor/SceneViewport.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/EditorContext.hpp"
#include <filesystem>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

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
    return m_initialized;
}

void SceneViewport::shutdown() {
    if (!m_initialized || !m_device) return;
    m_device->destroyTexture(m_colorTarget);
    m_device->destroyTexture(m_depthTarget);
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_initialized = false;
}
#endif

// ── Main render entry point ───────────────────────────────────────

void SceneViewport::render(ECS::World& world, EditorContext& ctx
#ifdef CF_HAS_SDL3
                            , Render::Camera2D& editorCamera
#endif
                           ) {
    if (!m_open) return;

#ifdef CF_HAS_SDL3
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) return;

    ImGui::Image((ImTextureID)(intptr_t)m_colorTarget->handle, viewportSize);
#else
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) return;
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
        world.add<ECS::Position2D>(entity, worldX, -worldY);

        if (asset->type == AssetType::Texture) {
            world.add<ECS::Sprite>(entity, assetPath.filename().string(), 0);
        }

        ctx.selectedEntity = entity;
        ctx.endUndo(world);
    }

    bool hovered = ImGui::IsItemHovered();

    if (hovered && ctx.selectedEntity.isValid() && ctx.gizmoMode != EditorContext::GizmoMode::None) {
        bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        if (dragging && !m_gizmoDragging) {
            ctx.beginUndo(EditorCommand::SetField, ctx.selectedEntity.id(), world);
            m_gizmoDragging = true;
        }
        if (m_gizmoDragging) {
            m_Gizmo.onImGuiRender(world, ctx.selectedEntity, ctx);
        }
        if (!dragging && m_gizmoDragging) {
            ctx.endUndo(world);
            m_gizmoDragging = false;
        }
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
        snprintf(buf, sizeof(buf), "Gizmo: %s [W/E/R]  Grid: %s", modeStr, m_config.grid ? "ON" : "OFF");
        drawList->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(200, 200, 200, 200), buf);
    }

    if (ctx.selectedEntity.isValid() && hovered) {
        drawGizmo(world, ctx, origin, viewportSize);
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        ctx.viewportPanX += delta.x;
        ctx.viewportPanY += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    if (hovered) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0) {
            ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
            if (ctx.viewportZoom < 0.1f) ctx.viewportZoom = 0.1f;
            if (ctx.viewportZoom > 10.0f) ctx.viewportZoom = 10.0f;
        }
    }
}

// ── Gizmo drawing ─────────────────────────────────────────────────

void SceneViewport::drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;

    auto* pos = world.get<ECS::Position2D>(ctx.selectedEntity);
    if (!pos) return;

    // Convert world position to screen position
    f32 worldToScreen = ctx.viewportZoom * 50.0f;
    ImVec2 screenPos(
        origin.x + viewportSize.x * 0.5f + (pos->x + ctx.viewportPanX / worldToScreen) * worldToScreen,
        origin.y + viewportSize.y * 0.5f + (pos->y + ctx.viewportPanY / worldToScreen) * worldToScreen
    );

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float handleLen = 30.0f * ctx.viewportZoom;

    switch (ctx.gizmoMode) {
        case EditorContext::GizmoMode::Translate: {
            dl->AddLine(screenPos, ImVec2(screenPos.x + handleLen, screenPos.y),
                        IM_COL32(255, 50, 50, 255), 3.0f);
            dl->AddTriangleFilled(
                ImVec2(screenPos.x + handleLen + 8, screenPos.y),
                ImVec2(screenPos.x + handleLen, screenPos.y - 5),
                ImVec2(screenPos.x + handleLen, screenPos.y + 5),
                IM_COL32(255, 50, 50, 255));
            dl->AddLine(screenPos, ImVec2(screenPos.x, screenPos.y - handleLen),
                        IM_COL32(50, 255, 50, 255), 3.0f);
            dl->AddTriangleFilled(
                ImVec2(screenPos.x, screenPos.y - handleLen - 8),
                ImVec2(screenPos.x - 5, screenPos.y - handleLen),
                ImVec2(screenPos.x + 5, screenPos.y - handleLen),
                IM_COL32(50, 255, 50, 255));
            break;
        }
        case EditorContext::GizmoMode::Rotate: {
            dl->AddCircle(screenPos, handleLen, IM_COL32(255, 200, 50, 255), 32, 2.0f);
            dl->AddLine(screenPos,
                        ImVec2(screenPos.x + handleLen, screenPos.y),
                        IM_COL32(255, 200, 50, 255), 2.0f);
            break;
        }
        case EditorContext::GizmoMode::Scale: {
            dl->AddLine(screenPos, ImVec2(screenPos.x + handleLen, screenPos.y),
                        IM_COL32(100, 200, 255, 255), 2.0f);
            dl->AddRectFilled(
                ImVec2(screenPos.x + handleLen - 5, screenPos.y - 5),
                ImVec2(screenPos.x + handleLen + 5, screenPos.y + 5),
                IM_COL32(100, 200, 255, 255));
            dl->AddLine(screenPos, ImVec2(screenPos.x, screenPos.y - handleLen),
                        IM_COL32(50, 255, 100, 255), 2.0f);
            dl->AddRectFilled(
                ImVec2(screenPos.x - 5, screenPos.y - handleLen - 5),
                ImVec2(screenPos.x + 5, screenPos.y - handleLen + 5),
                IM_COL32(50, 255, 100, 255));
            break;
        }
        case EditorContext::GizmoMode::None:
            dl->AddCircle(screenPos, 6, IM_COL32(255, 255, 255, 180), 12, 2.0f);
            break;
    }
}

// ── Gizmo input handling ──────────────────────────────────────────

void SceneViewport::handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

        f32 sensitivity = 1.0f / (ctx.viewportZoom * 50.0f);

        auto* pos = world.get<ECS::Position2D>(ctx.selectedEntity);
        bool hadPos = (pos != nullptr);

        if (!hadPos) {
            pos = &world.add<ECS::Position2D>(ctx.selectedEntity, 0.0f, 0.0f);
        }

        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate:
                pos->x += delta.x * sensitivity;
                pos->y -= delta.y * sensitivity;
                break;
            case EditorContext::GizmoMode::Rotate: {
                auto& rot = world.add<ECS::Rotation>(ctx.selectedEntity);
                rot.angle += delta.x * 0.01f;
                break;
            }
            case EditorContext::GizmoMode::Scale: {
                auto& scl = world.add<ECS::Scale2D>(ctx.selectedEntity, 1.0f, 1.0f);
                scl.x *= 1.0f + delta.x * 0.005f;
                scl.y *= 1.0f + delta.y * 0.005f;
                if (scl.x < 0.01f) scl.x = 0.01f;
                if (scl.y < 0.01f) scl.y = 0.01f;
                break;
            }
            case EditorContext::GizmoMode::None: break;
        }

        ctx.isDirty = true;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
