#pragma once

#include "core/Types.hpp"

namespace Caffeine::Render {

/// Resolved at runtime from ForwardRenderFeaturesComponent (see ForwardRenderFeatures.hpp).
/// Coarse CPU occlusion is conservative on AABBs and can hide objects beside a large box.
enum class OcclusionMode : u8 { Off = 0, Coarse = 1 };

/// How glossy surfaces find what they reflect.
/// Off: sky only. Planar: a mirrored redraw for one flat floor. ScreenSpace: SSR only.
/// Probe (default): every glossy object gets its own cubemap of the scene around it, refined
/// by screen-space tracing where the reflected object is on screen.
enum class ReflectionMode : u8 { Off = 0, Planar = 1, ScreenSpace = 2, Probe = 3 };

/// Analytical height fog in the forward shader. It does not redraw the scene.
/// Low is 6 steps, Medium is 12. Shadow samples along the ray are optional.
enum class VolumetricQuality : u8 { Off = 0, Low = 1, Medium = 2 };

struct RenderFeatureSettings {
    bool instancingEnabled = true;
    u32  maxInstancesPerBatch = 256;

    OcclusionMode occlusion = OcclusionMode::Off;
    u32 occlusionMaxOccluders = 8;
    f32 occlusionMinRadius = 0.75f;

    bool iblEnabled = true;
    f32 iblDiffuse = 1.0f;
    f32 iblSpecular = 1.0f;

    ReflectionMode reflections = ReflectionMode::Probe;
    f32 reflectionPlaneY = 0.0f;
    f32 reflectionIntensity = 0.45f;
    f32 reflectionResolutionScale = 0.5f;
    /// Screen-space tracing on top of probes / planar (ScreenSpace mode always traces).
    bool screenSpaceTrace = true;
    f32 ssrIntensity = 1.0f;
    f32 ssrMaxRoughness = 0.6f;
    u32 ssrMaxSteps = 24;
    f32 ssrMaxDistance = 30.0f;
    u32 probeResolution = 128;
    /// Glossy objects closest to the camera that get their own probe.
    u32 maxReflectionProbes = 6;
    bool expensiveEffectsOnlyWhenSettled = true;

    VolumetricQuality volumetrics = VolumetricQuality::Off;
    f32 volumetricDensity = 0.012f;
    f32 volumetricHeight = 12.0f;
    f32 volumetricAnisotropy = 0.3f;
    bool volumetricShadows = false;
};

}  // namespace Caffeine::Render
