#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "ecs/Entity.hpp"
#include "scene/SceneComponents.hpp"
#include "render/Camera2D.hpp"
#include "math/Math.hpp"
#include "editor/EditorContext.hpp"
#include "editor/TransformGizmo.hpp"

#include <cmath>

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class SceneViewport {
public:
#ifdef CF_HAS_SDL3
    struct Config {
        u32  width       = 1280;
        u32  height      = 720;
        bool grid        = true;
        f32  gridSpacing = 64.0f;
        f32  gridWidth   = 2000.0f;
    };
#endif

    SceneViewport() = default;

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Config cfg = {}) {
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

    void shutdown() {
        if (!m_initialized || !m_device) return;
        m_device->destroyTexture(m_colorTarget);
        m_device->destroyTexture(m_depthTarget);
        m_colorTarget = nullptr;
        m_depthTarget = nullptr;
        m_initialized = false;
    }
#endif

    void render(ECS::World& world, EditorContext& ctx
#ifdef CF_HAS_SDL3
                , Render::Camera2D& editorCamera
#endif
               ) {
#ifdef CF_HAS_IMGUI
        if (!m_open) return;

#ifdef CF_HAS_SDL3
        // Use ImGui::Image to display the offscreen framebuffer
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x < 1 || viewportSize.y < 1) return;

        // Show the framebuffer texture as an ImGui image
        // SDL_GPUTexture* is passed as ImTextureID
        ImGui::Image((ImTextureID)(intptr_t)m_colorTarget->handle, viewportSize);
#else
        // Without SDL3, show a placeholder
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x < 1 || viewportSize.y < 1) return;
        ImGui::Dummy(viewportSize);
#endif

        bool hovered = ImGui::IsItemHovered();
        bool focused = ImGui::IsWindowFocused();

        if (hovered) {
            // Handle mouse drag for selected entity gizmo
            if (ctx.selectedEntity.isValid() && ctx.gizmoMode != EditorContext::GizmoMode::None) {
                m_Gizmo.onImGuiRender(world, ctx.selectedEntity, ctx);
            }
        }

        // Draw gizmo overlay and grid info
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetItemRectMin();

        // Grid overlay text
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

        // Draw gizmo handles for selected entity
        if (ctx.selectedEntity.isValid() && hovered) {
            drawGizmo(world, ctx, origin, viewportSize);
        }

        // Handle viewport pan (middle mouse)
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            ctx.viewportPanX += delta.x;
            ctx.viewportPanY += delta.y;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }

        // Handle viewport zoom (scroll)
        if (hovered) {
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0) {
                ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
                if (ctx.viewportZoom < 0.1f) ctx.viewportZoom = 0.1f;
                if (ctx.viewportZoom > 10.0f) ctx.viewportZoom = 10.0f;
            }
        }

#endif
    }

#ifdef CF_HAS_SDL3
    RHI::Texture* colorTarget() const { return m_colorTarget; }
#endif

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
        if (!ctx.selectedEntity.isValid()) return;

        auto* pos = world.get<ECS::Position2D>(ctx.selectedEntity);
        if (!pos) return;

        // Convert world position to screen position
        f32 worldToScreen = ctx.viewportZoom * 50.0f; // pixels per unit
        ImVec2 screenPos(
            origin.x + viewportSize.x * 0.5f + (pos->x + ctx.viewportPanX / worldToScreen) * worldToScreen,
            origin.y + viewportSize.y * 0.5f + (pos->y + ctx.viewportPanY / worldToScreen) * worldToScreen
        );

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float handleLen = 30.0f * ctx.viewportZoom;

        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate: {
                // X axis (red)
                dl->AddLine(screenPos, ImVec2(screenPos.x + handleLen, screenPos.y),
                            IM_COL32(255, 50, 50, 255), 3.0f);
                dl->AddTriangleFilled(
                    ImVec2(screenPos.x + handleLen + 8, screenPos.y),
                    ImVec2(screenPos.x + handleLen, screenPos.y - 5),
                    ImVec2(screenPos.x + handleLen, screenPos.y + 5),
                    IM_COL32(255, 50, 50, 255));
                // Y axis (green)
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
                // Circle with angle indicator
                dl->AddCircle(screenPos, handleLen, IM_COL32(255, 200, 50, 255), 32, 2.0f);
                dl->AddLine(screenPos,
                            ImVec2(screenPos.x + handleLen, screenPos.y),
                            IM_COL32(255, 200, 50, 255), 2.0f);
                break;
            }
            case EditorContext::GizmoMode::Scale: {
                // X axis (red box)
                dl->AddLine(screenPos, ImVec2(screenPos.x + handleLen, screenPos.y),
                            IM_COL32(100, 200, 255, 255), 2.0f);
                dl->AddRectFilled(
                    ImVec2(screenPos.x + handleLen - 5, screenPos.y - 5),
                    ImVec2(screenPos.x + handleLen + 5, screenPos.y + 5),
                    IM_COL32(100, 200, 255, 255));
                // Y axis (green box)
                dl->AddLine(screenPos, ImVec2(screenPos.x, screenPos.y - handleLen),
                            IM_COL32(50, 255, 100, 255), 2.0f);
                dl->AddRectFilled(
                    ImVec2(screenPos.x - 5, screenPos.y - handleLen - 5),
                    ImVec2(screenPos.x + 5, screenPos.y - handleLen + 5),
                    IM_COL32(50, 255, 100, 255));
                break;
            }
            case EditorContext::GizmoMode::None:
                // Selection indicator - small circle
                dl->AddCircle(screenPos, 6, IM_COL32(255, 255, 255, 180), 12, 2.0f);
                break;
        }
    }

    void handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize) {
        if (!ctx.selectedEntity.isValid()) return;

        // Simple drag handling for entity manipulation
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
                    pos->y -= delta.y * sensitivity; // Y is inverted in screen space
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
#endif

    bool m_open = true;
    bool m_initialized = false;
    TransformGizmo m_Gizmo;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    Config m_config;
#endif
};

} // namespace Caffeine::Editor
