#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

/// Procedural volumetric clouds and mist layered over the sky.
/// The engine renders the volume; plugins/scripts author the profile.
struct VolumetricCloudsComponent {
    bool enabled = false;
    /// 0 = off, 1 = low, 2 = medium, 3 = high (ray-march step count).
    u8 quality = 2;
    /// 0..1 sky coverage. 1 fills the sky.
    f32 coverage = 0.55f;
    /// Extinction scale of the cloud medium.
    f32 density = 0.35f;
    /// Cloud layer bounds in world units (metres).
    f32 baseAltitude = 1400.0f;
    f32 topAltitude = 2800.0f;
    /// Weight of the high-frequency detail octaves.
    f32 detail = 0.55f;
    /// Procedural seed (shifts the noise field).
    f32 seed = 7.0f;
    /// Horizontal wind in world units per second.
    f32 windSpeed = 8.0f;
    /// Wind direction in degrees around +Y.
    f32 windDirection = 35.0f;
    /// Ground mist blend at the camera height (0..1).
    f32 mistDensity = 0.12f;
    /// Mist falloff height.
    f32 mistHeight = 40.0f;
    /// Reserved for future density falloff shaping.
    f32 mistFalloff = 0.35f;
    /// Sun scattering strength multiplier.
    f32 sunScatter = 1.0f;

    u8 castShadows = 1;
    u8 _pad[3] = {};
    f32 _reserved = 0.0f;
};

}  // namespace Caffeine::ECS
