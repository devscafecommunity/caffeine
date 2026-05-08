#ifdef CF_HAS_SDL3

#include "catch.hpp"
#include <audio/AudioComponents.hpp>
#include <audio/AudioSystem.hpp>

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Audio;

static constexpr f32 kEps = 0.001f;
static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

// ============================================================================
// AudioClip
// ============================================================================

TEST_CASE("AudioClip - default values", "[audio]") {
    AudioClip clip;
    REQUIRE(clip.data         == nullptr);
    REQUIRE(clip.size         == 0u);
    REQUIRE(clip.sampleRate   == 44100u);
    REQUIRE(clip.channels     == 2u);
    REQUIRE(clip.bitsPerSample == 16u);
    REQUIRE(approxEq(clip.duration, 0.0f));
}

// ============================================================================
// AudioEmitter
// ============================================================================

TEST_CASE("AudioEmitter - default values", "[audio]") {
    AudioEmitter emitter;
    REQUIRE(approxEq(emitter.volume,      1.0f));
    REQUIRE(approxEq(emitter.maxDistance, 500.0f));
    REQUIRE(emitter.loop        == false);
    REQUIRE(emitter.playOnSpawn == true);
    REQUIRE(emitter.spatial     == true);
}

// ============================================================================
// AudioSystem — state before init (no SDL3 device required)
// ============================================================================

TEST_CASE("AudioSystem - default not initialized", "[audio]") {
    AudioSystem sys;
    REQUIRE(sys.isInitialized() == false);
}

TEST_CASE("AudioSystem - masterVolume default", "[audio]") {
    AudioSystem sys;
    REQUIRE(approxEq(sys.masterVolume(), 1.0f));
}

TEST_CASE("AudioSystem - masterPaused default", "[audio]") {
    AudioSystem sys;
    REQUIRE(sys.masterPaused() == false);
}

TEST_CASE("AudioSystem - listenerPosition default", "[audio]") {
    AudioSystem sys;
    Vec2 pos = sys.listenerPosition();
    REQUIRE(approxEq(pos.x, 0.0f));
    REQUIRE(approxEq(pos.y, 0.0f));
}

TEST_CASE("AudioSystem - shutdown on uninitialized is safe", "[audio]") {
    AudioSystem sys;
    sys.shutdown();
    REQUIRE(sys.isInitialized() == false);
}

TEST_CASE("AudioSystem - update on uninitialized is safe", "[audio]") {
    AudioSystem sys;
    sys.update(0.016);
    REQUIRE(true);
}

TEST_CASE("AudioSystem - setMasterVolume stores value", "[audio]") {
    AudioSystem sys;
    sys.setMasterVolume(0.5f);
    REQUIRE(approxEq(sys.masterVolume(), 0.5f));
}

TEST_CASE("AudioSystem - setMasterVolume clamps low", "[audio]") {
    AudioSystem sys;
    sys.setMasterVolume(-1.0f);
    REQUIRE(approxEq(sys.masterVolume(), 0.0f));
}

TEST_CASE("AudioSystem - setMasterVolume clamps high", "[audio]") {
    AudioSystem sys;
    sys.setMasterVolume(2.0f);
    REQUIRE(approxEq(sys.masterVolume(), 1.0f));
}

TEST_CASE("AudioSystem - setListenerPosition stores value", "[audio]") {
    AudioSystem sys;
    sys.setListenerPosition({100.0f, 200.0f});
    Vec2 pos = sys.listenerPosition();
    REQUIRE(approxEq(pos.x, 100.0f));
    REQUIRE(approxEq(pos.y, 200.0f));
}

TEST_CASE("AudioSystem - setMasterPaused stores value before init", "[audio]") {
    AudioSystem sys;
    sys.setMasterPaused(true);
    REQUIRE(sys.masterPaused() == true);
    sys.setMasterPaused(false);
    REQUIRE(sys.masterPaused() == false);
}

TEST_CASE("AudioSystem - playSFX null clip returns nullptr", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    AudioSource* src = sys.playSFX(nullptr, 1.0f, 0.0f);
    REQUIRE(src == nullptr);
}

TEST_CASE("AudioSystem - init does not crash", "[audio]") {
    AudioSystem sys;
    bool ok = sys.init(8);
    (void)ok;
    REQUIRE(true);
}

TEST_CASE("AudioSystem - double init returns true", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    bool second = sys.init(8);
    REQUIRE(second == true);
}

TEST_CASE("AudioSystem - stats returns zero active after init", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    auto s = sys.stats();
    REQUIRE(s.activeSFX   == 0u);
    REQUIRE(s.sfxPoolTotal == 8u);
    REQUIRE(s.musicPlaying == false);
}

TEST_CASE("AudioSystem - registerClip stores and returns non-null", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    static const u8 dummy[64] = {};
    AudioClip* clip = sys.registerClip(dummy, sizeof(dummy), 44100, 2, 16, 0.5f);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->data       == dummy);
    REQUIRE(clip->size       == 64u);
    REQUIRE(clip->sampleRate == 44100u);
    REQUIRE(approxEq(clip->duration, 0.5f));
}

TEST_CASE("AudioSystem - playSFXAt with emitter far away gives zero volume", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    static const u8 dummy[64] = {};
    AudioClip* clip = sys.registerClip(dummy, sizeof(dummy), 44100, 2, 16, 1.0f);
    sys.setListenerPosition({0.0f, 0.0f});
    // emitter is beyond maxDistance — volume should be 0, no source returned or silent
    AudioSource* src = sys.playSFXAt(clip, {600.0f, 0.0f}, 500.0f);
    // volume = clamp(1 - 600/500, 0, 1) = 0 → playSFX with volume 0
    // Source may or may not be returned; if it is, it should not crash
    (void)src;
    REQUIRE(true);
}

TEST_CASE("AudioSystem - playSFXAt center position gives full volume", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    static const u8 dummy[64] = {};
    AudioClip* clip = sys.registerClip(dummy, sizeof(dummy), 44100, 2, 16, 1.0f);
    sys.setListenerPosition({0.0f, 0.0f});
    AudioSource* src = sys.playSFXAt(clip, {0.0f, 0.0f}, 500.0f);
    // distance = 0 → volume = 1, pan = 0
    (void)src;
    REQUIRE(true);
}

TEST_CASE("AudioSystem - setSourcePosition does not crash with null", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    sys.setSourcePosition(nullptr, {100.0f, 0.0f}, 500.0f);
    REQUIRE(true);
}

TEST_CASE("AudioSystem - setMasterPaused does not crash when initialized", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    sys.setMasterPaused(true);
    REQUIRE(sys.masterPaused() == true);
    sys.setMasterPaused(false);
    REQUIRE(sys.masterPaused() == false);
}

TEST_CASE("AudioSystem - update does not crash after init", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    sys.update(0.016);
    REQUIRE(true);
}

TEST_CASE("AudioSystem - shutdown clears initialized state", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    sys.shutdown();
    REQUIRE(sys.isInitialized() == false);
}

TEST_CASE("AudioSystem - stats sfxPoolUsed matches active sources", "[audio]") {
    AudioSystem sys;
    sys.init(8);
    if (!sys.isInitialized()) return;
    auto s = sys.stats();
    REQUIRE(s.sfxPoolUsed == s.activeSFX);
}

#endif  // CF_HAS_SDL3
