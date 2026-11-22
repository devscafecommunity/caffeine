#include "catch.hpp"

#include "../src/caffeine/audio/volumetric/VolumetricMixer.hpp"

#include <cmath>
#include <functional>

using namespace Caffeine;
using namespace Caffeine::Audio;
using Catch::Detail::Approx;

namespace {

constexpr u32 kRate = 44100;
constexpr u32 kBlock = 256;

VolumetricClipPtr sine(f32 hz, f32 seconds = 1.0f) {
    auto clip = std::make_shared<VolumetricClip>();
    clip->sampleRate = kRate;
    const u32 frames = static_cast<u32>(seconds * kRate);
    // A whole number of cycles, so the loop has no seam.
    const f32 cycles = std::round(hz * seconds);
    const f32 exact = cycles / seconds;
    clip->samples.resize(frames);
    for (u32 i = 0; i < frames; ++i) clip->samples[i] = 0.5f * std::sin(6.28318530718f * exact * static_cast<f32>(i) / kRate);
    return clip;
}

VolumetricClipPtr noise(f32 seconds = 2.0f, u32 seed = 1) {
    auto clip = std::make_shared<VolumetricClip>();
    clip->sampleRate = kRate;
    const u32 frames = static_cast<u32>(seconds * kRate);
    clip->samples.resize(frames);
    u32 s = seed * 2654435761u + 1u;
    for (u32 i = 0; i < frames; ++i) {
        s = s * 1664525u + 1013904223u;
        clip->samples[i] = 0.5f * (static_cast<f32>(s >> 8) / 8388608.0f - 1.0f);
    }
    return clip;
}

VolumetricClipPtr impulse() {
    auto clip = std::make_shared<VolumetricClip>();
    clip->sampleRate = kRate;
    clip->samples.assign(kRate / 2, 0.0f);
    for (u32 i = 0; i < 8; ++i) clip->samples[10 + i] = 0.9f;
    return clip;
}

struct Rendered {
    std::vector<f32> left, right;
    f32 rms(usize from, usize to, bool useRight = false) const {
        const auto& v = useRight ? right : left;
        to = std::min(to, v.size());
        f64 sum = 0.0;
        for (usize i = from; i < to; ++i) sum += static_cast<f64>(v[i]) * v[i];
        return to > from ? static_cast<f32>(std::sqrt(sum / static_cast<f64>(to - from))) : 0.0f;
    }
    /// How rough the signal is: mean absolute step over mean absolute level (high for bright sound).
    f32 brightness(usize from, usize to) const {
        to = std::min(to, left.size());
        f64 diff = 0.0, level = 1e-12;
        for (usize i = from + 1; i < to; ++i) {
            diff += std::fabs(left[i] - left[i - 1]);
            level += std::fabs(left[i]);
        }
        return static_cast<f32>(diff / level);
    }
    /// Frequency by counting upward zero crossings.
    f32 frequency(usize from, usize to) const {
        to = std::min(to, left.size());
        u32 crossings = 0;
        for (usize i = from + 1; i < to; ++i)
            if (left[i - 1] < 0.0f && left[i] >= 0.0f) ++crossings;
        return static_cast<f32>(crossings) * kRate / static_cast<f32>(to - from);
    }
};

/// Renders `seconds` in blocks, calling `step(timeSeconds)` before each (to move things).
Rendered render(VolumetricMixer& mixer, f32 seconds, const std::function<void(f32)>& step = {}) {
    Rendered out;
    std::vector<f32> block(kBlock * 2);
    const u32 blocks = static_cast<u32>(seconds * kRate / kBlock);
    for (u32 b = 0; b < blocks; ++b) {
        if (step) step(static_cast<f32>(b * kBlock) / kRate);
        mixer.render(block.data(), kBlock);
        for (u32 i = 0; i < kBlock; ++i) {
            out.left.push_back(block[i * 2]);
            out.right.push_back(block[i * 2 + 1]);
        }
    }
    return out;
}

VoiceParams at(const VolumetricClipPtr& clip, const Vec3& position, bool loop = true) {
    VoiceParams p;
    p.clip = clip;
    p.position = position;
    p.loop = loop;
    return p;
}

}  // namespace

TEST_CASE("Volumetric audio: distance halves the level and the sound arrives c metres a second late", "[audio][volumetric]") {
    const auto tone = sine(500.0f);
    auto level = [&](f32 distance) {
        VolumetricMixer mixer(kRate);
        mixer.setListener({0, 0, 0}, {0, 0, 1});
        mixer.play(at(tone, {0, 0, distance}));
        const Rendered r = render(mixer, 0.6f);
        return r.rms(kRate / 4, kRate / 2);
    };
    const f32 near = level(10.0f), far = level(20.0f);
    CHECK(far == Approx(near * 0.5f).epsilon(0.1));
    CHECK(level(1.0f) > near);

    // An impulse 343 m away is heard one second later.
    VolumetricMixer mixer(kRate);
    mixer.setListener({0, 0, 0}, {0, 0, 1});
    mixer.play(at(impulse(), {0, 0, 343.0f}, false));
    mixer.settings().airAbsorption = false;
    const Rendered r = render(mixer, 1.2f);
    usize first = 0;
    while (first < r.left.size() && std::fabs(r.left[first]) < 1e-4f && std::fabs(r.right[first]) < 1e-4f) ++first;
    CHECK(first > static_cast<usize>(kRate * 0.99f));
    CHECK(first < static_cast<usize>(kRate * 1.01f));
    // And with the delay switched off, at once.
    VolumetricMixer instant(kRate);
    instant.settings().propagationDelay = false;
    instant.setListener({0, 0, 0}, {0, 0, 1});
    instant.play(at(impulse(), {0, 0, 343.0f}, false));
    const Rendered q = render(instant, 0.05f);
    CHECK(q.rms(0, 2000) > 1e-4f);
}

TEST_CASE("Volumetric audio: a source that moves is Doppler shifted by the changing delay", "[audio][volumetric][doppler]") {
    const auto tone = sine(1000.0f);
    const f32 speed = 34.3f;  // a tenth of the speed of sound
    auto measure = [&](f32 direction, bool doppler) {
        VolumetricMixer mixer(kRate);
        mixer.settings().airAbsorption = false;
        mixer.setListener({0, 0, 0}, {0, 0, 1});
        VoiceParams p = at(tone, {0, 0, direction > 0 ? 400.0f : 80.0f});
        p.doppler = doppler;
        p.maxDistance = 2000.0f;
        const VoiceId id = mixer.play(p);
        Rendered r = render(mixer, 1.6f, [&](f32 t) {
            const f32 z = (direction > 0 ? 400.0f : 80.0f) - direction * speed * t;
            mixer.setPosition(id, {0, 0, z});
        });
        // Away from the first moments (the sound has not arrived yet) and the last.
        const usize from = static_cast<usize>(1.2f * kRate), to = static_cast<usize>(1.55f * kRate);
        return r.frequency(from, to);
    };
    const f32 approaching = measure(+1.0f, true);
    const f32 receding = measure(-1.0f, true);
    CHECK(approaching == Approx(1000.0f / (1.0f - 0.1f)).epsilon(0.02));
    CHECK(receding == Approx(1000.0f / (1.0f + 0.1f)).epsilon(0.02));
    CHECK(approaching > receding);
    // No Doppler: the pitch stays.
    CHECK(measure(+1.0f, false) == Approx(1000.0f).epsilon(0.01));
}

TEST_CASE("Volumetric audio: the air takes the highs, so near sound is clearer than far sound", "[audio][volumetric]") {
    const auto hiss = noise();
    auto brightness = [&](f32 distance) {
        VolumetricMixer mixer(kRate);
        mixer.setListener({0, 0, 0}, {0, 0, 1});
        VoiceParams p = at(hiss, {0, 0, distance});
        p.maxDistance = 2000.0f;
        mixer.play(p);
        return render(mixer, 1.2f).brightness(kRate / 2, kRate);
    };
    CHECK(brightness(3.0f) > brightness(150.0f) * 1.15f);
}

TEST_CASE("Volumetric audio: a wall muffles (the highs more than the lows) and a doorway lets the sound bend round", "[audio][volumetric][occlusion]") {
    AcousticBoxWorld solid;
    solid.add({-200, 0, 9.8f}, {200, 40, 10.2f}, AcousticMaterial::concrete());
    const auto hiss = noise();
    const auto low = sine(120.0f);

    auto measure = [&](const AcousticWorld* world, const VolumetricClipPtr& clip, VoiceDebug* debug = nullptr) {
        VolumetricMixer mixer(kRate);
        mixer.setWorld(world);
        mixer.setListener({0, 1.5f, 0}, {0, 0, 1});
        const VoiceId id = mixer.play(at(clip, {0, 1.5f, 20.0f}));
        const Rendered r = render(mixer, 1.0f);
        if (debug) *debug = mixer.debug(id);
        return r;
    };
    VoiceDebug blocked;
    const Rendered open = measure(nullptr, hiss);
    const Rendered shut = measure(&solid, hiss, &blocked);
    const f32 dB = 20.0f * std::log10(open.rms(kRate / 2, kRate) / shut.rms(kRate / 2, kRate));
    INFO("hiss through the wall: -" << dB << " dB");
    CHECK(dB > 15.0f);
    CHECK(shut.brightness(kRate / 2, kRate) < open.brightness(kRate / 2, kRate) * 0.7f);
    CHECK(blocked.blocked);
    CHECK(blocked.transmissionLow > blocked.transmissionHigh);
    // The lows get through a wall far better than the highs.
    const f32 lowDb = 20.0f * std::log10(measure(nullptr, low).rms(kRate / 2, kRate) / measure(&solid, low).rms(kRate / 2, kRate));
    CHECK(lowDb < dB);
    // Thicker wall, quieter.
    AcousticBoxWorld thick;
    thick.add({-200, 0, 9.0f}, {200, 40, 11.0f}, AcousticMaterial::concrete());
    CHECK(measure(&thick, hiss).rms(kRate / 2, kRate) < shut.rms(kRate / 2, kRate));

    // A wall with an end: the sound bends round it, louder than through it, and seems to come from the corner.
    AcousticBoxWorld shortWall;
    shortWall.add({-1, 0, 9.8f}, {1, 4, 10.2f}, AcousticMaterial::concrete());
    VoiceDebug bent;
    const Rendered round = measure(&shortWall, hiss, &bent);
    CHECK(bent.blocked);
    CHECK(bent.diffracted);
    CHECK(round.rms(kRate / 2, kRate) > shut.rms(kRate / 2, kRate) * 2.0f);
    CHECK(round.rms(kRate / 2, kRate) < open.rms(kRate / 2, kRate));
    // The same wall with the source off to the side of the listener's line: it is heard from its edge.
    VolumetricMixer mixer(kRate);
    mixer.setWorld(&shortWall);
    mixer.setListener({0, 1.5f, 0}, {0, 0, 1});
    const VoiceId id = mixer.play(at(hiss, {0, 1.5f, 20.0f}));
    render(mixer, 0.2f);
    const VoiceDebug d = mixer.debug(id);
    CHECK(std::fabs(d.apparentDirection.x) > 0.01f);  // pulled towards a corner of the wall, off the straight line
    // Without diffraction the same scene is just muffled.
    mixer.settings().diffraction = false;
    mixer.stop(id);
    const VoiceId again = mixer.play(at(hiss, {0, 1.5f, 20.0f}));
    render(mixer, 0.2f);
    CHECK_FALSE(mixer.debug(again).diffracted);
}

TEST_CASE("Volumetric audio: the room is read from rays: shut in, open, and far walls send echoes", "[audio][volumetric][room]") {
    AcousticBoxWorld field;
    field.add({-500, -1, -500}, {500, 0, 500}, AcousticMaterial::ground(), false);
    const RoomEstimate open = probeRoom(field, {0, 1.5f, 0});
    CHECK(open.openness > 0.9f);
    CHECK(open.wet < 0.05f);
    CHECK(open.skyExposure == Approx(1.0f));
    CHECK(open.echoCount == 0);

    AcousticBoxWorld hall;
    hall.addRoom({-4, 0, -4}, {4, 3, 4}, AcousticMaterial::concrete());
    const RoomEstimate shut = probeRoom(hall, {0, 1.5f, 0});
    CHECK(shut.openness < 0.1f);
    CHECK(shut.skyExposure < 0.1f);
    CHECK(shut.rt60 > 0.3f);
    CHECK(shut.wet > open.wet + 0.4f);

    // A bigger room rings longer; softer walls ring less.
    AcousticBoxWorld cathedral;
    cathedral.addRoom({-20, 0, -20}, {20, 18, 20}, AcousticMaterial::concrete());
    CHECK(probeRoom(cathedral, {0, 1.5f, 0}).rt60 > shut.rt60);
    AcousticBoxWorld padded;
    padded.addRoom({-4, 0, -4}, {4, 3, 4}, AcousticMaterial::curtain());
    CHECK(probeRoom(padded, {0, 1.5f, 0}).rt60 < shut.rt60);

    // A cliff 60 m away: the shot comes back 2 d / c later.
    AcousticBoxWorld canyon = field;
    canyon.add({-200, 0, 60}, {200, 80, 70}, AcousticMaterial::concrete());
    const RoomEstimate echoing = probeRoom(canyon, {0, 1.5f, 0});
    REQUIRE(echoing.echoCount >= 1);
    CHECK(echoing.echoes[0].delay == Approx(2.0f * 60.0f / 343.0f).margin(0.05));
    CHECK(echoing.echoes[0].gain > 0.1f);
}

TEST_CASE("Volumetric audio: a shot rings in a shut room, echoes off a far wall and is dry in a field", "[audio][volumetric][room]") {
    AcousticBoxWorld field;
    field.add({-500, -1, -500}, {500, 0, 500}, AcousticMaterial::ground(), false);
    AcousticBoxWorld hall;
    hall.addRoom({-5, 0, -5}, {5, 3.5f, 5}, AcousticMaterial::concrete());
    AcousticBoxWorld canyon = field;
    canyon.add({-300, 0, 90}, {300, 80, 100}, AcousticMaterial::concrete());

    auto shot = [&](const AcousticWorld* world, Vec3 where, f32 seconds, VolumetricMixer** keep = nullptr) {
        (void)keep;
        VolumetricMixer mixer(kRate);
        mixer.settings().propagationDelay = false;
        mixer.setWorld(world);
        mixer.setListener({0, 1.5f, 0}, {0, 0, 1});
        mixer.play(at(impulse(), where, false));
        return render(mixer, seconds);
    };
    const Rendered dry = shot(&field, {0, 1.5f, 4.0f}, 1.2f);
    const Rendered room = shot(&hall, {0, 1.5f, 4.0f}, 1.2f);
    // The tail: energy between 0.3 and 1.0 s after the shot.
    const f32 dryTail = dry.rms(kRate * 3 / 10, kRate);
    const f32 roomTail = room.rms(kRate * 3 / 10, kRate);
    INFO("tail: field " << dryTail << " room " << roomTail);
    CHECK(roomTail > dryTail * 10.0f + 1e-6f);

    // The echo: a second burst of energy near 2 d / c = 0.52 s that the dry field does not have.
    const Rendered cliff = shot(&canyon, {0, 1.5f, 4.0f}, 1.2f);
    const usize a = static_cast<usize>(0.50f * kRate), b = static_cast<usize>(0.56f * kRate);
    const f32 echo = cliff.rms(a, b);
    const f32 quiet = cliff.rms(static_cast<usize>(0.30f * kRate), static_cast<usize>(0.36f * kRate));
    INFO("echo " << echo << " quiet " << quiet);
    CHECK(echo > quiet * 4.0f);
    CHECK(echo > dry.rms(a, b) * 4.0f + 1e-6f);
}

TEST_CASE("Volumetric audio: the ears: a source on the right is louder and earlier in the right ear", "[audio][volumetric]") {
    const auto hiss = noise();
    VolumetricMixer mixer(kRate);
    mixer.setListener({0, 0, 0}, {0, 0, 1});
    // Facing +Z with +Y up, the right-hand side is -X.
    mixer.play(at(hiss, {-8, 0, 0.5f}));
    const Rendered r = render(mixer, 1.0f);
    CHECK(r.rms(kRate / 2, kRate, true) > r.rms(kRate / 2, kRate, false) * 1.5f);
    // An impulse on the right reaches the right ear first.
    VolumetricMixer pulse(kRate);
    pulse.settings().propagationDelay = false;
    pulse.setListener({0, 0, 0}, {0, 0, 1});
    pulse.play(at(impulse(), {-8, 0, 0.0f}, false));
    const Rendered p = render(pulse, 0.05f);
    auto onset = [](const std::vector<f32>& v) {
        usize i = 0;
        f32 peak = 0.0f;
        for (f32 s : v) peak = std::max(peak, std::fabs(s));
        while (i < v.size() && std::fabs(v[i]) < peak * 0.2f) ++i;
        return i;
    };
    CHECK(onset(p.right) < onset(p.left));
    CHECK(onset(p.left) - onset(p.right) < 40);  // under a millisecond
    // With the ears off both sides are the same.
    VolumetricMixer flat(kRate);
    flat.settings().ears = false;
    flat.setListener({0, 0, 0}, {0, 0, 1});
    flat.play(at(hiss, {-8, 0, 0.5f}));
    const Rendered f = render(flat, 0.5f);
    CHECK(f.rms(kRate / 4, kRate / 2, true) == Approx(f.rms(kRate / 4, kRate / 2, false)));
}

TEST_CASE("Volumetric audio: ambient layers (rain, wind) are muffled indoors and layers have their own volume", "[audio][volumetric][layers]") {
    AcousticBoxWorld world;
    world.add({-500, -1, -500}, {500, 0, 500}, AcousticMaterial::ground(), false);
    world.addRoom({20, 0, -4}, {28, 3, 4}, AcousticMaterial::concrete());
    const auto rain = noise(3.0f, 4);
    auto ambient = [&](const Vec3& listener, f32 layerVolume = 1.0f) {
        VolumetricMixer mixer(kRate);
        mixer.setWorld(&world);
        mixer.setListener(listener, {0, 0, 1});
        VoiceParams p = at(rain, {0, 0, 0});
        p.layer = AudioLayer::Ambient;
        mixer.setLayerVolume(AudioLayer::Ambient, layerVolume);
        mixer.play(p);
        return render(mixer, 1.5f);
    };
    const Rendered outside = ambient({0, 1.5f, 0});
    const Rendered inside = ambient({24, 1.5f, 0});
    CHECK(inside.rms(kRate, kRate * 3 / 2) < outside.rms(kRate, kRate * 3 / 2) * 0.35f);
    CHECK(inside.brightness(kRate, kRate * 3 / 2) < outside.brightness(kRate, kRate * 3 / 2) * 0.7f);
    // The layer's volume scales it, and 0 silences it.
    CHECK(ambient({0, 1.5f, 0}, 0.5f).rms(kRate, kRate * 3 / 2) == Approx(outside.rms(kRate, kRate * 3 / 2) * 0.5f).epsilon(0.1));
    CHECK(ambient({0, 1.5f, 0}, 0.0f).rms(kRate, kRate * 3 / 2) < 1e-6f);
}

TEST_CASE("Volumetric audio: voices end, stop, and the output never clips", "[audio][volumetric]") {
    VolumetricMixer mixer(kRate);
    mixer.setListener({0, 0, 0}, {0, 0, 1});
    VoiceParams p = at(impulse(), {0, 0, 3}, false);
    const VoiceId once = mixer.play(p);
    CHECK(mixer.playing(once));
    render(mixer, 1.0f);
    CHECK_FALSE(mixer.playing(once));
    CHECK(mixer.activeVoices() == 0);
    // A looping voice runs until stopped.
    const VoiceId loop = mixer.play(at(sine(300.0f), {0, 0, 3}));
    render(mixer, 0.3f);
    CHECK(mixer.playing(loop));
    mixer.stop(loop);
    CHECK_FALSE(mixer.playing(loop));
    // Many loud voices: the soft limiter keeps every sample inside -1..1.
    for (int i = 0; i < 12; ++i) {
        VoiceParams loud = at(noise(1.0f, 20 + i), {static_cast<f32>(i) - 6.0f, 0, 1.0f});
        loud.volume = 3.0f;
        mixer.play(loud);
    }
    const Rendered r = render(mixer, 0.5f);
    f32 peak = 0.0f;
    for (usize i = 0; i < r.left.size(); ++i) peak = std::max({peak, std::fabs(r.left[i]), std::fabs(r.right[i])});
    CHECK(peak <= 1.0f);
    CHECK(peak > 0.5f);
}

TEST_CASE("Volumetric audio: a wall turned 45 degrees blocks along its own axes", "[audio][volumetric][geometry]") {
    // A thin wall, 10 m long, turned 45 degrees about Y: a ray that would miss the unturned box crosses it.
    AcousticBoxWorld world;
    AcousticOrientedBox wall;
    wall.center = {0, 1.5f, 10};
    wall.half = {5.0f, 1.5f, 0.1f};
    const f32 c = std::cos(0.7853982f), s = std::sin(0.7853982f);
    wall.axis[0] = {c, 0, -s};
    wall.axis[2] = {s, 0, c};
    wall.material = AcousticMaterial::concrete();
    world.add(wall);
    // Along the wall's own length (5 m out at 45 degrees: x = 3.5, z = 10 - 3.5): blocked there, clear beyond its end.
    CHECK_FALSE(world.transmission({3.5f, 1.5f, 0}, {3.5f, 1.5f, 20}).clear());
    CHECK(world.transmission({-12.0f, 1.5f, 0}, {-12.0f, 1.5f, 20}).clear());
    // The unturned box would have blocked x = 4, the turned one does not reach z = 10 there but does at z = 6.5.
    CHECK(world.transmission({6.0f, 1.5f, 0}, {6.0f, 1.5f, 5.0f}).clear());
    // Rays see its face: the hit distance and normal come out in the wall's frame.
    const RayHit hit = world.raycast({0, 1.5f, 0}, {0, 0, 1}, 50.0f);
    REQUIRE(hit.hit);
    CHECK(hit.distance == Approx(10.0f - 0.1f / c).margin(0.05));
    CHECK(std::fabs(hit.normal.dot(Vec3(-s, 0, -c))) > 0.99f);
    // A detour candidate sits at the turned corner, not at an axis-aligned one.
    bool atCorner = false;
    for (const Vec3& p : world.detourCandidates({0, 1.5f, 0}, {0, 1.5f, 20})) {
        if (std::fabs(p.x - (5.35f * c + 0.45f * s)) < 0.2f || std::fabs(p.x + (5.35f * c + 0.45f * s)) < 0.2f) atCorner = true;
    }
    CHECK(atCorner);
}

TEST_CASE("Volumetric audio: a source in another room brings that room's reverb through the wall", "[audio][volumetric][room]") {
    // The listener is in the open; a shut concrete hall holds the source. What leaks through the wall rings.
    AcousticBoxWorld world;
    world.add({-500, -1, -500}, {500, 0, 500}, AcousticMaterial::ground(), false);
    world.addRoom({20, 0, -6}, {32, 4, 6}, AcousticMaterial::wood(), 0.3f);  // wood: the wall lets some through
    auto tail = [&](bool reverb) {
        VolumetricMixer mixer(kRate);
        mixer.settings().propagationDelay = false;
        mixer.settings().reverb = reverb;
        mixer.setWorld(&world);
        mixer.setListener({10, 1.5f, 0}, {1, 0, 0});
        mixer.play(at(impulse(), {26, 1.5f, 0}, false));
        const Rendered r = render(mixer, 1.6f);
        return std::make_pair(r.rms(kRate / 2, kRate * 3 / 2), mixer.debug(1).blocked);
    };
    const auto with = tail(true);
    const auto without = tail(false);
    INFO("tail with " << with.first << " without " << without.first);
    CHECK(with.first > without.first * 3.0f + 1e-7f);
    // The tail is the hall's: it rings for longer than the open field it is heard in would.
    VolumetricMixer open(kRate);
    open.settings().propagationDelay = false;
    open.setWorld(&world);
    open.setListener({10, 1.5f, 0}, {1, 0, 0});
    open.play(at(impulse(), {10, 1.5f, 8.0f}, false));  // both in the open
    const Rendered r = render(open, 1.6f);
    CHECK(r.rms(kRate / 2, kRate * 3 / 2) < with.first);
}

TEST_CASE("Volumetric audio: a source updated less often than the mixer renders still shifts smoothly", "[audio][volumetric][doppler]") {
    // The host moves the source at 60 Hz while blocks are 5.8 ms: the mixer carries it on between moves.
    const auto tone = sine(1000.0f);
    const f32 speed = 34.3f;
    VolumetricMixer mixer(kRate);
    mixer.settings().airAbsorption = false;
    mixer.setListener({0, 0, 0}, {0, 0, 1});
    VoiceParams p = at(tone, {0, 0, 400.0f});
    p.maxDistance = 2000.0f;
    const VoiceId id = mixer.play(p);
    std::vector<f32> left;
    std::vector<f32> block(kBlock * 2);
    f32 sinceMove = 1.0f;  // seconds since the last move
    f32 t = 0.0f;
    for (u32 b = 0; b < static_cast<u32>(1.6f * kRate / kBlock); ++b) {
        if (sinceMove >= 1.0f / 60.0f) {
            mixer.moveVoice(id, {0, 0, 400.0f - speed * t}, sinceMove >= 1.0f ? 0.0f : sinceMove);
            sinceMove = 0.0f;
        }
        mixer.render(block.data(), kBlock);
        for (u32 i = 0; i < kBlock; ++i) left.push_back(block[i * 2]);
        t += static_cast<f32>(kBlock) / kRate;
        sinceMove += static_cast<f32>(kBlock) / kRate;
    }
    Rendered r;
    r.left = left;
    // Frequency in successive 0.1 s windows after the sound has arrived.
    std::vector<f32> freqs;
    for (f32 from = 1.2f; from + 0.1f < 1.55f; from += 0.1f)
        freqs.push_back(r.frequency(static_cast<usize>(from * kRate), static_cast<usize>((from + 0.1f) * kRate)));
    for (f32 f : freqs) CHECK(f == Approx(1000.0f / 0.9f).epsilon(0.03));
}

TEST_CASE("Volumetric audio: the head shadows the highs of the far ear much more than its lows", "[audio][volumetric][ears]") {
    auto levels = [&](f32 hz) {
        VolumetricMixer mixer(kRate);
        mixer.settings().airAbsorption = false;
        mixer.setListener({0, 0, 0}, {0, 0, 1});
        mixer.play(at(sine(hz), {-6, 0, 0.0f}));  // hard right (right = -X when facing +Z)
        const Rendered r = render(mixer, 0.6f);
        return std::make_pair(r.rms(kRate / 4, kRate / 2, true), r.rms(kRate / 4, kRate / 2, false));
    };
    const auto low = levels(200.0f);
    const auto high = levels(6000.0f);
    const f32 lowDb = 20.0f * std::log10(low.first / low.second);
    const f32 highDb = 20.0f * std::log10(high.first / high.second);
    INFO("near/far ear: " << lowDb << " dB at 200 Hz, " << highDb << " dB at 6 kHz");
    CHECK(lowDb > -0.5f);
    CHECK(lowDb < 6.0f);
    CHECK(highDb > 12.0f);
    CHECK(highDb > lowDb + 8.0f);
    // Dead ahead the ears agree at every frequency.
    VolumetricMixer ahead(kRate);
    ahead.settings().airAbsorption = false;
    ahead.setListener({0, 0, 0}, {0, 0, 1});
    ahead.play(at(sine(6000.0f), {0, 0, 6}));
    const Rendered r = render(ahead, 0.6f);
    CHECK(r.rms(kRate / 4, kRate / 2, true) == Approx(r.rms(kRate / 4, kRate / 2, false)).epsilon(0.02));
    // The time difference follows Woodworth: about 0.66 ms at 90 degrees, half that at 30.
    auto itd = [&](f32 x, f32 z) {
        VolumetricMixer pulse(kRate);
        pulse.settings().propagationDelay = false;
        pulse.settings().airAbsorption = false;
        pulse.setListener({0, 0, 0}, {0, 0, 1});
        pulse.play(at(impulse(), {x, 0, z}, false));
        const Rendered p = render(pulse, 0.05f);
        auto onset = [](const std::vector<f32>& v) {
            f32 peak = 0.0f;
            for (f32 s : v) peak = std::max(peak, std::fabs(s));
            usize i = 0;
            while (i < v.size() && std::fabs(v[i]) < peak * 0.3f) ++i;
            return i;
        };
        return static_cast<f32>(onset(p.left) - onset(p.right)) / kRate * 1000.0f;  // ms
    };
    CHECK(itd(-6.0f, 0.0f) == Approx(0.66f).margin(0.12f));
    CHECK(itd(-3.0f, 5.196f) == Approx(0.34f).margin(0.12f));
}
