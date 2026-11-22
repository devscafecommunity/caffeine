#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

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

    /// With a WeatherComponent that links the sky: the clouds dim the sun by their cover.
    bool castShadows = true;
    u8 _pad[3] = {};
    f32 _reserved = 0.0f;

    // ---- Appended later: scenes written before these keep the defaults, which render as before.
    /// Multiplies the colour of the sun-lit side of the clouds (1,1,1 = the sun's own colour).
    Vec3 tint{1.0f, 1.0f, 1.0f};
    /// 0..1: darkens and thickens the clouds towards a storm.
    f32 darkness = 0.0f;
    /// Strength of the sky light that fills the shadowed side of the clouds.
    f32 skyLight = 1.0f;

    // ---- Appended with the cloud shadows and the sun's light through them: a scene written before keeps these.
    /// The clouds shade the ground: moving patches of shadow on everything the sun reaches (forward shading and
    /// Photon's bounce follow), from the same noise as the sky's clouds.
    bool groundShadows = false;
    u8 _pad2[3] = {};
    f32 shadowStrength = 0.7f;  ///< 0..1: how dark a fully clouded patch is
    /// Bright rim where the sun is behind a cloud's edge, 0..2.
    f32 silverLining = 0.0f;
    /// Light shafts through the clouds and past the geometry when the sun is in view, 0..1.
    f32 sunShafts = 0.0f;
};

}  // namespace Caffeine::ECS
