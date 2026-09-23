#pragma once

#include "caffeine/postprocess/PostProcessPresets.hpp"
#include "ecs/PostProcessComponents.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::PostProcess::EditorUI {

#ifdef CF_HAS_IMGUI

inline bool dragEffectBool(const char* label, bool& value) {
    return ImGui::Checkbox(label, &value);
}

inline bool drawBenchmarkPresets(ECS::PostProcessComponent& fx) {
    bool dirty = false;
    ImGui::TextDisabled("Benchmark looks (optional starting points)");
    if (ImGui::Button("Cinematic")) {
        applyBenchmarkCinematic(fx);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Horror")) {
        applyBenchmarkHorror(fx);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Arcade")) {
        applyBenchmarkArcade(fx);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        resetAllEffects(fx);
        dirty = true;
    }
    return dirty;
}

inline bool drawEffectStack(ECS::PostProcessComponent& fx) {
    bool dirty = false;

    ImGui::TextColored(ImVec4(0.75f, 0.8f, 0.95f, 1.0f),
                       "Editor preview overlays are active in the Scene Viewport and camera previews. "
                       "Full GPU post-processing passes are not wired yet.");
    ImGui::Spacing();

    if (ImGui::Checkbox("Stack enabled", &fx.enabled)) dirty = true;

    if (ImGui::CollapsingHeader("Pre-processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (dragEffectBool("Anti-aliasing", fx.antiAliasing.enabled)) dirty = true;
        if (fx.antiAliasing.enabled) {
            const char* modes[] = {"FXAA", "TAA"};
            int mode = static_cast<int>(fx.antiAliasing.mode);
            if (ImGui::Combo("AA mode", &mode, modes, 2)) {
                fx.antiAliasing.mode = static_cast<Caffeine::u32>(mode);
                dirty = true;
            }
            if (ImGui::SliderFloat("Sharpness", &fx.antiAliasing.sharpness, 0.0f, 1.0f)) {
                dirty = true;
            }
        }
        if (dragEffectBool("Ambient occlusion", fx.ambientOcclusion.enabled)) dirty = true;
        if (fx.ambientOcclusion.enabled) {
            if (ImGui::SliderFloat("AO intensity", &fx.ambientOcclusion.intensity, 0.0f, 2.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("AO radius", &fx.ambientOcclusion.radius, 0.1f, 2.0f)) {
                dirty = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Color & exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (dragEffectBool("Auto exposure", fx.autoExposure.enabled)) dirty = true;
        if (fx.autoExposure.enabled) {
            if (ImGui::SliderFloat("Min exposure", &fx.autoExposure.minExposure, 0.05f, 2.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Max exposure", &fx.autoExposure.maxExposure, 1.0f, 8.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Adapt speed", &fx.autoExposure.adaptationSpeed, 0.1f, 5.0f)) {
                dirty = true;
            }
        }
        if (dragEffectBool("Color grading", fx.colorGrading.enabled)) dirty = true;
        if (fx.colorGrading.enabled) {
            if (ImGui::SliderFloat("Exposure", &fx.colorGrading.exposure, 0.2f, 4.0f)) dirty = true;
            if (ImGui::SliderFloat("Contrast", &fx.colorGrading.contrast, 0.5f, 2.0f)) dirty = true;
            if (ImGui::SliderFloat("Saturation", &fx.colorGrading.saturation, 0.0f, 2.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Temperature", &fx.colorGrading.temperature, -1.0f, 1.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Tint", &fx.colorGrading.tint, -1.0f, 1.0f)) dirty = true;
        }
    }

    if (ImGui::CollapsingHeader("Image effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (dragEffectBool("Bloom", fx.bloom.enabled)) dirty = true;
        if (fx.bloom.enabled) {
            if (ImGui::SliderFloat("Bloom intensity", &fx.bloom.intensity, 0.0f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Threshold", &fx.bloom.threshold, 0.5f, 2.0f)) dirty = true;
            if (ImGui::SliderFloat("Scatter", &fx.bloom.scatter, 0.0f, 1.0f)) dirty = true;
        }
        if (dragEffectBool("Depth of field", fx.depthOfField.enabled)) dirty = true;
        if (fx.depthOfField.enabled) {
            if (ImGui::SliderFloat("Focus distance", &fx.depthOfField.focusDistance, 0.5f, 100.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Aperture", &fx.depthOfField.aperture, 0.01f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Max blur", &fx.depthOfField.maxBlur, 0.0f, 2.0f)) dirty = true;
        }
        if (dragEffectBool("Motion blur", fx.motionBlur.enabled)) dirty = true;
        if (fx.motionBlur.enabled) {
            if (ImGui::SliderFloat("Motion blur", &fx.motionBlur.intensity, 0.0f, 1.0f)) {
                dirty = true;
            }
        }
        if (dragEffectBool("Chromatic aberration", fx.chromaticAberration.enabled)) dirty = true;
        if (fx.chromaticAberration.enabled) {
            if (ImGui::SliderFloat("Chromatic", &fx.chromaticAberration.intensity, 0.0f, 1.0f)) {
                dirty = true;
            }
        }
        if (dragEffectBool("Lens distortion", fx.lensDistortion.enabled)) dirty = true;
        if (fx.lensDistortion.enabled) {
            if (ImGui::SliderFloat("Distortion", &fx.lensDistortion.intensity, -1.0f, 1.0f)) {
                dirty = true;
            }
        }
        if (dragEffectBool("Grain", fx.grain.enabled)) dirty = true;
        if (fx.grain.enabled) {
            if (ImGui::SliderFloat("Grain", &fx.grain.intensity, 0.0f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Grain size", &fx.grain.size, 0.5f, 3.0f)) dirty = true;
        }
        if (dragEffectBool("Vignette", fx.vignette.enabled)) dirty = true;
        if (fx.vignette.enabled) {
            if (ImGui::SliderFloat("Vignette", &fx.vignette.intensity, 0.0f, 1.0f)) dirty = true;
            if (ImGui::SliderFloat("Vignette smooth", &fx.vignette.smoothness, 0.0f, 1.0f)) {
                dirty = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Screen space", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (dragEffectBool("Screen space reflections", fx.screenSpaceReflections.enabled)) {
            dirty = true;
        }
        if (fx.screenSpaceReflections.enabled) {
            if (ImGui::SliderFloat("SSR intensity", &fx.screenSpaceReflections.intensity, 0.0f, 1.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Max roughness", &fx.screenSpaceReflections.maxRoughness, 0.0f, 1.0f)) {
                dirty = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Atmosphere")) {
        if (dragEffectBool("Deferred fog", fx.deferredFog.enabled)) dirty = true;
        if (fx.deferredFog.enabled) {
            if (ImGui::SliderFloat("Fog density", &fx.deferredFog.density, 0.0f, 0.2f)) dirty = true;
            if (ImGui::SliderFloat("Fog start", &fx.deferredFog.start, 0.0f, 500.0f)) dirty = true;
            if (ImGui::SliderFloat("Fog end", &fx.deferredFog.end, 1.0f, 2000.0f)) dirty = true;
            if (ImGui::ColorEdit3("Fog color", &fx.deferredFog.colorR)) dirty = true;
        }
    }

    if (ImGui::CollapsingHeader("Scripting")) {
        ImGui::TextWrapped(
            "Attach a Lua module to drive the stack at runtime, or author custom effect logic.");
        if (ImGui::InputText("Custom effect script", fx.customEffectScript,
                             sizeof(fx.customEffectScript))) {
            dirty = true;
        }
        ImGui::TextDisabled("API: caffeine.postprocess.* — see docs/rendering/post-processing.md");
    }

    return dirty;
}

#endif

}  // namespace Caffeine::PostProcess::EditorUI
