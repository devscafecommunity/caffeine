#include "catch.hpp"

#include "../src/caffeine/audio/AudioComponents.hpp"
#include "../src/caffeine/audio/ThunderSynth.hpp"
#include "../src/caffeine/audio/VolumetricComponents.hpp"
#include "../src/caffeine/audio/volumetric/SceneAudio.hpp"
#include "../src/caffeine/ecs/Components.hpp"
#include "../src/caffeine/ecs/World.hpp"

#include <filesystem>

#if defined(CF_HAS_SDL3)

using namespace Caffeine;
using namespace Caffeine::Audio;

namespace {

/// A steady tone as a WAV the emitters can load.
std::string writeTone(const char* name) {
    ThunderClip clip;
    clip.sampleRate = 22050;
    for (u32 i = 0; i < 22050; ++i) clip.samples.push_back(0.4f * std::sin(6.28318530718f * 440.0f * static_cast<f32>(i) / 22050.0f));
    const auto path = (std::filesystem::temp_directory_path() / name).string();
    REQUIRE(writeWav16(path, clip));
    return path;
}

f32 level(VolumetricAudio& audio, f32 seconds) {
    std::vector<f32> out(static_cast<usize>(seconds * 48000) * 2);
    const u32 frames = audio.renderOffline(out.data(), static_cast<u32>(out.size() / 2));
    f64 sum = 0.0;
    for (u32 i = frames / 2; i < frames; ++i) sum += static_cast<f64>(out[i * 2]) * out[i * 2] + static_cast<f64>(out[i * 2 + 1]) * out[i * 2 + 1];
    return static_cast<f32>(std::sqrt(sum / std::max(1u, frames / 2)));
}

ECS::Entity emitter(ECS::World& world, const std::string& clip, const Vec3& at) {
    ECS::Entity e = world.create();
    world.add<ECS::Transform>(e).position = at;
    auto& em = world.add<AudioEmitter>(e);
    em.clipPath = FixedString<128>(clip.c_str());
    em.loop = true;
    return e;
}

}  // namespace

TEST_CASE("Scene audio: emitters become voices, obstacles block them, and without the component nothing plays", "[audio][volumetric][scene]") {
    const std::string tone = writeTone("caffeine_scene_audio_tone.wav");
    ECS::World world;
    VolumetricAudio audio(48000);
    SceneAudio scene(audio);
    emitter(world, tone, {0, 1.5f, 12});

    // No Volumetric Audio component: nothing is mixed.
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK_FALSE(scene.enabled());
    CHECK(scene.voices().empty());

    ECS::Entity settings = world.create();
    world.add<VolumetricAudioComponent>(settings);
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    REQUIRE(scene.enabled());
    REQUIRE(scene.voices().size() == 1);
    CHECK(scene.voices()[0].loaded);
    CHECK(scene.missingClips() == 0);
    const f32 clear = level(audio, 0.6f);
    CHECK(clear > 0.001f);
    CHECK(scene.voices()[0].playing);
    CHECK_FALSE(scene.voices()[0].debug.blocked);

    // A wall (a cube entity, scaled 400 x 40 x 0.4: too wide to bend round) between the emitter and the listener.
    ECS::Entity wall = world.create();
    auto& t = world.add<ECS::Transform>(wall);
    t.position = {0, 1.5f, 6};
    t.scale = {400, 40, 0.4f};
    world.add<AcousticObstacle>(wall);
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(scene.obstacleCount() == 1);
    const f32 muffled = level(audio, 0.6f);
    CHECK(scene.voices()[0].debug.blocked);
    CHECK(muffled < clear * 0.5f);

    // Turning the wall's component off lets it through again.
    world.get<AcousticObstacle>(wall)->enabled = false;
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(scene.obstacleCount() == 0);
    CHECK(level(audio, 0.6f) > muffled * 2.0f);

    // A clip that does not exist is counted, not fatal.
    emitter(world, "no/such/clip.wav", {5, 1.5f, 5});
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(scene.missingClips() == 1);

    // Settings: Doppler scale, layer volumes and master volume reach the mixer.
    auto* c = world.get<VolumetricAudioComponent>(settings);
    c->masterVolume = 0.0f;
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(level(audio, 0.4f) < 1e-6f);

    // Switched off: every voice stops.
    c->enabled = false;
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(scene.voices().empty());
    std::filesystem::remove(tone);
}

TEST_CASE("Scene audio: an emitter that moves is followed, and removing it stops its voice", "[audio][volumetric][scene]") {
    const std::string tone = writeTone("caffeine_scene_audio_move.wav");
    ECS::World world;
    VolumetricAudio audio(48000);
    SceneAudio scene(audio);
    world.add<VolumetricAudioComponent>(world.create());
    ECS::Entity e = emitter(world, tone, {0, 1.5f, 5});
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    level(audio, 0.2f);
    const f32 near = scene.voices()[0].debug.distance;
    world.get<ECS::Transform>(e)->position = {0, 1.5f, 80};
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    level(audio, 0.2f);
    CHECK(scene.voices()[0].debug.distance > near * 10.0f);
    world.destroy(e);
    scene.sync(world, {0, 1.5f, 0}, {0, 0, 1});
    CHECK(scene.voices().empty());
    std::filesystem::remove(tone);
}


TEST_CASE("Scene audio: the weather is played through the mixer, so a roof muffles it and a clap comes from the sky", "[audio][volumetric][scene][weather]") {
    ECS::World world;
    VolumetricAudio audio(48000);
    SceneAudio scene(audio);
    world.add<VolumetricAudioComponent>(world.create());
    const Vec3 ears{0, 1.5f, 0};
    SceneAudio::WeatherFeed storm;
    storm.enabled = true;
    storm.volume = 1.0f;
    storm.rain = true;
    storm.rainIntensity = 0.9f;
    storm.windSpeed = 14.0f;
    auto frames = [&](int n, const SceneAudio::WeatherFeed& feed, const SceneAudio::ThunderClap* claps = nullptr, u32 count = 0) {
        for (int i = 0; i < n; ++i) {
            scene.sync(world, ears, {0, 0, 1}, 1.0f / 60.0f);
            scene.feedWeather(feed, i == 0 ? claps : nullptr, i == 0 ? count : 0, 1.0f / 60.0f);
            std::vector<f32> block(800 * 2);
            audio.renderOffline(block.data(), 800);
        }
    };

    // Not asked: no beds.
    SceneAudio::WeatherFeed quiet = storm;
    quiet.enabled = false;
    frames(30, quiet);
    CHECK_FALSE(scene.weatherRainPlaying());
    // Asked: rain and wind beds start and follow the weather.
    frames(120, storm);
    CHECK(scene.weatherRainPlaying());
    CHECK(scene.weatherWindPlaying());
    CHECK(scene.weatherRainLevel() > 0.3f);
    const f32 outdoors = level(audio, 0.5f);
    CHECK(outdoors > 0.01f);

    // A roof over the listener: the ambient layer is muffled and quieter (sky exposure 0).
    ECS::Entity roof = world.create();
    auto& t = world.add<ECS::Transform>(roof);
    t.position = {0, 3.0f, 0};
    t.scale = {200, 0.3f, 200};
    world.add<AcousticObstacle>(roof);
    frames(240, storm);
    const f32 indoors = level(audio, 0.5f);
    INFO("outdoors " << outdoors << " indoors " << indoors);
    CHECK(indoors < outdoors * 0.5f);
    world.destroy(roof);
    frames(240, storm);

    // A clap: a sound in the sky at its bearing, heard at once (it has already travelled). Mirror strikes, one at +90 degrees
    // (towards +X, the listener's left when facing +Z) and one at -90, are balanced oppositely between the ears.
    auto balance = [&](f32 azimuth) {
        ECS::World w;
        VolumetricAudio a(48000);
        SceneAudio sc(a);
        w.add<VolumetricAudioComponent>(w.create());
        SceneAudio::WeatherFeed feed;
        feed.enabled = true;
        feed.volume = 1.0f;
        SceneAudio::ThunderClap c{5000.0f, azimuth, 1.0f};
        sc.sync(w, ears, {0, 0, 1}, 1.0f / 60.0f);
        sc.feedWeather(feed, &c, 1, 1.0f / 60.0f);
        CHECK(sc.thunderPlayed() == 1);
        std::vector<f32> out(12000 * 2);
        a.renderOffline(out.data(), 12000);
        f64 left = 0.0, right = 0.0;
        for (u32 i = 0; i < 12000; ++i) {
            left += static_cast<f64>(out[i * 2]) * out[i * 2];
            right += static_cast<f64>(out[i * 2 + 1]) * out[i * 2 + 1];
        }
        CHECK(left + right > 1e-6);
        return static_cast<f32>(left / std::max(right, 1e-12));
    };
    CHECK(balance(1.5707963f) > balance(-1.5707963f) * 1.15f);
}

#endif
