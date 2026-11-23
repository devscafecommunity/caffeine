#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

namespace Caffeine::ECS {

/// Scene weather: exponential height fog and rain, plus the coupling that makes them agree with the
/// clouds (VolumetricCloudsComponent), the sun and Photon's sky. Every section is off by default, so
/// adding the component changes nothing until a section is switched on, and a scene without it renders
/// exactly as before. The first enabled component in the scene is the one the renderer reads.
///
/// The struct is serialised as a raw prefix: new fields are only ever appended.
struct WeatherComponent {
    bool enabled = true;  ///< master switch
    bool fogEnabled = false;
    bool rainEnabled = false;
    /// Couples the sections: rain thickens the clouds and the fog, an overcast sky dims the sun and
    /// greys Photon's sky light, fog tints it. Off = each section acts alone.
    bool linkSky = true;

    // ---- Fog: density * exp(-falloff * (y - baseHeight)) per metre, integrated along the view ray.
    f32 fogDensity = 0.012f;
    f32 fogHeightFalloff = 0.06f;  ///< 0 = the same density at every height
    f32 fogBaseHeight = 0.0f;      ///< world Y where the density equals `fogDensity`
    f32 fogStart = 0.0f;           ///< metres from the camera before the fog begins
    f32 fogMaxOpacity = 0.92f;
    Vec3 fogColor{0.62f, 0.68f, 0.76f};  ///< sRGB
    /// 0 = `fogColor`, 1 = the sky's colour at the horizon in the direction being looked at.
    f32 fogSkyBlend = 0.5f;
    f32 fogSunScatter = 0.35f;     ///< glow of the fog towards the sun
    f32 fogSunExponent = 6.0f;     ///< how tight that glow is

    // ---- Rain: layered streaks hidden behind nearer geometry, and wet surfaces.
    f32 rainIntensity = 0.5f;      ///< 0..1
    f32 rainWindSpeed = 4.0f;      ///< metres per second, tilts the streaks
    f32 rainWindDirection = 35.0f; ///< degrees around +Y (same convention as the clouds' wind)
    f32 rainDropLength = 0.5f;     ///< 0..1
    f32 rainSpeed = 1.0f;          ///< animation speed multiplier
    f32 rainBrightness = 0.6f;
    f32 rainWetness = 0.8f;        ///< how wet surfaces get at full intensity (0..1)

    // ---- Appended with the depth rain, the wind and the lightning: a scene written before keeps these defaults.
    /// Rings and splashes where the rain meets the ground near the camera, 0..1.
    f32 rainSplashes = 0.6f;
    /// How much the wind surges, 0..1 (the rain's slant, the foliage's sway and the clouds' drift share the gusts).
    f32 windGust = 0.35f;
    /// Multiplies the sway of foliage materials with the wind (rain wind speed, in gusts); 0 leaves foliage alone.
    f32 windFoliage = 0.0f;

    bool lightningEnabled = false;
    bool thunder = true;           ///< strikes raise thunder events, arriving after the sound's travel time
    u8 _pad[2] = {};
    f32 lightningRate = 6.0f;      ///< strikes per minute
    f32 lightningIntensity = 1.0f; ///< 0..2: the brightness of the flash
    f32 lightningDistanceMin = 300.0f;   ///< metres
    f32 lightningDistanceMax = 3500.0f;
    u32 lightningSeed = 1;         ///< the same seed strikes at the same times and places

    // ---- Appended with the weather's sound.
    /// The editor and runtime play the rain, the wind and the thunder (made from noise, no files). Off by default:
    /// a scene is silent until it asks for sound.
    bool audioEnabled = false;
    u8 _pad2[3] = {};
    f32 audioVolume = 0.8f;        ///< 0..1, the master volume of the weather's sound
};

}  // namespace Caffeine::ECS
