#pragma once

#include "core/Types.hpp"
#include "render/RenderFeatures.hpp"

namespace Caffeine::ECS {

/// Per-scene forward-pass capabilities (instancing, IBL, occlusion, reflections, volumetrics).
/// Attach to an environment entity; the GPU renderer resolves these instead of editor settings.
struct ForwardInstancingProfile {
    bool enabled = true;
    u32  maxInstancesPerBatch = 256;
};

struct ForwardIblProfile {
    bool enabled = true;
    f32  diffuse = 1.0f;
    f32  specular = 1.0f;
};

struct ForwardOcclusionProfile {
    bool enabled = false;
    u32  maxOccluders = 8;
    f32  minRadius = 0.75f;
};

struct ForwardReflectionProfile {
    bool enabled = true;
    Render::ReflectionMode mode = Render::ReflectionMode::Probe;
    f32 planeY = 0.0f;
    f32 intensity = 0.45f;
    f32 resolutionScale = 0.5f;
    bool screenSpaceTrace = true;
    f32 ssrIntensity = 1.0f;
    f32 ssrMaxRoughness = 0.6f;
    u32 ssrMaxSteps = 24;
    f32 ssrMaxDistance = 30.0f;
    u32 probeResolution = 128;
    u32 maxProbes = 6;
    bool expensiveOnlyWhenSettled = true;
};

struct ForwardVolumetricProfile {
    bool enabled = false;
    Render::VolumetricQuality quality = Render::VolumetricQuality::Off;
    f32 density = 0.012f;
    f32 height = 12.0f;
    f32 anisotropy = 0.3f;
    bool sampleShadows = false;
};

struct ForwardRenderFeaturesComponent {
    bool enabled = true;
    ForwardInstancingProfile instancing{};
    ForwardIblProfile ibl{};
    ForwardOcclusionProfile occlusion{};
    ForwardReflectionProfile reflections{};
    ForwardVolumetricProfile volumetrics{};
};

}  // namespace Caffeine::ECS
