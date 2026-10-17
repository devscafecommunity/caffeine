#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

namespace Caffeine::ECS {

enum class EnvironmentEffectKind : u8 {
    WorldLight = 0,
};

enum class WorldLightMode : u8 {
    DayNight = 0,
    Zone = 1,
};

/// Scene-wide environment driver. Core engine object, not a plugin.
/// World Light Simulation: Day/Night rotates the sun and ambient; Zone tints a volume.
struct EnvironmentEffectsComponent {
    u8 kind = 0;
    u8 mode = 0;
    u8 enabled = 1;
    u8 driveExistingLights = 1;
    f32 timeOfDay = 14.0f;
    f32 dayLengthSeconds = 0.0f;
    f32 sunIntensity = 1.2f;
    f32 moonIntensity = 0.18f;
    Vec3 sunColor{1.00f, 0.96f, 0.88f};
    Vec3 moonColor{0.45f, 0.55f, 0.85f};
    f32 ambientDay = 0.28f;
    f32 ambientNight = 0.04f;
    f32 shadowStrength = 1.0f;
    f32 aoDay = 0.18f;
    f32 aoNight = 0.55f;
    f32 zoneRadius = 12.0f;
    Vec3 zoneColor{1.00f, 0.92f, 0.75f};
    f32 zoneIntensity = 1.0f;
    f32 zoneFalloff = 1.5f;
    Vec3 drivenAmbient{0.18f, 0.18f, 0.20f};
    f32 drivenAmbient2D = 1.0f;
    f32 skyExposure = 1.0f;
    u8 castShadows = 1;
    u8 environmentLighting = 1;
    u8 ambientOcclusion = 1;
    u8 volumetricsEnabled = 1;
    f32 indoorDarkness = 0.92f;
    f32 skyVisibility = 1.0f;
    f32 volumetricDensity = 0.045f;
};

}  // namespace Caffeine::ECS
