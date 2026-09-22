#pragma once

#include "editor/EditorContext.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/PlayMode2D.hpp"
#include "scene/SceneComponents.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#include <stb/stb_image.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

struct Camera2DPreviewTextureEntry {
    std::unique_ptr<ImTextureData> texture;
    int width = 0;
    int height = 0;
    bool loadFailed = false;
};

inline std::string resolveSpritePathForPreview(const std::string& name, const EditorContext& ctx) {
    if (name.empty()) return {};
    const std::filesystem::path sceneDir =
        ctx.currentScenePath.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(ctx.currentScenePath).parent_path();
    const std::filesystem::path projectRoot = sceneDir.parent_path();
    const std::array<std::filesystem::path, 4> candidates = {
        projectRoot / "assets" / "sprites" / name,
        projectRoot / "assets" / name,
        sceneDir / name,
        std::filesystem::path(name),
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) return path.string();
    }
    return {};
}

inline ECS::Entity findPreviewCamera2D(ECS::World& world, const EditorContext& ctx) {
    if (ctx.selectedEntity.isValid() && world.has<ECS::Camera2DComponent>(ctx.selectedEntity)) {
        return ECS::Entity(ctx.selectedEntity.id(), &world);
    }
    ECS::Entity active = Scene::findActiveCamera2DEntity(world);
    if (active.isValid()) return active;

    ECS::ComponentQuery q;
    q.with<ECS::Camera2DComponent>();
    ECS::Entity fallback;
    world.forEach<ECS::Camera2DComponent>(q, [&](ECS::Entity e, ECS::Camera2DComponent&) {
        if (!fallback.isValid()) fallback = e;
    });
    return fallback;
}

inline void drawCamera2DPreviewGrid(ImDrawList* dl, ImVec2 origin, ImVec2 panelSize, float camX,
                                    float camY, float zoom) {
    const float worldToScreen = zoom * 50.0f;
    const float cx = origin.x + panelSize.x * 0.5f;
    const float cy = origin.y + panelSize.y * 0.5f;
    auto w2s = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(cx + (wx - camX) * worldToScreen, cy - (wy - camY) * worldToScreen);
    };

    const ImU32 gridCol = IM_COL32(50, 50, 58, 180);
    const float step = 50.0f;
    const float halfW = (panelSize.x * 0.5f) / std::max(worldToScreen, 1.0f);
    const float halfH = (panelSize.y * 0.5f) / std::max(worldToScreen, 1.0f);
    const int minX = static_cast<int>(std::floor((camX - halfW) / step));
    const int maxX = static_cast<int>(std::ceil((camX + halfW) / step));
    const int minY = static_cast<int>(std::floor((camY - halfH) / step));
    const int maxY = static_cast<int>(std::ceil((camY + halfH) / step));
    for (int gx = minX; gx <= maxX; ++gx) {
        ImVec2 a = w2s(gx * step, camY - halfH);
        ImVec2 b = w2s(gx * step, camY + halfH);
        dl->AddLine(a, b, gridCol, 1.0f);
    }
    for (int gy = minY; gy <= maxY; ++gy) {
        ImVec2 a = w2s(camX - halfW, gy * step);
        ImVec2 b = w2s(camX + halfW, gy * step);
        dl->AddLine(a, b, gridCol, 1.0f);
    }

    const ImVec2 center = w2s(camX, camY);
    dl->AddLine(ImVec2(center.x - 10.0f, center.y), ImVec2(center.x + 10.0f, center.y),
                IM_COL32(255, 220, 80, 200), 1.5f);
    dl->AddLine(ImVec2(center.x, center.y - 10.0f), ImVec2(center.x, center.y + 10.0f),
                IM_COL32(255, 220, 80, 200), 1.5f);
}

inline int renderCamera2DPreview(ECS::World& world, const EditorContext& ctx, ImDrawList* dl,
                                 ImVec2 origin, ImVec2 panelSize, float camX, float camY,
                                 float zoom,
                                 std::unordered_map<std::string, Camera2DPreviewTextureEntry>& texCache) {
    dl->AddRectFilled(origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                      IM_COL32(15, 15, 18, 255));
    dl->PushClipRect(origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y), true);
    drawCamera2DPreviewGrid(dl, origin, panelSize, camX, camY, zoom);

    const float worldToScreen = zoom * 50.0f;
    const float cx = origin.x + panelSize.x * 0.5f;
    const float cy = origin.y + panelSize.y * 0.5f;
    auto w2s = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(cx + (wx - camX) * worldToScreen, cy - (wy - camY) * worldToScreen);
    };

    int spriteCount = 0;
    ECS::ComponentQuery spriteQ;
    spriteQ.with<ECS::Transform>();
    spriteQ.with<ECS::Sprite>();
    world.forEach<ECS::Transform, ECS::Sprite>(spriteQ,
        [&](ECS::Entity entity, ECS::Transform& pos, ECS::Sprite& sprite) {
            if (Scene::isEffectivelyDisabled(world, entity)) return;
            ++spriteCount;

            Vec3 worldPosition = pos.position;
            float scaleX = std::max(0.1f, pos.scale.x);
            float scaleY = std::max(0.1f, pos.scale.y);
            if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
                worldPosition = Vec3(wt->matrix(0, 3), wt->matrix(1, 3), wt->matrix(2, 3));
                scaleX = std::max(0.1f, std::sqrt(wt->matrix(0, 0) * wt->matrix(0, 0) +
                                                  wt->matrix(1, 0) * wt->matrix(1, 0)));
                scaleY = std::max(0.1f, std::sqrt(wt->matrix(0, 1) * wt->matrix(0, 1) +
                                                  wt->matrix(1, 1) * wt->matrix(1, 1)));
            }

            ImVec2 screenPos = w2s(worldPosition.x, worldPosition.y);
            float halfW = std::max(8.0f, 0.5f * worldToScreen * scaleX);
            float halfH = std::max(8.0f, 0.5f * worldToScreen * scaleY);

            bool hasTexture = false;
            ImTextureRef texRef;
            if (!sprite.name.empty()) {
                const std::string path = resolveSpritePathForPreview(sprite.name, ctx);
                if (!path.empty()) {
                    auto it = texCache.find(path);
                    if (it == texCache.end()) {
                        int w = 0, h = 0, ch = 0;
                        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
                        Camera2DPreviewTextureEntry entry;
                        if (px && w > 0 && h > 0) {
                            entry.width = w;
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
                        auto [newIt, ok] = texCache.emplace(path, std::move(entry));
                        it = newIt;
                    }
                    if (!it->second.loadFailed && it->second.texture) {
                        if (it->second.texture->Status == ImTextureStatus_WantCreate) {
                            ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
                        }
                        if (it->second.texture->GetTexID() != ImTextureID_Invalid) {
                            hasTexture = true;
                            texRef = it->second.texture->GetTexRef();
                            const float aspect =
                                static_cast<float>(it->second.width) /
                                static_cast<float>(std::max(it->second.height, 1));
                            if (aspect > 1.0f) halfH = std::max(8.0f, halfW / aspect);
                            else halfW = std::max(8.0f, halfH * aspect);
                        }
                    }
                }
            }

            const ImVec2 p1(screenPos.x - halfW, screenPos.y - halfH);
            const ImVec2 p3(screenPos.x + halfW, screenPos.y + halfH);
            if (hasTexture) {
                dl->AddImage(texRef, p1, p3);
            } else {
                dl->AddRectFilled(p1, p3, IM_COL32(90, 140, 220, 200));
                dl->AddRect(p1, p3, IM_COL32(180, 210, 255, 220), 0.0f, 0, 1.5f);
            }
        });

    dl->PopClipRect();
    return spriteCount;
}

#endif

}  // namespace Caffeine::Editor
