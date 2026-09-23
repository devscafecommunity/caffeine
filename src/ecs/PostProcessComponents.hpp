#pragma once

#include "core/Types.hpp"
#include <cstring>

namespace Caffeine::ECS {

/// Modular post-processing stack — each effect can be toggled and tuned independently.
/// GPU passes are planned; editor preview uses ImGui overlays until the render graph is wired.

struct PostProcessAmbientOcclusion {
    bool enabled = false;
    f32 intensity = 1.0f;
    f32 radius = 0.5f;
    f32 bias = 0.025f;
};

struct PostProcessAntiAliasing {
    bool enabled = false;
    /// 0 = FXAA, 1 = TAA (placeholder indices for future GPU path)
    u32 mode = 0;
    f32 sharpness = 0.5f;
};

struct PostProcessAutoExposure {
    bool enabled = false;
    f32 minExposure = 0.2f;
    f32 maxExposure = 4.0f;
    f32 adaptationSpeed = 1.0f;
    f32 compensation = 0.0f;
};

struct PostProcessBloom {
    bool enabled = false;
    f32 intensity = 0.0f;
    f32 threshold = 1.0f;
    f32 scatter = 0.7f;
};

struct PostProcessChromaticAberration {
    bool enabled = false;
    f32 intensity = 0.0f;
};

struct PostProcessColorGrading {
    bool enabled = true;
    f32 exposure = 1.0f;
    f32 contrast = 1.0f;
    f32 saturation = 1.0f;
    f32 temperature = 0.0f;
    f32 tint = 0.0f;
};

struct PostProcessDeferredFog {
    bool enabled = false;
    f32 density = 0.02f;
    f32 start = 10.0f;
    f32 end = 200.0f;
    f32 colorR = 0.7f;
    f32 colorG = 0.75f;
    f32 colorB = 0.85f;
};

struct PostProcessDepthOfField {
    bool enabled = false;
    f32 focusDistance = 10.0f;
    f32 aperture = 0.1f;
    f32 focalLength = 50.0f;
    f32 maxBlur = 1.0f;
};

struct PostProcessGrain {
    bool enabled = false;
    f32 intensity = 0.0f;
    f32 size = 1.0f;
};

struct PostProcessLensDistortion {
    bool enabled = false;
    f32 intensity = 0.0f;
};

struct PostProcessMotionBlur {
    bool enabled = false;
    f32 intensity = 0.0f;
    f32 maxVelocity = 1.0f;
};

struct PostProcessScreenSpaceReflections {
    bool enabled = false;
    f32 intensity = 0.5f;
    f32 maxRoughness = 0.6f;
    u32 maxSteps = 64;
};

struct PostProcessVignette {
    bool enabled = false;
    f32 intensity = 0.0f;
    f32 smoothness = 0.5f;
};

struct PostProcessComponent {
    bool enabled = true;

    PostProcessAmbientOcclusion ambientOcclusion;
    PostProcessAntiAliasing antiAliasing;
    PostProcessAutoExposure autoExposure;
    PostProcessBloom bloom;
    PostProcessChromaticAberration chromaticAberration;
    PostProcessColorGrading colorGrading;
    PostProcessDeferredFog deferredFog;
    PostProcessDepthOfField depthOfField;
    PostProcessGrain grain;
    PostProcessLensDistortion lensDistortion;
    PostProcessMotionBlur motionBlur;
    PostProcessScreenSpaceReflections screenSpaceReflections;
    PostProcessVignette vignette;

    /// Optional Lua module: onPostProcess(entityId, dt, api) for custom stacks / effect logic.
    char customEffectScript[256] = {};

    // Legacy flat accessors (sync with modular fields for older code paths).
    f32 exposure() const { return colorGrading.exposure; }
    void setExposure(f32 v) { colorGrading.exposure = v; }

    f32 contrast() const { return colorGrading.contrast; }
    void setContrast(f32 v) { colorGrading.contrast = v; }

    f32 saturation() const { return colorGrading.saturation; }
    void setSaturation(f32 v) { colorGrading.saturation = v; }

    f32 vignetteIntensity() const { return vignette.intensity; }
    void setVignetteIntensity(f32 v) {
        vignette.intensity = v;
        vignette.enabled = v > 0.001f;
    }

    f32 bloomIntensity() const { return bloom.intensity; }
    void setBloomIntensity(f32 v) {
        bloom.intensity = v;
        bloom.enabled = v > 0.001f;
    }

    f32 chromaticIntensity() const { return chromaticAberration.intensity; }
    void setChromaticIntensity(f32 v) {
        chromaticAberration.intensity = v;
        chromaticAberration.enabled = v > 0.001f;
    }

    f32 filmGrainIntensity() const { return grain.intensity; }
    void setFilmGrainIntensity(f32 v) {
        grain.intensity = v;
        grain.enabled = v > 0.001f;
    }
};

}  // namespace Caffeine::ECS
