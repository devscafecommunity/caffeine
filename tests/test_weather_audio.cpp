#include "catch.hpp"

#include "../src/caffeine/audio/AmbienceSynth.hpp"
#include "../src/caffeine/audio/AudioService.hpp"
#include "../src/caffeine/audio/Spatial.hpp"
#include "../src/caffeine/audio/WeatherAudio.hpp"

#include <map>

using namespace Caffeine;
using namespace Caffeine::Audio;
using Catch::Detail::Approx;

namespace {

/// A device that records what it is asked to do.
class FakeSink final : public AudioSink {
public:
    struct Playing {
        std::string name;
        f32 volume = 0.0f;
        f32 pan = 0.0f;
        bool loop = false;
        bool live = true;
    };
    bool deviceReady = true;
    std::map<std::string, usize> registered;
    std::map<u32, Playing> playing;
    u32 next = 1;

    bool ready() const override { return deviceReady; }
    bool registerMono(const std::string& name, const std::vector<f32>& samples, u32) override {
        registered[name] = samples.size();
        return true;
    }
    u32 play(const std::string& name, f32 volume, f32 pan, bool loop) override {
        playing[next] = Playing{name, volume, pan, loop, true};
        return next++;
    }
    bool setVolume(u32 handle, f32 volume) override {
        auto it = playing.find(handle);
        if (it == playing.end() || !it->second.live) return false;
        it->second.volume = volume;
        return true;
    }
    bool stop(u32 handle) override {
        auto it = playing.find(handle);
        if (it == playing.end()) return false;
        it->second.live = false;
        return true;
    }
    usize liveCount(const std::string& prefix) const {
        usize n = 0;
        for (const auto& [_, p] : playing)
            if (p.live && p.name.rfind(prefix, 0) == 0) ++n;
        return n;
    }
};

}  // namespace

TEST_CASE("Spatial audio: gain falls with the distance and never rises", "[audio][spatial]") {
    CHECK(distanceGain(0.5f, 2.0f, 100.0f) == 1.0f);
    CHECK(distanceGain(2.0f, 2.0f, 100.0f) == 1.0f);
    CHECK(distanceGain(100.0f, 2.0f, 100.0f) == 0.0f);
    CHECK(distanceGain(400.0f, 2.0f, 100.0f) == 0.0f);
    f32 last = 1.0f;
    for (f32 d = 2.0f; d <= 100.0f; d += 1.0f) {
        const f32 g = distanceGain(d, 2.0f, 100.0f);
        CHECK(g <= last + 1e-6f);
        last = g;
    }
    // Inverse halves the gain for every doubling of the distance (6 dB), as the air does; linear falls straight.
    CHECK(distanceGain(4.0f, 2.0f, 100.0f, Rolloff::Inverse) == Approx(0.5f));
    CHECK(distanceGain(8.0f, 2.0f, 100.0f, Rolloff::Inverse) == Approx(0.25f));
    CHECK(distanceGain(51.0f, 2.0f, 100.0f, Rolloff::Linear) == Approx(0.5f).margin(0.01));
    CHECK(distanceGain(100.0f, 2.0f, 100.0f, Rolloff::Linear) == 0.0f);
    // A higher rolloff falls faster.
    CHECK(distanceGain(20.0f, 2.0f, 100.0f, Rolloff::Inverse, 3.0f) < distanceGain(20.0f, 2.0f, 100.0f, Rolloff::Inverse, 1.0f));
}

TEST_CASE("Spatial audio: a source on the right pans right, on the left pans left, and ahead stays centred", "[audio][spatial]") {
    Listener3D ears;  // at the origin, facing +Z, +Y up: the right hand is -X
    const SpatialParams right = spatialize(ears, Vec3(-10.0f, 0.0f, 0.0f), 1.0f, 100.0f);
    const SpatialParams left = spatialize(ears, Vec3(10.0f, 0.0f, 0.0f), 1.0f, 100.0f);
    const SpatialParams ahead = spatialize(ears, Vec3(0.0f, 0.0f, 10.0f), 1.0f, 100.0f);
    const SpatialParams behind = spatialize(ears, Vec3(0.0f, 0.0f, -10.0f), 1.0f, 100.0f);
    CHECK(right.pan == Approx(1.0f));
    CHECK(left.pan == Approx(-1.0f));
    CHECK(ahead.pan == Approx(0.0f).margin(1e-4));
    CHECK(behind.pan == Approx(0.0f).margin(1e-4));
    CHECK(right.distance == Approx(10.0f));
    CHECK(right.delay == Approx(10.0f / 343.0f));
    // Turning to face -X puts that source ahead.
    ears.forward = Vec3(-1.0f, 0.0f, 0.0f);
    CHECK(spatialize(ears, Vec3(-10.0f, 0.0f, 0.0f), 1.0f, 100.0f).pan == Approx(0.0f).margin(1e-4));
    // On top of the listener there is no side.
    Listener3D here;
    CHECK(spatialize(here, Vec3(0.0f, 0.0f, 0.0f), 1.0f, 100.0f).pan == 0.0f);
    CHECK(std::fabs(spatialize(here, Vec3(-0.1f, 0.0f, 0.0f), 1.0f, 100.0f).pan) < 0.2f);
}

TEST_CASE("Weather audio: the beds follow the rain and the wind, and are silent without them", "[audio][weather]") {
    WeatherSound quiet;
    CHECK(weatherLevels(quiet).rain == 0.0f);
    CHECK(weatherLevels(quiet).wind == 0.0f);
    WeatherSound rain;
    rain.rain = true;
    rain.rainIntensity = 0.2f;
    const f32 light = weatherLevels(rain).rain;
    rain.rainIntensity = 1.0f;
    const f32 heavy = weatherLevels(rain).rain;
    CHECK(light > 0.0f);
    CHECK(heavy > light);
    CHECK(heavy <= 1.0f);
    // The rain's intensity at 0 is silence even with the rain switched on.
    rain.rainIntensity = 0.0f;
    CHECK(weatherLevels(rain).rain == 0.0f);
    // Wind: stronger wind, louder; a gust swells it.
    WeatherSound wind;
    wind.windSpeed = 5.0f;
    const f32 breeze = weatherLevels(wind).wind;
    wind.windSpeed = 20.0f;
    const f32 gale = weatherLevels(wind).wind;
    wind.gust = 1.5f;
    const f32 gusting = weatherLevels(wind).wind;
    CHECK(gale > breeze);
    CHECK(gusting > gale);
    CHECK(gusting <= 1.0f);
}

TEST_CASE("Weather audio: thunder is panned to the strike", "[audio][weather]") {
    // Facing +Z (yaw 0): a strike at azimuth +90 degrees (toward +X) is on the left; -90 degrees on the right.
    CHECK(thunderPan(1.5707963f, 0.0f) == Approx(-1.0f));
    CHECK(thunderPan(-1.5707963f, 0.0f) == Approx(1.0f));
    CHECK(thunderPan(0.0f, 0.0f) == Approx(0.0f).margin(1e-5));
    // Turning to face the strike centres it.
    CHECK(thunderPan(1.5707963f, 1.5707963f) == Approx(0.0f).margin(1e-5));
}

TEST_CASE("Weather audio: the rain bed starts when it rains, follows the intensity and stops when it ends",
          "[audio][weather]") {
    FakeSink sink;
    WeatherAudio audio(sink);
    audio.setVolume(1.0f);
    WeatherAudio::Frame frame;
    frame.dt = 0.1f;
    // Dry: nothing plays, nothing is made.
    for (int i = 0; i < 10; ++i) audio.update(frame);
    CHECK(sink.playing.empty());
    CHECK(sink.registered.empty());
    // It rains: the bed is made once and plays looped, getting louder as the level rises.
    frame.weather.rain = true;
    frame.weather.rainIntensity = 0.9f;
    for (int i = 0; i < 40; ++i) audio.update(frame);
    REQUIRE(audio.rainPlaying());
    CHECK(sink.registered.count("weather:rain") == 1);
    CHECK(sink.liveCount("weather:rain") == 1);
    const u32 handle = 1;
    CHECK(sink.playing[handle].loop);
    const f32 loud = sink.playing[handle].volume;
    CHECK(loud > 0.3f);
    frame.weather.rainIntensity = 0.2f;
    for (int i = 0; i < 40; ++i) audio.update(frame);
    CHECK(sink.playing[handle].volume < loud);
    // The master volume scales it.
    audio.setVolume(0.25f);
    audio.update(frame);
    CHECK(sink.playing[handle].volume < 0.2f);
    // The rain stops: the bed fades out and is stopped, not left running at 0.
    frame.weather.rain = false;
    for (int i = 0; i < 200; ++i) audio.update(frame);
    CHECK_FALSE(audio.rainPlaying());
    CHECK(sink.liveCount("weather:rain") == 0);
    // It rains again: it starts again without making the clip twice.
    frame.weather.rain = true;
    for (int i = 0; i < 40; ++i) audio.update(frame);
    CHECK(audio.rainPlaying());
    CHECK(sink.registered.size() == 1);
}

TEST_CASE("Weather audio: each thunder event plays one clap, panned and scaled, from a cached clip", "[audio][weather][thunder]") {
    FakeSink sink;
    WeatherAudio audio(sink);
    audio.setVolume(1.0f);
    WeatherThunder close{300.0f, -1.5707963f, 0.9f};  // on the right
    WeatherThunder far{3500.0f, 1.5707963f, 0.2f};    // on the left, quieter
    WeatherAudio::Frame frame;
    WeatherThunder both[2] = {close, far};
    frame.thunder = both;
    frame.thunderCount = 2;
    audio.update(frame);
    CHECK(audio.thunderPlayed() == 2);
    CHECK(audio.thunderHeard() == 2);
    REQUIRE(sink.liveCount("weather:thunder") == 2);
    f32 closeVolume = 0.0f, farVolume = 0.0f, closePan = 0.0f, farPan = 0.0f;
    for (const auto& [_, p] : sink.playing) {
        CHECK_FALSE(p.loop);
        if (p.pan > 0.0f) {
            closePan = p.pan;
            closeVolume = p.volume;
        } else {
            farPan = p.pan;
            farVolume = p.volume;
        }
    }
    CHECK(closePan == Approx(1.0f));
    CHECK(farPan == Approx(-1.0f));
    CHECK(closeVolume == Approx(0.9f));
    CHECK(farVolume == Approx(0.2f));
    // A far clap is a longer clip than a close one.
    usize shortest = ~usize{0}, longest = 0;
    for (const auto& [name, count] : sink.registered) {
        shortest = std::min(shortest, count);
        longest = std::max(longest, count);
    }
    CHECK(longest > shortest);
    // Without a device nothing is played, but the claps are still counted as heard and the levels still follow.
    FakeSink deaf;
    deaf.deviceReady = false;
    WeatherAudio silent(deaf);
    frame.weather.rain = true;
    frame.weather.rainIntensity = 1.0f;
    for (int i = 0; i < 30; ++i) silent.update(frame);
    CHECK(deaf.playing.empty());
    CHECK(deaf.registered.empty());
    CHECK(silent.thunderPlayed() == 0);
    CHECK(silent.thunderHeard() == 60);
    CHECK(silent.rainLevel() > 0.3f);
}

TEST_CASE("Ambience synth: the loops are deterministic, seamless and bounded", "[audio][weather]") {
    const ThunderClip rain = synthesizeRainLoop(7);
    const ThunderClip again = synthesizeRainLoop(7);
    const ThunderClip other = synthesizeRainLoop(8);
    const ThunderClip wind = synthesizeWindLoop(11);
    CHECK(rain.samples == again.samples);
    CHECK_FALSE(rain.samples == other.samples);
    CHECK(rain.seconds() == Approx(6.0f).margin(0.05));
    CHECK(wind.seconds() == Approx(8.0f).margin(0.05));
    for (const ThunderClip* clip : {&rain, &wind}) {
        f32 peak = 0.0f;
        f64 energy = 0.0;
        for (f32 s : clip->samples) {
            peak = std::max(peak, std::fabs(s));
            energy += static_cast<f64>(s) * s;
        }
        CHECK(peak <= 1.0f);
        CHECK(peak > 0.5f);
        CHECK(energy / clip->samples.size() > 0.005);
        // Seamless: the step from the last sample to the first is no bigger than a typical step inside the clip.
        f64 typical = 0.0;
        for (usize i = 1; i < clip->samples.size(); ++i) typical += std::fabs(clip->samples[i] - clip->samples[i - 1]);
        typical /= static_cast<f64>(clip->samples.size() - 1);
        const f64 seam = std::fabs(clip->samples.front() - clip->samples.back());
        CHECK(seam < typical * 6.0 + 0.05);
    }
    // The wind is a lower sound than the rain: less change from sample to sample.
    auto roughness = [](const ThunderClip& c) {
        f64 diff = 0.0, level = 1e-9;
        for (usize i = 1; i < c.samples.size(); ++i) {
            diff += std::fabs(c.samples[i] - c.samples[i - 1]);
            level += std::fabs(c.samples[i]);
        }
        return diff / level;
    };
    CHECK(roughness(wind) < roughness(rain));
}

#if defined(CF_HAS_SDL3) && defined(CF_HAS_SCRIPTING)
TEST_CASE("AudioService: made-up samples register without a device and 3D calls are safe", "[audio][service]") {
    AudioService service;  // no init: no device
    CHECK_FALSE(service.ready());
    const std::vector<f32> tone(2205, 0.25f);
    CHECK(service.registerSamples("test:tone", tone, 22050, 1));
    CHECK_FALSE(service.registerSamples("", tone, 22050, 1));
    CHECK_FALSE(service.registerSamples("test:empty", {}, 22050, 1));
    // The same name again keeps the first clip (a looping source reads its memory).
    CHECK(service.registerSamples("test:tone", std::vector<f32>(10, 0.5f), 22050, 1));
    CHECK(service.duration("test:tone") == Approx(0.1f).margin(0.001));
    // Nothing plays without a device, and nothing crashes.
    service.setListener3D(Listener3D{});
    CHECK(service.playAt3D("test:tone", Vec3(1.0f, 0.0f, 5.0f)) == 0);
    CHECK_FALSE(service.setSourcePosition3D(1, Vec3(0.0f, 0.0f, 0.0f)));
}
#endif
