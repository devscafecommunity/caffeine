#pragma once

#include "caffeine/environment/VolumetricClouds.hpp"
#include "caffeine/photon/Photon.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/WeatherComponents.hpp"
#include "ecs/World.hpp"
#include "math/Vec3.hpp"
#include "scene/HierarchySystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Caffeine::Scene {

/// What the renderer reads once per frame: the weather section values (fog and rain, already coupled to
/// each other) and what an overcast, foggy or rainy sky does to the light. Pure data: a scene without a
/// WeatherComponent resolves to the identity (nothing active, every scale 1).
struct WeatherResolved {
    bool active = false;  ///< any section is on

    bool fog = false;
    f32 fogDensity = 0.0f;
    f32 fogHeightFalloff = 0.0f;
    f32 fogBaseHeight = 0.0f;
    f32 fogStart = 0.0f;
    f32 fogMaxOpacity = 0.0f;
    Vec3 fogColor{1.0f, 1.0f, 1.0f};  ///< linear
    f32 fogSkyBlend = 0.0f;
    f32 fogSunScatter = 0.0f;
    f32 fogSunExponent = 1.0f;

    bool rain = false;
    f32 rainIntensity = 0.0f;
    f32 rainWindSpeed = 0.0f;
    f32 rainWindDirection = 0.0f;
    f32 rainDropLength = 0.0f;
    f32 rainSpeed = 1.0f;
    f32 rainBrightness = 0.0f;
    f32 wetness = 0.0f;  ///< 0..1 added to every lit mesh's own wetness

    /// 0 = clear, 1 = fully overcast (cloud cover and rain, whichever is larger).
    f32 overcast = 0.0f;
    /// Multiplies the directional lights (the same number reaches the forward shader and Photon).
    f32 sunScale = 1.0f;
    /// Multiplies Photon's sky light and the forward ambient.
    f32 skyScale = 1.0f;
    /// Colour factor on Photon's sky radiance (greyer under cloud, hued by thick fog).
    Vec3 skyTint{1.0f, 1.0f, 1.0f};

    /// The clouds as the sky should draw them after the coupling (valid when `cloudsLinked`).
    bool cloudsLinked = false;
    f32 cloudCoverage = 0.0f;
    f32 cloudDensity = 0.0f;
    f32 cloudDarkness = 0.0f;

    // ---- Wind, splashes and lightning.
    f32 rainSplashes = 0.0f;
    f32 windGust = 0.0f;
    /// Foliage sway multiplier at a calm moment (1 = untouched); the renderer scales it by the gust of the moment.
    f32 windFoliage = 0.0f;

    /// Clouds that shade the ground and shine at their edges (copied from the clouds when they are on).
    bool cloudShadows = false;
    f32 cloudShadowStrength = 0.0f;
    f32 silverLining = 0.0f;
    f32 sunShafts = 0.0f;

    /// Lightning: strikes need storm clouds (an overcast of 0.4 or more) when the link is on.
    bool lightning = false;
    bool thunder = false;
    f32 lightningRate = 0.0f;
    f32 lightningIntensity = 0.0f;
    f32 lightningDistanceMin = 300.0f;
    f32 lightningDistanceMax = 3500.0f;
    u32 lightningSeed = 1;

    /// The weather's sound (rain, wind and thunder beds) is on, and how loud.
    bool audio = false;
    f32 audioVolume = 0.8f;
};

// ---------------------------------------------------------------------------------------------- wind

/// How strong the wind is at `time` relative to its mean: 1 on average, surging by `gustiness` (0..1). A sum of
/// slow sines, so it is smooth, never negative and repeats only after minutes.
inline f32 windGustFactor(f32 time, f32 gustiness) {
    const f32 g = std::clamp(gustiness, 0.0f, 1.0f);
    const f32 wave = 0.55f * std::sin(0.71f * time) + 0.30f * std::sin(1.93f * time + 1.3f) +
                     0.15f * std::sin(5.17f * time + 0.4f);
    return std::max(0.15f, 1.0f + g * 1.2f * wave);
}

// ------------------------------------------------------------------------------------------ lightning

/// The settings of the lightning, as `resolveWeather` hands them over.
struct LightningSettings {
    f32 rate = 6.0f;  ///< strikes per minute
    f32 intensity = 1.0f;
    f32 distanceMin = 300.0f;
    f32 distanceMax = 3500.0f;
    u32 seed = 1;
};

/// One strike, derived from the seed and the slot it falls in: always the same for the same inputs.
struct LightningStrike {
    bool valid = false;
    f32 start = 0.0f;      ///< seconds
    f32 azimuth = 0.0f;    ///< radians, around +Y, the direction of the bolt
    f32 distance = 0.0f;   ///< metres
    f32 elevation = 0.0f;  ///< radians: how high the bolt's top is seen
    f32 strength = 0.0f;   ///< brightness at the viewer, after the distance
    f32 pulses[3] = {};    ///< when each return stroke fires, seconds after `start`
    f32 amplitude[3] = {};
};

/// What a frame sees of the lightning at one moment.
struct LightningState {
    f32 flash = 0.0f;      ///< 0 none; about 1 at a full return stroke, more for a close strike
    f32 bolt = 0.0f;       ///< 0..1: how visible the bolt itself is
    LightningStrike strike;
};

/// A clap of thunder arriving at the listener.
struct ThunderEvent {
    f32 arrival = 0.0f;   ///< seconds on the clock the strikes use
    f32 distance = 0.0f;  ///< metres
    f32 azimuth = 0.0f;
    f32 volume = 0.0f;    ///< 0..1
};

inline constexpr f32 kSpeedOfSound = 343.0f;

inline f32 lightningHash01(u32 seed, i64 slot, u32 salt) {
    u64 x = (static_cast<u64>(slot) * 0x9E3779B97F4A7C15ull) ^ (static_cast<u64>(seed) << 17) ^
            (static_cast<u64>(salt) * 0xBF58476D1CE4E5B9ull);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<f32>(static_cast<u32>(x >> 40)) / 16777216.0f;
}

inline f32 lightningSlotLength(const LightningSettings& settings) {
    return 60.0f / std::clamp(settings.rate, 0.2f, 120.0f);
}

/// The strike of slot `slot`: one per slot, at a hashed moment of it.
inline LightningStrike lightningStrike(const LightningSettings& settings, i64 slot) {
    LightningStrike strike;
    const f32 slotLength = lightningSlotLength(settings);
    const f32 dMin = std::max(settings.distanceMin, 50.0f);
    const f32 dMax = std::max(settings.distanceMax, dMin);
    strike.valid = true;
    strike.start = (static_cast<f32>(slot) + 0.05f + 0.85f * lightningHash01(settings.seed, slot, 1)) * slotLength;
    strike.azimuth = lightningHash01(settings.seed, slot, 2) * 6.28318530718f;
    strike.distance = dMin * std::pow(dMax / dMin, lightningHash01(settings.seed, slot, 3));
    // The top of the bolt sits in the cloud base, about 1.8 km up.
    strike.elevation = std::atan2(1800.0f, strike.distance);
    const f32 reach = std::clamp(1.2f / (1.0f + std::pow(strike.distance / 900.0f, 1.2f)), 0.15f, 1.2f);
    strike.strength = (0.6f + 0.4f * lightningHash01(settings.seed, slot, 4)) * std::max(settings.intensity, 0.0f) * reach;
    strike.pulses[0] = 0.0f;
    strike.pulses[1] = 0.08f + 0.06f * lightningHash01(settings.seed, slot, 5);
    strike.pulses[2] = strike.pulses[1] + 0.08f + 0.08f * lightningHash01(settings.seed, slot, 6);
    strike.amplitude[0] = 1.0f;
    strike.amplitude[1] = 0.5f + 0.3f * lightningHash01(settings.seed, slot, 7);
    strike.amplitude[2] = 0.35f + 0.4f * lightningHash01(settings.seed, slot, 8);
    return strike;
}

/// The flash and the bolt at `time`. Only the strikes of the current slot and the one before can still be lit.
inline LightningState lightningAt(const LightningSettings& settings, f32 time) {
    LightningState state;
    if (settings.intensity <= 0.0f || time < 0.0f) return state;
    const f32 slotLength = lightningSlotLength(settings);
    const i64 slot = static_cast<i64>(std::floor(time / slotLength));
    for (i64 k = slot; k >= std::max<i64>(0, slot - 1); --k) {
        const LightningStrike strike = lightningStrike(settings, k);
        const f32 age = time - strike.start;
        if (age < 0.0f || age > 1.6f) continue;
        f32 flash = 0.0f;
        f32 bolt = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const f32 dt = age - strike.pulses[i];
            if (dt < 0.0f) continue;
            flash += strike.amplitude[i] * std::exp(-dt / (0.045f + 0.02f * static_cast<f32>(i)));
            bolt += strike.amplitude[i] * std::exp(-dt / 0.05f);
        }
        flash += 0.25f * std::exp(-age / 0.35f);  // the cloud's afterglow
        flash *= strike.strength;
        if (flash > state.flash) {
            state.flash = flash;
            state.bolt = std::clamp(bolt, 0.0f, 1.0f);
            state.strike = strike;
        }
    }
    return state;
}

/// The thunder that reaches the listener in (`from`, `to`] on the strikes' clock. At most `capacity` are written;
/// the number found is returned.
inline u32 thunderBetween(const LightningSettings& settings, f32 from, f32 to, ThunderEvent* out, u32 capacity) {
    if (settings.intensity <= 0.0f || to <= from) return 0;
    const f32 slotLength = lightningSlotLength(settings);
    const f32 latest = std::max(settings.distanceMax, settings.distanceMin) / kSpeedOfSound + 1.0f;
    const i64 first = std::max<i64>(0, static_cast<i64>(std::floor((from - latest) / slotLength)));
    const i64 last = static_cast<i64>(std::floor(to / slotLength));
    u32 found = 0;
    for (i64 k = first; k <= last; ++k) {
        const LightningStrike strike = lightningStrike(settings, k);
        const f32 arrival = strike.start + strike.distance / kSpeedOfSound;
        if (arrival <= from || arrival > to) continue;
        if (out && found < capacity) {
            out[found] = ThunderEvent{arrival, strike.distance, strike.azimuth,
                                      std::clamp(1.0f / (1.0f + strike.distance / 1200.0f), 0.05f, 1.0f)};
        }
        ++found;
    }
    return found;
}

inline f32 weatherSrgbToLinear(f32 c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline f32 weatherSmoothstep(f32 a, f32 b, f32 x) {
    const f32 t = std::clamp((x - a) / std::max(b - a, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline f32 weatherLerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

/// The sky colour that Photon uses where a ray escapes, before any weather.
inline Vec3 weatherBaseSky() {
    return Vec3(ECS::kPhotonSkyRadiance[0], ECS::kPhotonSkyRadiance[1], ECS::kPhotonSkyRadiance[2]);
}

/// Pure: combines the two components. Either may be null.
inline WeatherResolved resolveWeather(const ECS::WeatherComponent* weather,
                                      const ECS::VolumetricCloudsComponent* clouds) {
    WeatherResolved r;
    if (!weather || !weather->enabled) return r;

    const bool link = weather->linkSky != 0;
    const f32 rain = weather->rainEnabled ? std::clamp(weather->rainIntensity, 0.0f, 1.0f) : 0.0f;
    const bool cloudsOn = clouds && clouds->enabled && clouds->quality > 0;

    // ---- Clouds, thickened and darkened by rain.
    f32 coverage = 0.0f;
    f32 density = 0.0f;
    f32 darkness = 0.0f;
    if (cloudsOn) {
        coverage = std::clamp(clouds->coverage, 0.0f, 1.0f);
        density = std::max(clouds->density, 0.0f);
        darkness = std::clamp(clouds->darkness, 0.0f, 1.0f);
        if (link && rain > 0.0f) {
            coverage = std::max(coverage, 0.55f + 0.40f * rain);
            density *= 1.0f + 0.8f * rain;
            darkness = std::max(darkness, 0.75f * rain);
        }
        r.cloudsLinked = true;
        r.cloudCoverage = coverage;
        r.cloudDensity = density;
        r.cloudDarkness = darkness;
    }

    // ---- Fog.
    if (weather->fogEnabled && weather->fogDensity > 0.0f) {
        r.fog = true;
        r.fogDensity = std::max(weather->fogDensity, 0.0f) * (link ? 1.0f + rain : 1.0f);
        r.fogHeightFalloff = std::max(weather->fogHeightFalloff, 0.0f);
        r.fogBaseHeight = weather->fogBaseHeight;
        r.fogStart = std::max(weather->fogStart, 0.0f);
        r.fogMaxOpacity = std::clamp(weather->fogMaxOpacity, 0.0f, 1.0f);
        r.fogColor = Vec3(weatherSrgbToLinear(weather->fogColor.x),
                          weatherSrgbToLinear(weather->fogColor.y),
                          weatherSrgbToLinear(weather->fogColor.z));
        r.fogSkyBlend = std::clamp(weather->fogSkyBlend, 0.0f, 1.0f);
        r.fogSunScatter = std::max(weather->fogSunScatter, 0.0f);
        r.fogSunExponent = std::clamp(weather->fogSunExponent, 1.0f, 64.0f);
    }

    // ---- Rain.
    if (rain > 0.0f) {
        r.rain = true;
        r.rainIntensity = rain;
        r.rainDropLength = std::clamp(weather->rainDropLength, 0.0f, 1.0f);
        r.rainSpeed = std::max(weather->rainSpeed, 0.0f);
        r.rainBrightness = std::max(weather->rainBrightness, 0.0f);
        r.wetness = std::clamp(rain * std::clamp(weather->rainWetness, 0.0f, 1.0f), 0.0f, 1.0f);
    }

    // The wind blows whether or not it rains: foliage and the weather's sound read it too.
    r.rainWindSpeed = std::max(weather->rainWindSpeed, 0.0f);
    r.rainWindDirection = weather->rainWindDirection;
    r.rainSplashes = r.rain ? std::clamp(weather->rainSplashes, 0.0f, 1.0f) : 0.0f;
    r.windGust = std::clamp(weather->windGust, 0.0f, 1.0f);
    r.windFoliage = std::clamp(weather->windFoliage, 0.0f, 3.0f);
    if (cloudsOn) {
        r.cloudShadows = clouds->groundShadows;
        r.cloudShadowStrength = std::clamp(clouds->shadowStrength, 0.0f, 1.0f);
        r.silverLining = std::clamp(clouds->silverLining, 0.0f, 2.0f);
        r.sunShafts = std::clamp(clouds->sunShafts, 0.0f, 1.0f);
        // Rain's heavier clouds shade more.
        if (link && rain > 0.0f) r.cloudShadowStrength = std::max(r.cloudShadowStrength, 0.55f + 0.35f * rain);
    }

    r.active = r.fog || r.rain || r.cloudsLinked;
    if (!link) {
        r.lightning = weather->lightningEnabled;
    }
    r.thunder = weather->thunder;
    r.lightningRate = std::clamp(weather->lightningRate, 0.2f, 120.0f);
    r.lightningIntensity = std::clamp(weather->lightningIntensity, 0.0f, 2.0f);
    r.lightningDistanceMin = std::max(weather->lightningDistanceMin, 50.0f);
    r.lightningDistanceMax = std::max(weather->lightningDistanceMax, r.lightningDistanceMin);
    r.lightningSeed = weather->lightningSeed;
    r.audio = weather->audioEnabled;
    r.audioVolume = std::clamp(weather->audioVolume, 0.0f, 1.0f);
    if (!link) return r;

    // ---- What the sky does to the light.
    const f32 cloudCover = cloudsOn ? weatherSmoothstep(0.35f, 0.95f, coverage) *
                                          std::clamp(density / 0.35f, 0.0f, 1.0f)
                                    : 0.0f;
    const f32 rainCover = 0.85f * rain;
    r.overcast = std::max(cloudCover, rainCover);
    // Clouds dim the sun only when they are allowed to cast; rain always does.
    const f32 sunCover = std::max(cloudsOn && clouds->castShadows ? cloudCover : 0.0f, rainCover);
    r.sunScale = 1.0f - 0.8f * sunCover;
    r.skyScale = 1.0f + 0.25f * r.overcast;

    const Vec3 base = weatherBaseSky();
    const f32 baseLuma = 0.2126f * base.x + 0.7152f * base.y + 0.0722f * base.z;
    // Overcast: the sky tends to a neutral grey of the same brightness.
    Vec3 target(weatherLerp(base.x, baseLuma * 1.05f, r.overcast),
                weatherLerp(base.y, baseLuma * 1.05f, r.overcast),
                weatherLerp(base.z, baseLuma * 1.05f, r.overcast));
    // Thick fog hands its hue to the sky light, keeping the brightness.
    if (r.fog) {
        const f32 influence = std::clamp(r.fogDensity / 0.04f, 0.0f, 1.0f) * 0.6f;
        const f32 fogLuma =
            std::max(0.2126f * r.fogColor.x + 0.7152f * r.fogColor.y + 0.0722f * r.fogColor.z, 1e-4f);
        const f32 targetLuma = 0.2126f * target.x + 0.7152f * target.y + 0.0722f * target.z;
        const f32 k = targetLuma / fogLuma;
        target = Vec3(weatherLerp(target.x, r.fogColor.x * k, influence),
                      weatherLerp(target.y, r.fogColor.y * k, influence),
                      weatherLerp(target.z, r.fogColor.z * k, influence));
    }
    r.skyTint = Vec3(target.x / base.x, target.y / base.y, target.z / base.z);
    r.active = r.active || r.overcast > 0.0f;
    // Lightning needs a storm: with the link on it fires only under a heavy overcast.
    r.lightning = weather->lightningEnabled && r.overcast >= 0.4f;
    r.active = r.active || r.lightning;
    return r;
}

/// The first enabled, visible weather component in the scene.
inline const ECS::WeatherComponent* findActiveWeather(ECS::World& world) {
    const ECS::WeatherComponent* found = nullptr;
    ECS::ComponentQuery query;
    query.with<ECS::WeatherComponent>();
    world.forEach<ECS::WeatherComponent>(query, [&](ECS::Entity entity, ECS::WeatherComponent& w) {
        if (found || !w.enabled || isEffectivelyDisabled(world, entity)) return;
        found = &w;
    });
    return found;
}

/// The clouds the sky pass draws: the last enabled component (the renderer's own rule).
inline const ECS::VolumetricCloudsComponent* findActiveClouds(ECS::World& world) {
    const ECS::VolumetricCloudsComponent* found = nullptr;
    ECS::ComponentQuery query;
    query.with<ECS::VolumetricCloudsComponent>();
    world.forEach<ECS::VolumetricCloudsComponent>(
        query, [&](ECS::Entity, ECS::VolumetricCloudsComponent& c) {
            if (c.enabled && c.quality > 0) found = &c;
        });
    return found;
}

inline WeatherResolved resolveSceneWeather(ECS::World& world) {
    return resolveWeather(findActiveWeather(world), findActiveClouds(world));
}

}  // namespace Caffeine::Scene
