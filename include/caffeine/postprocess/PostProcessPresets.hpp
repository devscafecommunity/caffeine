#pragma once

#include "ecs/PostProcessComponents.hpp"

namespace Caffeine::PostProcess {

/// Benchmark looks — starting points, not game types.
inline void applyBenchmarkCinematic(ECS::PostProcessComponent& fx) {
    fx.enabled = true;
    fx.colorGrading.enabled = true;
    fx.colorGrading.exposure = 1.05f;
    fx.colorGrading.contrast = 1.1f;
    fx.colorGrading.saturation = 1.05f;
    fx.vignette.enabled = true;
    fx.vignette.intensity = 0.35f;
    fx.bloom.enabled = true;
    fx.bloom.intensity = 0.15f;
    fx.chromaticAberration.enabled = true;
    fx.chromaticAberration.intensity = 0.02f;
    fx.grain.enabled = true;
    fx.grain.intensity = 0.04f;
    fx.depthOfField.enabled = true;
    fx.depthOfField.focusDistance = 12.0f;
    fx.depthOfField.aperture = 0.08f;
}

inline void applyBenchmarkHorror(ECS::PostProcessComponent& fx) {
    fx.enabled = true;
    fx.colorGrading.enabled = true;
    fx.colorGrading.exposure = 0.75f;
    fx.colorGrading.contrast = 1.25f;
    fx.colorGrading.saturation = 0.7f;
    fx.colorGrading.temperature = -0.15f;
    fx.vignette.enabled = true;
    fx.vignette.intensity = 0.55f;
    fx.grain.enabled = true;
    fx.grain.intensity = 0.12f;
    fx.chromaticAberration.enabled = true;
    fx.chromaticAberration.intensity = 0.08f;
    fx.deferredFog.enabled = true;
    fx.deferredFog.density = 0.04f;
    fx.ambientOcclusion.enabled = true;
    fx.ambientOcclusion.intensity = 1.2f;
}

inline void applyBenchmarkArcade(ECS::PostProcessComponent& fx) {
    fx.enabled = true;
    fx.colorGrading.enabled = true;
    fx.colorGrading.exposure = 1.2f;
    fx.colorGrading.contrast = 1.15f;
    fx.colorGrading.saturation = 1.3f;
    fx.bloom.enabled = true;
    fx.bloom.intensity = 0.25f;
    fx.bloom.scatter = 0.85f;
    fx.vignette.enabled = true;
    fx.vignette.intensity = 0.1f;
    fx.motionBlur.enabled = true;
    fx.motionBlur.intensity = 0.15f;
    fx.antiAliasing.enabled = true;
    fx.antiAliasing.mode = 0;
}

inline void resetAllEffects(ECS::PostProcessComponent& fx) {
    fx = ECS::PostProcessComponent {};
}

}  // namespace Caffeine::PostProcess
