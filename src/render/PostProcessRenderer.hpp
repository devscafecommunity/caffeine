#pragma once

#include "ecs/PostProcessComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"

#include <algorithm>
#include <cmath>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Render {

#ifdef CF_HAS_IMGUI

inline void applyPostProcessOverlay(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                                    const ECS::PostProcessComponent& fx) {
    if (!dl || !fx.enabled || size.x < 1.0f || size.y < 1.0f) return;

    const ImVec2 p0 = origin;
    const ImVec2 p1(origin.x + size.x, origin.y + size.y);

    if (fx.exposure != 1.0f || fx.contrast != 1.0f || fx.saturation != 1.0f) {
        const f32 tint = std::clamp(fx.exposure, 0.2f, 3.0f);
        const u8 alpha = static_cast<u8>(std::clamp((tint - 1.0f) * 40.0f + 8.0f, 0.0f, 48.0f));
        if (tint > 1.0f) {
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 240, alpha));
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 20, alpha));
        }
    }

    if (fx.vignette > 0.001f) {
        const f32 v = std::clamp(fx.vignette, 0.0f, 1.0f);
        const u8 edge = static_cast<u8>(v * 180.0f);
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, edge), IM_COL32(0, 0, 0, edge),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, edge), IM_COL32(0, 0, 0, edge));
    }

    if (fx.bloom > 0.001f) {
        const u8 bloomA = static_cast<u8>(std::clamp(fx.bloom, 0.0f, 1.0f) * 50.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(255, 245, 220, bloomA));
    }

    if (fx.chromaticAberration > 0.001f) {
        const f32 ca = std::clamp(fx.chromaticAberration, 0.0f, 1.0f);
        const f32 offset = ca * 3.0f;
        dl->AddRect(ImVec2(p0.x - offset, p0.y), ImVec2(p1.x - offset, p1.y),
                    IM_COL32(255, 0, 0, 40), 0.0f, 0, 2.0f);
        dl->AddRect(ImVec2(p0.x + offset, p0.y), ImVec2(p1.x + offset, p1.y),
                    IM_COL32(0, 0, 255, 40), 0.0f, 0, 2.0f);
    }

    if (fx.filmGrain > 0.001f) {
        const u8 grain = static_cast<u8>(std::clamp(fx.filmGrain, 0.0f, 1.0f) * 28.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(128, 128, 128, grain));
    }
}

inline const ECS::PostProcessComponent* findPostProcessForCamera(ECS::World& world,
                                                               ECS::Entity cameraEntity) {
    if (cameraEntity.isValid()) {
        if (auto* fx = world.get<ECS::PostProcessComponent>(cameraEntity)) return fx;
    }
    ECS::ComponentQuery q;
    q.with<ECS::PostProcessComponent>();
    const ECS::PostProcessComponent* fallback = nullptr;
    world.forEach<ECS::PostProcessComponent>(q, [&](ECS::Entity, ECS::PostProcessComponent& fx) {
        if (!fallback) fallback = &fx;
    });
    return fallback;
}

#endif

}  // namespace Caffeine::Render
