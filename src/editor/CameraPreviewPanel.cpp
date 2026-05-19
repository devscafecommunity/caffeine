#include "editor/CameraPreviewPanel.hpp"

#ifdef CF_HAS_IMGUI

#include "ecs/Components.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"

#include <stb/stb_image.h>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace Caffeine::Editor {

void CameraPreviewPanel::onImGuiRender(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camera Preview", &m_open)) {
        ImGui::End();
        return;
    }

    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    if (panelSize.x < 4.0f) panelSize.x = 4.0f;
    if (panelSize.y < 4.0f) panelSize.y = 4.0f;

    ECS::Entity cameraEntity;
    float camX = 0.0f, camY = 0.0f, zoom = 1.0f;
    bool found = false;

    {
        ECS::ComponentQuery q;
        q.with<ECS::Camera2DComponent>();
        q.with<ECS::Position2D>();
        world.forEach<ECS::Camera2DComponent, ECS::Position2D>(q,
            [&](ECS::Entity e, ECS::Camera2DComponent& cam, ECS::Position2D& pos) {
                if (!found) {
                    cameraEntity = e;
                    camX  = pos.x;
                    camY  = pos.y;
                    zoom  = cam.zoom;
                    found = true;
                }
            });
    }

    if (!found) {
        ECS::ComponentQuery q;
        q.with<ECS::Camera2DComponent>();
        world.forEach<ECS::Camera2DComponent>(q,
            [&](ECS::Entity e, ECS::Camera2DComponent& cam) {
                if (!found) {
                    cameraEntity = e;
                    zoom  = cam.zoom;
                    found = true;
                }
            });
    }

    ImVec2 origin = ImGui::GetCursorScreenPos();

    if (!found) {
        renderNoCamera(panelSize);
    } else {
        ImGui::InvisibleButton("##campreview", panelSize);
        renderCameraView(world, ctx, origin, panelSize, camX, camY, zoom);
    }

    ImGui::End();
}

void CameraPreviewPanel::renderNoCamera(ImVec2 panelSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                      IM_COL32(20, 20, 24, 255));

    const char* msg = "No Cameras detected";
    ImVec2 textSize = ImGui::CalcTextSize(msg);
    ImVec2 textPos  = ImVec2(origin.x + (panelSize.x - textSize.x) * 0.5f,
                              origin.y + (panelSize.y - textSize.y) * 0.5f);
    dl->AddText(textPos, IM_COL32(120, 120, 130, 200), msg);

    ImGui::InvisibleButton("##camempty", panelSize);
}

void CameraPreviewPanel::renderCameraView(ECS::World& world, EditorContext& ctx,
                                          ImVec2 origin, ImVec2 panelSize,
                                          float camX, float camY, float zoom) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                      IM_COL32(15, 15, 18, 255));

    dl->PushClipRect(origin,
                     ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                     true);

    const float worldToScreen = zoom * 50.0f;
    const float cx = origin.x + panelSize.x * 0.5f;
    const float cy = origin.y + panelSize.y * 0.5f;

    auto w2s = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(cx + (wx - camX) * worldToScreen,
                      cy - (wy - camY) * worldToScreen);
    };

    ECS::ComponentQuery spriteQ;
    spriteQ.with<ECS::Position2D>();
    spriteQ.with<ECS::Sprite>();

    world.forEach<ECS::Position2D, ECS::Sprite>(spriteQ,
        [&](ECS::Entity, ECS::Position2D& pos, ECS::Sprite& sprite) {
            float scaleX = 1.0f, scaleY = 1.0f;
            ImVec2 screenPos = w2s(pos.x, pos.y);

            float halfW = std::max(8.0f, 0.5f * worldToScreen * scaleX);
            float halfH = std::max(8.0f, 0.5f * worldToScreen * scaleY);

            bool hasTexture = false;
            ImTextureRef texRef;

            if (!sprite.name.empty()) {
                const std::string path = resolveSpritePath(sprite.name, ctx);
                if (!path.empty()) {
                    auto it = m_texCache.find(path);
                    if (it == m_texCache.end()) {
                        int w = 0, h = 0, ch = 0;
                        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
                        TexEntry entry;
                        if (px && w > 0 && h > 0) {
                            entry.width  = w;
                            entry.height = h;
                            entry.texture = std::make_unique<ImTextureData>();
                            entry.texture->Create(ImTextureFormat_RGBA32, w, h);
                            std::memcpy(entry.texture->GetPixels(), px,
                                        static_cast<size_t>(w * h * 4));
                            entry.texture->SetStatus(ImTextureStatus_WantCreate);
                            ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
                        } else {
                            entry.loadFailed = true;
                        }
                        if (px) stbi_image_free(px);
                        auto [newIt, ok] = m_texCache.emplace(path, std::move(entry));
                        it = newIt;
                    }
                    if (!it->second.loadFailed && it->second.texture) {
                        if (it->second.texture->Status == ImTextureStatus_WantCreate)
                            ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
                        ImTextureID tid = it->second.texture->GetTexID();
                        hasTexture = (tid != ImTextureID_Invalid);
                        if (hasTexture) {
                            texRef = it->second.texture->GetTexRef();
                            const float aspect = static_cast<float>(it->second.width) /
                                                 static_cast<float>(it->second.height);
                            if (aspect > 1.0f) halfH = std::max(8.0f, halfW / aspect);
                            else               halfW = std::max(8.0f, halfH * aspect);
                        }
                    }
                }
            }

            const ImVec2 p1 = ImVec2(screenPos.x - halfW, screenPos.y - halfH);
            const ImVec2 p2 = ImVec2(screenPos.x + halfW, screenPos.y - halfH);
            const ImVec2 p3 = ImVec2(screenPos.x + halfW, screenPos.y + halfH);
            const ImVec2 p4 = ImVec2(screenPos.x - halfW, screenPos.y + halfH);

            if (hasTexture) {
                dl->AddImageQuad(texRef, p1, p2, p3, p4);
            } else {
                dl->AddRectFilled(p1, p3, IM_COL32(64, 64, 64, 200));
            }
        });

    dl->PopClipRect();

    const float borderAlpha = 160;
    dl->AddRect(origin,
                ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                IM_COL32(80, 140, 255, borderAlpha), 0.0f, 0, 1.5f);
}

std::string CameraPreviewPanel::resolveSpritePath(const std::string& name,
                                                   const EditorContext& ctx) const {
    if (name.empty()) return {};
    std::filesystem::path p(name);
    if (std::filesystem::exists(p)) return name;

    std::filesystem::path root;
    if (!ctx.currentScenePath.empty())
        root = std::filesystem::path(ctx.currentScenePath).parent_path();
    else
        root = std::filesystem::current_path();

    auto candidate = root / name;
    if (std::filesystem::exists(candidate)) return candidate.string();

    for (auto& sub : {"assets", "sprites", "textures"}) {
        auto sp = root / sub / name;
        if (std::filesystem::exists(sp)) return sp.string();
    }
    return {};
}

}

#endif
