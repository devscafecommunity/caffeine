#include "catch.hpp"

#include "../src/caffeine/audio/ThunderSynth.hpp"
#include "../src/caffeine/ecs/World.hpp"
#include "../src/caffeine/ecs/WeatherComponents.hpp"
#include "../src/caffeine/scene/ComponentReflection.hpp"
#include "../src/caffeine/scene/EditorSceneSerializer.hpp"
#include "../src/caffeine/scene/WeatherSystem.hpp"
#include "caffeine/environment/VolumetricClouds.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::ECS;
namespace R = Caffeine::Scene::Reflection;

namespace {

WeatherComponent fogOnly() {
    WeatherComponent w;
    w.fogEnabled = true;
    return w;
}

WeatherComponent stormRain(f32 intensity = 1.0f) {
    WeatherComponent w;
    w.rainEnabled = true;
    w.rainIntensity = intensity;
    return w;
}

VolumetricCloudsComponent clouds(f32 coverage, f32 density = 0.35f) {
    VolumetricCloudsComponent c;
    c.enabled = true;
    c.coverage = coverage;
    c.density = density;
    return c;
}

f32 luma(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace

TEST_CASE("Weather: nothing is on by default, so a new component changes nothing", "[weather]") {
    const WeatherComponent fresh;
    CHECK(fresh.enabled);
    CHECK_FALSE(fresh.fogEnabled);
    CHECK_FALSE(fresh.rainEnabled);

    const Scene::WeatherResolved r = Scene::resolveWeather(&fresh, nullptr);
    CHECK_FALSE(r.active);
    CHECK_FALSE(r.fog);
    CHECK_FALSE(r.rain);
    CHECK(r.sunScale == 1.0f);
    CHECK(r.skyScale == 1.0f);
    CHECK(r.skyTint.x == 1.0f);
    CHECK(r.skyTint.y == 1.0f);
    CHECK(r.skyTint.z == 1.0f);
    CHECK(r.wetness == 0.0f);

    // No component at all is the same identity.
    const Scene::WeatherResolved none = Scene::resolveWeather(nullptr, nullptr);
    CHECK_FALSE(none.active);
    CHECK(none.sunScale == 1.0f);

    // The cloud defaults render as before: a tint of 1 and no darkness.
    const VolumetricCloudsComponent c;
    CHECK(c.tint.x == 1.0f);
    CHECK(c.darkness == 0.0f);
    CHECK(c.skyLight == 1.0f);
    CHECK_FALSE(c.enabled);
}

TEST_CASE("Weather: the master switch turns every section off", "[weather]") {
    WeatherComponent w = stormRain();
    w.fogEnabled = true;
    w.enabled = false;
    const VolumetricCloudsComponent c = clouds(0.9f);
    const Scene::WeatherResolved r = Scene::resolveWeather(&w, &c);
    CHECK_FALSE(r.active);
    CHECK_FALSE(r.rain);
    CHECK_FALSE(r.fog);
    CHECK(r.sunScale == 1.0f);
}

TEST_CASE("Weather: fog values are clamped and the colour is linear", "[weather]") {
    WeatherComponent w = fogOnly();
    w.fogDensity = 0.02f;
    w.fogMaxOpacity = 4.0f;
    w.fogSunExponent = 500.0f;
    w.fogColor = Vec3(1.0f, 0.5f, 0.0f);
    const Scene::WeatherResolved r = Scene::resolveWeather(&w, nullptr);
    REQUIRE(r.fog);
    CHECK(r.fogDensity == Approx(0.02f));
    CHECK(r.fogMaxOpacity == 1.0f);
    CHECK(r.fogSunExponent == 64.0f);
    CHECK(r.fogColor.x == Approx(1.0f));
    CHECK(r.fogColor.y == Approx(0.2140f).margin(0.001));  // sRGB 0.5 in linear light
    CHECK(r.fogColor.z == Approx(0.0f));

    // A fog with no density is not a fog.
    w.fogDensity = 0.0f;
    CHECK_FALSE(Scene::resolveWeather(&w, nullptr).fog);
}

TEST_CASE("Weather: rain wets surfaces in proportion to its intensity", "[weather]") {
    WeatherComponent w = stormRain(0.5f);
    w.rainWetness = 0.8f;
    CHECK(Scene::resolveWeather(&w, nullptr).wetness == Approx(0.4f));
    w.rainIntensity = 1.0f;
    w.rainWetness = 1.0f;
    CHECK(Scene::resolveWeather(&w, nullptr).wetness == Approx(1.0f));
    w.rainIntensity = 3.0f;  // out of range
    CHECK(Scene::resolveWeather(&w, nullptr).rainIntensity == 1.0f);
    w.rainEnabled = false;
    const Scene::WeatherResolved dry = Scene::resolveWeather(&w, nullptr);
    CHECK_FALSE(dry.rain);
    CHECK(dry.wetness == 0.0f);
}

TEST_CASE("Weather: rain thickens and darkens the clouds and thickens the fog when linked", "[weather]") {
    WeatherComponent w = stormRain(1.0f);
    w.fogEnabled = true;
    w.fogDensity = 0.01f;
    const VolumetricCloudsComponent c = clouds(0.3f, 0.3f);

    const Scene::WeatherResolved linked = Scene::resolveWeather(&w, &c);
    REQUIRE(linked.cloudsLinked);
    CHECK(linked.cloudCoverage > 0.9f);
    CHECK(linked.cloudDensity > 0.3f * 1.5f);
    CHECK(linked.cloudDarkness >= 0.7f);
    CHECK(linked.fogDensity == Approx(0.02f));

    w.linkSky = false;
    const Scene::WeatherResolved alone = Scene::resolveWeather(&w, &c);
    CHECK(alone.cloudCoverage == Approx(0.3f));
    CHECK(alone.cloudDensity == Approx(0.3f));
    CHECK(alone.cloudDarkness == 0.0f);
    CHECK(alone.fogDensity == Approx(0.01f));
    CHECK(alone.sunScale == 1.0f);
    CHECK(alone.skyTint.x == 1.0f);
}

TEST_CASE("Weather: an overcast sky dims the sun and greys Photon's sky light", "[weather]") {
    WeatherComponent w;
    w.fogEnabled = false;

    const VolumetricCloudsComponent clear = clouds(0.1f);
    const VolumetricCloudsComponent overcast = clouds(1.0f, 0.6f);
    const Scene::WeatherResolved a = Scene::resolveWeather(&w, &clear);
    const Scene::WeatherResolved b = Scene::resolveWeather(&w, &overcast);
    CHECK(a.overcast == 0.0f);
    CHECK(a.sunScale == 1.0f);
    CHECK(b.overcast > 0.9f);
    CHECK(b.sunScale < 0.3f);
    CHECK(b.skyScale > a.skyScale);

    // Greyer: the blue shrinks relative to the red, the brightness stays about the same.
    const Vec3 base = Scene::weatherBaseSky();
    const Vec3 tinted(base.x * b.skyTint.x, base.y * b.skyTint.y, base.z * b.skyTint.z);
    CHECK(tinted.z / tinted.x < base.z / base.x);
    CHECK(luma(tinted) == Approx(luma(base)).epsilon(0.1));

    // Clouds that may not cast leave the sun alone; the sky still greys.
    VolumetricCloudsComponent noShadow = overcast;
    noShadow.castShadows = false;
    const Scene::WeatherResolved c = Scene::resolveWeather(&w, &noShadow);
    CHECK(c.sunScale == 1.0f);
    CHECK(c.overcast > 0.9f);

    // The sun never goes out entirely.
    CHECK(b.sunScale > 0.0f);
}

TEST_CASE("Weather: thick fog gives the sky light its hue", "[weather]") {
    WeatherComponent w = fogOnly();
    w.fogColor = Vec3(0.9f, 0.55f, 0.35f);  // a warm haze
    w.fogDensity = 0.001f;
    const Scene::WeatherResolved thin = Scene::resolveWeather(&w, nullptr);
    w.fogDensity = 0.05f;
    const Scene::WeatherResolved thick = Scene::resolveWeather(&w, nullptr);
    const Vec3 base = Scene::weatherBaseSky();
    // Warmer than the clear sky, and more so as the fog thickens.
    CHECK(thick.skyTint.x / thick.skyTint.z > thin.skyTint.x / thin.skyTint.z);
    CHECK(thick.skyTint.x * base.x / (thick.skyTint.z * base.z) > base.x / base.z);
}

TEST_CASE("Weather: the scene picks the first enabled weather and skips disabled entities", "[weather]") {
    World world;
    Entity off = world.create("Off");
    auto& disabled = world.add<WeatherComponent>(off);
    disabled.enabled = false;
    disabled.rainEnabled = true;
    Entity on = world.create("On");
    auto& active = world.add<WeatherComponent>(on);
    active.rainEnabled = true;
    active.rainIntensity = 0.25f;
    Entity later = world.create("Later");
    world.add<WeatherComponent>(later).fogEnabled = true;

    const WeatherComponent* found = Scene::findActiveWeather(world);
    REQUIRE(found != nullptr);
    CHECK(found->rainIntensity == Approx(0.25f));

    const Scene::WeatherResolved r = Scene::resolveSceneWeather(world);
    CHECK(r.rain);
    CHECK_FALSE(r.fog);

    World empty;
    CHECK(Scene::findActiveWeather(empty) == nullptr);
    CHECK_FALSE(Scene::resolveSceneWeather(empty).active);
}

TEST_CASE("Weather and clouds are reflected, so the editor and scripts can edit them", "[weather][reflection]") {
    const R::ComponentDesc* weather = R::findByName("Weather");
    const R::ComponentDesc* cloudsDesc = R::findByName("Volumetric Clouds");
    REQUIRE(weather != nullptr);
    REQUIRE(cloudsDesc != nullptr);

    for (const char* name : {"Fog Enabled", "Fog Density", "Fog Height Falloff", "Fog Color", "Rain Enabled",
                             "Rain Intensity", "Rain Wetness", "Link Sky"}) {
        INFO(name);
        CHECK(R::findField(*weather, name) != nullptr);
    }
    for (const char* name : {"Enabled", "Coverage", "Density", "Wind Speed", "Tint", "Darkness", "Sky Light"}) {
        INFO(name);
        CHECK(R::findField(*cloudsDesc, name) != nullptr);
    }
    // Padding and reserved bytes are not exposed.
    CHECK(R::findField(*cloudsDesc, "_reserved") == nullptr);

    World world;
    Entity e = world.create("Sky");
    weather->add(world, e);
    const R::FieldDesc* density = R::findField(*weather, "Fog Density");
    REQUIRE(density != nullptr);
    R::FieldValue value;
    value.kind = R::FieldKind::F32;
    value.vec[0] = 0.07f;
    REQUIRE(density->set(world, e, value));
    CHECK(world.get<WeatherComponent>(e)->fogDensity == Approx(0.07f));

    const R::FieldDesc* colour = R::findField(*weather, "Fog Color");
    REQUIRE(colour != nullptr);
    CHECK(colour->kind == R::FieldKind::Vec3);
    R::FieldValue read;
    REQUIRE(colour->get(world, e, read));
    CHECK(read.vec[2] == Approx(0.76f));
}

TEST_CASE("Weather and clouds survive a scene round trip; older cloud scenes keep the new defaults", "[weather][scene]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "caffeine_weather_scene_test.caf";
    {
        World world;
        Entity e = world.create("Sky");
        auto& w = world.add<WeatherComponent>(e);
        w.fogEnabled = true;
        w.rainEnabled = true;
        w.fogDensity = 0.031f;
        w.rainIntensity = 0.7f;
        w.fogColor = Vec3(0.1f, 0.2f, 0.3f);
        auto& c = world.add<VolumetricCloudsComponent>(e);
        c.enabled = true;
        c.tint = Vec3(1.0f, 0.8f, 0.6f);
        c.darkness = 0.4f;
        Editor::SceneSerializer serializer(world);
        REQUIRE(serializer.serialize(path.string()));
    }
    World loaded;
    Editor::SceneSerializer serializer(loaded);
    REQUIRE(serializer.deserialize(path.string()));
    const WeatherComponent* w = Scene::findActiveWeather(loaded);
    REQUIRE(w != nullptr);
    CHECK(w->fogEnabled);
    CHECK(w->rainEnabled);
    CHECK(w->fogDensity == Approx(0.031f));
    CHECK(w->rainIntensity == Approx(0.7f));
    CHECK(w->fogColor.z == Approx(0.3f));
    const VolumetricCloudsComponent* c = Scene::findActiveClouds(loaded);
    REQUIRE(c != nullptr);
    CHECK(c->tint.y == Approx(0.8f));
    CHECK(c->darkness == Approx(0.4f));
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // A scene written before the fields were appended stores a shorter prefix: they keep their defaults.
    VolumetricCloudsComponent full = clouds(0.8f);
    full.tint = Vec3(0.1f, 0.1f, 0.1f);
    full.darkness = 0.9f;
    VolumetricCloudsComponent old;
    std::memcpy(&old, &full, offsetof(VolumetricCloudsComponent, tint));
    CHECK(old.coverage == Approx(0.8f));
    CHECK(old.tint.x == 1.0f);
    CHECK(old.darkness == 0.0f);
    CHECK(old.skyLight == 1.0f);
}

TEST_CASE("Weather: the wind gusts around its mean and never drops out", "[weather][wind]") {
    CHECK(Scene::windGustFactor(3.0f, 0.0f) == Approx(1.0f));  // no gustiness, no surge
    f32 sum = 0.0f;
    f32 low = 10.0f;
    f32 high = 0.0f;
    const int samples = 6000;
    for (int i = 0; i < samples; ++i) {
        const f32 g = Scene::windGustFactor(static_cast<f32>(i) * 0.05f, 0.8f);
        sum += g;
        low = std::min(low, g);
        high = std::max(high, g);
        CHECK(g >= 0.15f);
    }
    CHECK(sum / samples == Approx(1.0f).margin(0.1));  // surges and lulls balance around the mean
    CHECK(high > 1.5f);
    CHECK(low < 0.6f);
    CHECK(Scene::windGustFactor(1.0f, 0.5f) == Scene::windGustFactor(1.0f, 0.5f));  // deterministic
}

TEST_CASE("Weather: lightning is deterministic, spread by the rate and bounded by the distances", "[weather][lightning]") {
    Scene::LightningSettings settings;
    settings.rate = 12.0f;  // one a slot of 5 s
    settings.seed = 42;
    const f32 slot = 60.0f / 12.0f;
    for (i64 k = 0; k < 40; ++k) {
        const auto a = Scene::lightningStrike(settings, k);
        const auto b = Scene::lightningStrike(settings, k);
        CHECK(a.start == b.start);
        CHECK(a.azimuth == b.azimuth);
        CHECK(a.start >= static_cast<f32>(k) * slot);
        CHECK(a.start < static_cast<f32>(k + 1) * slot);
        CHECK(a.distance >= settings.distanceMin * 0.999f);
        CHECK(a.distance <= settings.distanceMax * 1.001f);
        CHECK(a.azimuth >= 0.0f);
        CHECK(a.azimuth < 6.2832f);
        CHECK(a.strength > 0.0f);
        CHECK(a.pulses[1] > a.pulses[0]);
        CHECK(a.pulses[2] > a.pulses[1]);
    }
    // Another seed strikes elsewhere and at other times.
    Scene::LightningSettings other = settings;
    other.seed = 43;
    CHECK(Scene::lightningStrike(settings, 7).start != Scene::lightningStrike(other, 7).start);
    CHECK(Scene::lightningStrike(settings, 7).azimuth != Scene::lightningStrike(other, 7).azimuth);
    // A higher rate packs strikes closer together.
    Scene::LightningSettings fast = settings;
    fast.rate = 60.0f;
    CHECK(Scene::lightningStrike(fast, 10).start < 12.0f);
}

TEST_CASE("Weather: a flash peaks at the strike, fades within a second and has no light before it", "[weather][lightning]") {
    Scene::LightningSettings settings;
    settings.rate = 6.0f;
    settings.seed = 7;
    settings.intensity = 1.0f;
    const auto strike = Scene::lightningStrike(settings, 3);
    CHECK(Scene::lightningAt(settings, strike.start - 0.01f).flash == Approx(0.0f).margin(1e-4));
    const auto at = Scene::lightningAt(settings, strike.start + 0.001f);
    CHECK(at.flash > strike.strength);  // the peak is a full stroke plus the afterglow, scaled by the distance
    CHECK(at.bolt > 0.5f);
    CHECK(at.strike.start == Approx(strike.start));
    // It fades: late in the strike's life nothing remains of the bolt and little of the flash.
    const auto late = Scene::lightningAt(settings, strike.start + 1.5f);
    CHECK(late.bolt < 0.01f);
    CHECK(late.flash < at.flash * 0.5f);
    // The return strokes flicker: the flash rises again at a later pulse.
    const f32 trough = Scene::lightningAt(settings, strike.start + strike.pulses[1] - 0.005f).flash;
    const f32 second = Scene::lightningAt(settings, strike.start + strike.pulses[1] + 0.003f).flash;
    CHECK(second > trough);
    // No intensity, no flash; a time before everything is dark.
    Scene::LightningSettings off = settings;
    off.intensity = 0.0f;
    CHECK(Scene::lightningAt(off, strike.start + 0.001f).flash == 0.0f);
    CHECK(Scene::lightningAt(settings, -5.0f).flash == 0.0f);
    // A closer strike is brighter than a far one (same seed and slot, other range).
    Scene::LightningSettings near = settings;
    near.distanceMin = 100.0f;
    near.distanceMax = 120.0f;
    Scene::LightningSettings far = settings;
    far.distanceMin = 4000.0f;
    far.distanceMax = 4200.0f;
    CHECK(Scene::lightningStrike(near, 3).strength > Scene::lightningStrike(far, 3).strength * 2.0f);
}

TEST_CASE("Weather: thunder arrives after the sound's travel time, once", "[weather][lightning]") {
    Scene::LightningSettings settings;
    settings.rate = 10.0f;
    settings.seed = 5;
    // Walk the clock in steps: every thunderclap is found in exactly one window.
    std::vector<Scene::ThunderEvent> all;
    for (f32 t = 0.0f; t < 120.0f; t += 0.37f) {
        Scene::ThunderEvent events[8];
        const u32 n = Scene::thunderBetween(settings, t, t + 0.37f, events, 8);
        for (u32 i = 0; i < std::min(n, 8u); ++i) all.push_back(events[i]);
    }
    REQUIRE(all.size() >= 15);  // 10 a minute for two minutes, minus the ones still travelling at the end
    for (size_t i = 1; i < all.size(); ++i) CHECK(all[i].arrival > all[i - 1].arrival);  // no repeats
    for (const auto& e : all) {
        // Its slot's strike plus distance / 343 gives the arrival.
        const i64 slot = static_cast<i64>(std::floor((e.arrival - e.distance / Scene::kSpeedOfSound) / 6.0f));
        const auto strike = Scene::lightningStrike(settings, slot);
        CHECK(e.arrival == Approx(strike.start + strike.distance / Scene::kSpeedOfSound).margin(1e-3));
        CHECK(e.volume > 0.0f);
        CHECK(e.volume <= 1.0f);
    }
    // A farther strike is later and quieter than a close one.
    Scene::LightningSettings close = settings;
    close.distanceMin = 100.0f;
    close.distanceMax = 110.0f;
    Scene::LightningSettings distant = settings;
    distant.distanceMin = 3000.0f;
    distant.distanceMax = 3100.0f;
    Scene::ThunderEvent a{};
    Scene::ThunderEvent b{};
    REQUIRE(Scene::thunderBetween(close, 0.0f, 30.0f, &a, 1) >= 1);
    REQUIRE(Scene::thunderBetween(distant, 0.0f, 40.0f, &b, 1) >= 1);
    CHECK(b.arrival - Scene::lightningStrike(distant, 0).start > a.arrival - Scene::lightningStrike(close, 0).start + 5.0f);
    CHECK(b.volume < a.volume);
    // An empty or backwards window finds nothing.
    CHECK(Scene::thunderBetween(settings, 5.0f, 5.0f, nullptr, 0) == 0);
    CHECK(Scene::thunderBetween(settings, 9.0f, 3.0f, nullptr, 0) == 0);
}

TEST_CASE("Weather: lightning needs a storm when linked, and the new sections resolve their settings", "[weather][lightning]") {
    WeatherComponent w;
    w.lightningEnabled = true;
    w.lightningRate = 9.0f;
    w.lightningSeed = 77;
    // A clear sky: nothing fires.
    const VolumetricCloudsComponent clear = clouds(0.1f);
    CHECK_FALSE(Scene::resolveWeather(&w, &clear).lightning);
    // Heavy cloud: it does, with the settings handed over.
    VolumetricCloudsComponent heavy = clouds(1.0f, 0.7f);
    const Scene::WeatherResolved storm = Scene::resolveWeather(&w, &heavy);
    CHECK(storm.lightning);
    CHECK(storm.lightningRate == Approx(9.0f));
    CHECK(storm.lightningSeed == 77u);
    CHECK(storm.active);
    // Rain alone makes an overcast sky when linked.
    WeatherComponent rainy = stormRain(1.0f);
    rainy.lightningEnabled = true;
    CHECK(Scene::resolveWeather(&rainy, nullptr).lightning);
    // Unlinked, it fires whatever the sky.
    w.linkSky = false;
    CHECK(Scene::resolveWeather(&w, &clear).lightning);
    // Off by default.
    const WeatherComponent plainStorm = stormRain(1.0f);
    CHECK_FALSE(Scene::resolveWeather(&plainStorm, &heavy).lightning);

    // Splashes, gusts and the foliage multiplier are clamped; splashes exist only while it rains.
    WeatherComponent wind;
    wind.rainEnabled = true;
    wind.rainSplashes = 4.0f;
    wind.windGust = -1.0f;
    wind.windFoliage = 9.0f;
    const Scene::WeatherResolved r = Scene::resolveWeather(&wind, nullptr);
    CHECK(r.rainSplashes == 1.0f);
    CHECK(r.windGust == 0.0f);
    CHECK(r.windFoliage == 3.0f);
    wind.rainEnabled = false;
    CHECK(Scene::resolveWeather(&wind, nullptr).rainSplashes == 0.0f);

    // Cloud shadows and the silver lining come from the clouds, and rain deepens the shadow when linked.
    VolumetricCloudsComponent shading = clouds(0.6f);
    shading.groundShadows = true;
    shading.shadowStrength = 0.3f;
    shading.silverLining = 5.0f;
    shading.sunShafts = 0.4f;
    WeatherComponent calm;
    calm.fogEnabled = false;
    const Scene::WeatherResolved shaded = Scene::resolveWeather(&calm, &shading);
    CHECK(shaded.cloudShadows);
    CHECK(shaded.cloudShadowStrength == Approx(0.3f));
    CHECK(shaded.silverLining == 2.0f);
    CHECK(shaded.sunShafts == Approx(0.4f));
    const WeatherComponent rainStorm = stormRain(1.0f);
    CHECK(Scene::resolveWeather(&rainStorm, &shading).cloudShadowStrength > 0.8f);
}


TEST_CASE("Thunder synth: deterministic, longer and duller with the distance, and a readable WAV", "[weather][thunder]") {
    using namespace Caffeine::Audio;
    const ThunderClip near = synthesizeThunder(300.0f, 5);
    const ThunderClip again = synthesizeThunder(300.0f, 5);
    const ThunderClip other = synthesizeThunder(300.0f, 6);
    const ThunderClip far = synthesizeThunder(3500.0f, 5);
    REQUIRE_FALSE(near.samples.empty());
    CHECK(near.samples == again.samples);
    CHECK_FALSE(near.samples == other.samples);
    CHECK(far.seconds() > near.seconds());
    f32 peak = 0.0f;
    for (f32 s : near.samples) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.5f);
    CHECK(peak <= 1.0f);
    // The distance takes the highs: the far clip has less energy in the sample-to-sample change.
    auto roughness = [](const ThunderClip& c) {
        f64 diff = 0.0;
        f64 level = 1e-9;
        for (size_t i = 1; i < c.samples.size(); ++i) {
            diff += std::fabs(c.samples[i] - c.samples[i - 1]);
            level += std::fabs(c.samples[i]);
        }
        return diff / level;
    };
    CHECK(roughness(far) < roughness(near));
    // The WAV is written and the SDL loader that AudioService uses can read it back.
    const auto path = (std::filesystem::temp_directory_path() / "caffeine_thunder_test.wav").string();
    REQUIRE(writeWav16(path, near));
    CHECK(std::filesystem::file_size(path) == 44 + near.samples.size() * 2);
    std::filesystem::remove(path);
}
