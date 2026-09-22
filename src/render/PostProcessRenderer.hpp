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

    f32 exposure = fx.colorGrading.exposure;
    if (fx.autoExposure.enabled) {
        exposure = std::clamp(exposure + fx.autoExposure.compensation, fx.autoExposure.minExposure,
                              fx.autoExposure.maxExposure);
    }

    if (fx.colorGrading.enabled) {
        const f32 tint = std::clamp(exposure, 0.2f, 4.0f);
        const f32 contrastDelta = std::abs(fx.colorGrading.contrast - 1.0f);
        const f32 satDelta = std::abs(fx.colorGrading.saturation - 1.0f);
        const u8 alpha = static_cast<u8>(
            std::clamp((tint - 1.0f) * 40.0f + contrastDelta * 30.0f + satDelta * 24.0f + 10.0f, 8.0f,
                       56.0f));
        if (tint >= 1.0f) {
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 240, alpha));
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 20, alpha));
        }
        if (fx.colorGrading.temperature < -0.05f) {
            dl->AddRectFilled(p0, p1, IM_COL32(0, 20, 40, 20));
        } else if (fx.colorGrading.temperature > 0.05f) {
            dl->AddRectFilled(p0, p1, IM_COL32(40, 20, 0, 20));
        }
    }

    if (fx.deferredFog.enabled && fx.deferredFog.density > 0.001f) {
        const u8 fogA = static_cast<u8>(std::clamp(fx.deferredFog.density * 400.0f, 0.0f, 120.0f));
        const u8 r = static_cast<u8>(fx.deferredFog.colorR * 255.0f);
        const u8 g = static_cast<u8>(fx.deferredFog.colorG * 255.0f);
        const u8 b = static_cast<u8>(fx.deferredFog.colorB * 255.0f);
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(r, g, b, 0), IM_COL32(r, g, b, 0),
                                    IM_COL32(r, g, b, fogA), IM_COL32(r, g, b, fogA));
    }

    if (fx.ambientOcclusion.enabled && fx.ambientOcclusion.intensity > 0.001f) {
        const u8 ao = static_cast<u8>(
            std::clamp(fx.ambientOcclusion.intensity * 35.0f, 0.0f, 80.0f));
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, ao / 2));
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, ao), IM_COL32(0, 0, 0, ao),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    }

    if (fx.screenSpaceReflections.enabled && fx.screenSpaceReflections.intensity > 0.001f) {
        const u8 ssr = static_cast<u8>(
            std::clamp(fx.screenSpaceReflections.intensity * 40.0f, 0.0f, 60.0f));
        const f32 h = size.y * 0.35f;
        dl->AddRectFilled(ImVec2(p0.x, p1.y - h), p1, IM_COL32(200, 220, 255, ssr));
    }

    if (fx.depthOfField.enabled && fx.depthOfField.maxBlur > 0.001f) {
        const f32 blur = std::clamp(fx.depthOfField.maxBlur, 0.0f, 2.0f);
        const u8 edge = static_cast<u8>(blur * 60.0f);
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, edge), IM_COL32(0, 0, 0, edge),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    }

    if (fx.vignette.enabled && fx.vignette.intensity > 0.001f) {
        const f32 v = std::clamp(fx.vignette.intensity, 0.0f, 1.0f);
        const u8 edge = static_cast<u8>(v * 180.0f);
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, edge), IM_COL32(0, 0, 0, edge),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, edge), IM_COL32(0, 0, 0, edge));
    }

    if (fx.bloom.enabled && fx.bloom.intensity > 0.001f) {
        const u8 bloomA = static_cast<u8>(std::clamp(fx.bloom.intensity, 0.0f, 1.0f) * 50.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(255, 245, 220, bloomA));
    }

    if (fx.chromaticAberration.enabled && fx.chromaticAberration.intensity > 0.001f) {
        const f32 ca = std::clamp(fx.chromaticAberration.intensity, 0.0f, 1.0f);
        const f32 offset = ca * 3.0f;
        dl->AddRect(ImVec2(p0.x - offset, p0.y), ImVec2(p1.x - offset, p1.y),
                    IM_COL32(255, 0, 0, 40), 0.0f, 0, 2.0f);
        dl->AddRect(ImVec2(p0.x + offset, p0.y), ImVec2(p1.x + offset, p1.y),
                    IM_COL32(0, 0, 255, 40), 0.0f, 0, 2.0f);
    }

    if (fx.lensDistortion.enabled && std::abs(fx.lensDistortion.intensity) > 0.001f) {
        const u8 d = static_cast<u8>(std::clamp(std::abs(fx.lensDistortion.intensity) * 50.0f, 0.0f, 70.0f));
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, d), IM_COL32(0, 0, 0, d),
                                    IM_COL32(0, 0, 0, d), IM_COL32(0, 0, 0, d));
    }

    if (fx.motionBlur.enabled && fx.motionBlur.intensity > 0.001f) {
        const u8 mb = static_cast<u8>(std::clamp(fx.motionBlur.intensity, 0.0f, 1.0f) * 35.0f);
        const f32 stripe = size.x * 0.15f;
        dl->AddRectFilled(ImVec2(p0.x, p0.y + size.y * 0.45f),
                          ImVec2(p0.x + stripe, p0.y + size.y * 0.55f),
                          IM_COL32(255, 255, 255, mb));
    }

    if (fx.grain.enabled && fx.grain.intensity > 0.001f) {
        const u8 grain = static_cast<u8>(std::clamp(fx.grain.intensity, 0.0f, 1.0f) * 28.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(128, 128, 128, grain));
    }

    if (fx.antiAliasing.enabled) {
        const u8 aa =
            static_cast<u8>(std::clamp(fx.antiAliasing.sharpness * 18.0f + 8.0f, 8.0f, 28.0f));
        dl->AddRect(p0, p1, IM_COL32(180, 220, 255, aa), 0.0f, 0, 1.0f);
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
