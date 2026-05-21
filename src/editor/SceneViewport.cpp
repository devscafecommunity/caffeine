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

    if (hovered && ImGui::IsWindowFocused()) {
        bool is3DIso = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                        ctx.viewMode == EditorContext::ViewMode::Isometric);
        if (is3DIso) {
            float speed = ctx.camDistance * 0.04f / ctx.viewportZoom;
            float sinY  = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
                ctx.camFocus.x += -sinY * speed;
                ctx.camFocus.z +=  cosY * speed;
            }
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
                ctx.camFocus.x -= -sinY * speed;
                ctx.camFocus.z -=  cosY * speed;
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                ctx.camFocus.x -= cosY * speed;
                ctx.camFocus.z -= sinY * speed;
            }
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
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
            ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
            ctx.viewportZoom = std::max(0.1f, std::min(10.0f, ctx.viewportZoom));
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
            f32 s = ctx.viewportZoom * 50.0f;
            f32 sinY = std::sin(ctx.camYaw),  cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            f32 rx = p.x - ctx.camFocus.x;
            f32 ry = p.y - ctx.camFocus.y;
            f32 rz = p.z - ctx.camFocus.z;
            f32 vx =  cosY * rx + sinY * rz;
            f32 vy =  ry;
            f32 vz = -sinY * rx + cosY * rz;
            f32 vy2 =  cosP * vy + sinP * vz;
            f32 vz2 = -sinP * vy + cosP * vz;
            f32 dist = std::max(ctx.camDistance, 0.1f);
            f32 fovScale = s * dist / std::max(dist + vz2, 0.01f);
            return ImVec2(cx + vx  * fovScale + ctx.viewportPanX,
                          cy - vy2 * fovScale + ctx.viewportPanY);
        }
        case EditorContext::ViewMode::Isometric: {
            f32 s = ctx.viewportZoom * 50.0f;
            f32 cosA = std::cos(ctx.camYaw + 0.5236f); // 30° offset + azimuth
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
        ImVec2 screenPos = projectToScreen(pos.position, origin, viewportSize, ctx);

        f32 scaleX = std::max(0.1f, pos.scale.x);
        f32 scaleY = std::max(0.1f, pos.scale.y);

        f32 angle = pos.rotation.z;

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
    query.with<ECS::Transform>();
    query.without<ECS::Sprite>();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 7.0f;

    world.forEach<ECS::Transform>(query, [&](ECS::Entity entity, ECS::Transform& pos) {
        ImVec2 sp = projectToScreen(pos.position, origin, viewportSize, ctx);

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
    auto* pos = world.get<ECS::Transform>(ctx.selectedEntity);
    if (!pos) return;

    ImVec2 screenPos = projectToScreen(pos->position, origin, viewportSize, ctx);
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
    ImU32 axisColorX = IM_COL32(200, 80, 80, 120);
    ImU32 axisColorZ = IM_COL32(80, 80, 200, 120);

    float visibleRange = ctx.camDistance / ctx.viewportZoom;
    int halfLines = std::max(30, (int)(visibleRange * 4.0f));

    float spacing = 1.0f;
    while ((float)(halfLines * 2) / spacing > 60.0f) spacing *= 2.0f;
    int step = std::max(1, (int)spacing);

    auto camDepth = [&](Vec3 wp) -> float {
        if (ctx.viewMode != EditorContext::ViewMode::Mode3D) return 1.0f;
        float sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
        float sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
        float rx = wp.x - ctx.camFocus.x;
        float ry = wp.y - ctx.camFocus.y;
        float rz = wp.z - ctx.camFocus.z;
        float vz = -sinY * rx + cosY * rz;
        float vz2 = -sinP * ry + cosP * vz;
        return ctx.camDistance + vz2;
    };

    const float nearClip = 0.5f;

    auto clipLine = [&](Vec3 a, Vec3 b, bool& valid) -> std::pair<Vec3, Vec3> {
        float dA = camDepth(a), dB = camDepth(b);
        if (dA <= nearClip && dB <= nearClip) { valid = false; return {a, b}; }
        valid = true;
        if (dA > nearClip && dB > nearClip) return {a, b};
        float t = (nearClip - dA) / (dB - dA);
        Vec3 clip = {a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), a.z + t*(b.z-a.z)};
        return (dA <= nearClip) ? std::make_pair(clip, b) : std::make_pair(a, clip);
    };

    auto drawLine = [&](Vec3 a, Vec3 b, ImU32 color, float thickness) {
        bool valid;
        auto [ca, cb] = clipLine(a, b, valid);
        if (!valid) return;
        dl->AddLine(projectToScreen(ca, origin, viewportSize, ctx),
                    projectToScreen(cb, origin, viewportSize, ctx), color, thickness);
    };

    if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
        for (int i = -halfLines; i <= halfLines; i += step) {
            drawLine({(f32)i, (f32)(-halfLines), 0.f}, {(f32)i, (f32)(halfLines), 0.f},
                     (i == 0) ? axisColorX : gridColor, (i == 0) ? 1.5f : 0.5f);
            drawLine({(f32)(-halfLines), (f32)i, 0.f}, {(f32)(halfLines), (f32)i, 0.f},
                     (i == 0) ? axisColorZ : gridColor, (i == 0) ? 1.5f : 0.5f);
        }
    } else {
        for (int i = -halfLines; i <= halfLines; i += step) {
            drawLine({(f32)i, 0.f, (f32)(-halfLines)}, {(f32)i, 0.f, (f32)(halfLines)},
                     (i == 0) ? axisColorX : gridColor, (i == 0) ? 1.5f : 0.5f);
            drawLine({(f32)(-halfLines), 0.f, (f32)i}, {(f32)(halfLines), 0.f, (f32)i},
                     (i == 0) ? axisColorZ : gridColor, (i == 0) ? 1.5f : 0.5f);
        }
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
    if (!pos) pos = &world.add<ECS::Transform>(ctx.selectedEntity);

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
