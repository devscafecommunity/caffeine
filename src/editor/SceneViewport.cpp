#include "editor/SceneViewport.hpp"
#include "editor/DragDropSystem.hpp"
#include "editor/EditorContext.hpp"
#include "audio/AudioComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <stb/stb_image.h>
#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif

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
    if (!m_open) return;

    if (!ImGui::Begin("Scene Viewport", &m_open)) {
        ImGui::End();
        return;
    }

#ifdef CF_HAS_SDL3
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) {
        ImGui::End();
        return;
    }

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
        world.add<ECS::Position2D>(entity, worldX, -worldY);

        if (asset->type == AssetType::Texture) {
            world.add<ECS::Sprite>(entity, asset->path, 0);
        }

        if (asset->type == AssetType::Audio) {
            auto& emitter = world.add<Audio::AudioEmitter>(entity);
            emitter.clipPath = assetPath.filename().string().c_str();
        }

        ctx.selectedEntity = entity;
        ctx.endUndo(world);
    }

    bool hovered = ImGui::IsItemHovered();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) ctx.gizmoMode = EditorContext::GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) ctx.gizmoMode = EditorContext::GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) ctx.gizmoMode = EditorContext::GizmoMode::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) ctx.gizmoMode = EditorContext::GizmoMode::None;
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
        snprintf(buf, sizeof(buf), "Gizmo: %s [W/E/R]  Grid: %s", modeStr, m_config.grid ? "ON" : "OFF");
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
        ImGui::PopStyleVar();
    }

    drawGrid(drawList, origin, viewportSize, ctx);
    drawSprites(world, ctx, origin, viewportSize);
    drawEmptyEntities(world, ctx, origin, viewportSize);
    drawPhysicsDebug(world, ctx, origin, viewportSize);

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

    if (hovered) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0) {
            ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
            if (ctx.viewportZoom < 0.1f) ctx.viewportZoom = 0.1f;
            if (ctx.viewportZoom > 10.0f) ctx.viewportZoom = 10.0f;
        }
    }

    ImGui::End();
}

// ── Gizmo drawing ─────────────────────────────────────────────────

void SceneViewport::drawSprites(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ECS::ComponentQuery query;
    query.with<ECS::Position2D>();
    query.with<ECS::Sprite>();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;
    const f32 minHalfSize = 8.0f;

    world.forEach<ECS::Position2D, ECS::Sprite>(query, [&](ECS::Entity entity, ECS::Position2D& pos, ECS::Sprite& sprite) {
        ImVec2 screenPos(
            origin.x + viewportSize.x * 0.5f + (pos.x + ctx.viewportPanX / worldToScreen) * worldToScreen,
            origin.y + viewportSize.y * 0.5f + (-pos.y + ctx.viewportPanY / worldToScreen) * worldToScreen
        );

        f32 scaleX = 1.0f;
        f32 scaleY = 1.0f;
        if (auto* scale = world.get<ECS::Scale2D>(entity)) {
            scaleX = std::max(0.1f, scale->x);
            scaleY = std::max(0.1f, scale->y);
        }

        f32 angle = 0.0f;
        if (auto* rot = world.get<ECS::Rotation>(entity)) {
            angle = rot->angle;
        }

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
    ECS::ComponentQuery query;
    query.with<ECS::Position2D>();
    query.without<ECS::Sprite>();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 7.0f;

    world.forEach<ECS::Position2D>(query, [&](ECS::Entity entity, ECS::Position2D& pos) {
        ImVec2 sp(
            origin.x + viewportSize.x * 0.5f + (pos.x + ctx.viewportPanX / worldToScreen) * worldToScreen,
            origin.y + viewportSize.y * 0.5f + (-pos.y + ctx.viewportPanY / worldToScreen) * worldToScreen
        );

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
    });
}

void SceneViewport::drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;

    auto* pos = world.get<ECS::Position2D>(ctx.selectedEntity);
    if (!pos) return;

    f32 worldToScreen = ctx.viewportZoom * 50.0f;
    ImVec2 screenPos(
        origin.x + viewportSize.x * 0.5f + (pos->x + ctx.viewportPanX / worldToScreen) * worldToScreen,
        origin.y + viewportSize.y * 0.5f + (-pos->y + ctx.viewportPanY / worldToScreen) * worldToScreen
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

    if (world.has<Audio::AudioEmitter>(ctx.selectedEntity)) {
        auto* emitter = world.get<Audio::AudioEmitter>(ctx.selectedEntity);
        if (emitter->spatial && emitter->maxDistance > 0.0f) {
            f32 w2s = ctx.viewportZoom * 50.0f;
            f32 fullVolumeRadius = emitter->maxDistance * 0.5f;
            char buf[64];
            dl->AddCircle(screenPos, fullVolumeRadius * w2s, IM_COL32(0, 255, 255, 80), 48, 2.0f);
            snprintf(buf, sizeof(buf), "near %.0f", fullVolumeRadius);
            dl->AddText(ImVec2(screenPos.x + fullVolumeRadius * w2s + 4, screenPos.y - 8),
                        IM_COL32(0, 255, 255, 180), buf);
            dl->AddCircle(screenPos, emitter->maxDistance * w2s, IM_COL32(50, 130, 255, 60), 64, 2.0f);
            snprintf(buf, sizeof(buf), "max %.0f", emitter->maxDistance);
            dl->AddText(ImVec2(screenPos.x + emitter->maxDistance * w2s + 4, screenPos.y - 8),
                        IM_COL32(50, 130, 255, 180), buf);
            dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 20),
                        IM_COL32(180, 180, 255, 220), "S");
        }
    }
}

void SceneViewport::drawPhysicsDebug(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.physicsDebugVisible) return;
    using namespace Physics2D;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;

    auto worldToScreen_fn = [&](f32 wx, f32 wy) -> ImVec2 {
        return ImVec2(
            origin.x + viewportSize.x * 0.5f + (wx + ctx.viewportPanX / worldToScreen) * worldToScreen,
            origin.y + viewportSize.y * 0.5f + (-wy + ctx.viewportPanY / worldToScreen) * worldToScreen
        );
    };

    ECS::ComponentQuery q;
    q.with<Collider2D>();
    q.with<ECS::Position2D>();

    world.forEach<Collider2D, ECS::Position2D>(q,
        [&](ECS::Entity entity, Collider2D& col, ECS::Position2D& pos) {
            f32 cx = pos.x + col.offset.x;
            f32 cy = pos.y + col.offset.y;
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

    bool is3D = false;
    if (ctx.selectedEntity.isValid()) {
        is3D = world.has<ECS::Position3D>(ctx.selectedEntity);
    }

    ImVec2 center(widgetMin.x + widgetSize * 0.5f, widgetMin.y + widgetSize * 0.5f);
    const float axisLen = 22.0f;

    dl->AddLine(center, ImVec2(center.x + axisLen, center.y), IM_COL32(255, 70, 70, 255), 2.0f);
    dl->AddText(ImVec2(center.x + axisLen + 3.0f, center.y - 8.0f), IM_COL32(255, 90, 90, 255), "X");

    dl->AddLine(center, ImVec2(center.x, center.y - axisLen), IM_COL32(70, 255, 90, 255), 2.0f);
    dl->AddText(ImVec2(center.x - 4.0f, center.y - axisLen - 14.0f), IM_COL32(90, 255, 110, 255), "Y");

    if (is3D) {
        dl->AddLine(center, ImVec2(center.x - axisLen * 0.70f, center.y + axisLen * 0.70f), IM_COL32(90, 140, 255, 255), 2.0f);
        dl->AddText(ImVec2(center.x - axisLen * 0.70f - 12.0f, center.y + axisLen * 0.70f - 6.0f), IM_COL32(120, 165, 255, 255), "Z");
    }

    dl->AddCircleFilled(center, 2.8f, IM_COL32(240, 240, 240, 255));
    dl->AddText(ImVec2(widgetMin.x + 6.0f, widgetMin.y + widgetSize - 18.0f), IM_COL32(220, 220, 230, 220), is3D ? "3D" : "2D");

    ImVec2 btnPos(widgetMax.x + 6.0f, widgetMin.y + widgetSize - 24.0f);
    ImGui::SetCursorScreenPos(btnPos);
    if (ImGui::Button(m_projectionMode == ProjectionMode::Perspective ? "Persp" : "Ortho", ImVec2(buttonWidth, 24.0f))) {
        toggleProjectionMode();
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
                if (ctx.snapToGrid && ctx.snapGridSize > 0.0f) {
                    pos->x = roundf(pos->x / ctx.snapGridSize) * ctx.snapGridSize;
                    pos->y = roundf(pos->y / ctx.snapGridSize) * ctx.snapGridSize;
                }
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

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
