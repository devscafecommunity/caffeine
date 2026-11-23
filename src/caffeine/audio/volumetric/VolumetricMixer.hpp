#pragma once

// Volumetric audio: a software mixer that simulates how sound travels through the world instead of just panning it.
// Optional (a game that wants plain 2D/3D sound keeps AudioService), and device-free: it renders blocks of stereo
// samples, so the same code feeds a sound card in play, a WAV file in tests and a video in the demos.
//
// What a voice goes through:
//  * Propagation. The sound is read from its clip at `t - distance / c`, so it arrives late (a gunshot a kilometre away
//    is heard three seconds on) and a source that moves is *Doppler shifted* by the changing delay, exactly, with no
//    pitch trick.
//  * Distance. Gain falls with distance (inverse law) and the air takes the highs, so near sound is clear and far sound
//    is dull; the reverb does not fall as fast as the direct sound, so far sound is also washier.
//  * Occlusion. The segment from the source to the listener is traced through the AcousticWorld: what a wall lets
//    through is quiet and dull (lows pass walls far better than highs). If the sound can bend round the wall (a door,
//    the end of it) that diffracted path is mixed in too, and the voice seems to come from the opening.
//  * The room. Rays round the listener give its size, openness and reflectivity: a reverb tail (a feedback delay
//    network) fits them, and far, hard surfaces send back distinct echoes.
//  * Ears. A small time and level difference between the ears and a dulling of the far ear and of sound from behind.
//  * Layers. World voices are spatial; Ambient voices (rain, wind) cover the scene and follow how exposed the listener
//    is (muffled and quieter indoors); Music and Ui are plain.

#include "audio/volumetric/AcousticWorld.hpp"
#include "audio/volumetric/RoomProbe.hpp"

#include <memory>
#include <vector>

namespace Caffeine::Audio {

/// Decoded audio: mono or stereo, interleaved, -1..1.
struct VolumetricClip {
    std::vector<f32> samples;
    u32 sampleRate = 44100;
    u32 channels = 1;
    u32 frames() const { return channels ? static_cast<u32>(samples.size() / channels) : 0; }
    f32 seconds() const { return sampleRate ? static_cast<f32>(frames()) / static_cast<f32>(sampleRate) : 0.0f; }
};
using VolumetricClipPtr = std::shared_ptr<const VolumetricClip>;

enum class AudioLayer : u8 {
    World,    ///< localised sounds: spatial, occluded, Doppler-shifted
    Ambient,  ///< covers the scene (rain, wind): follows how exposed the listener is
    Music,
    Ui,
};

struct VoiceParams {
    VolumetricClipPtr clip;
    Vec3 position{0.0f, 0.0f, 0.0f};
    f32 volume = 1.0f;
    bool loop = false;
    AudioLayer layer = AudioLayer::World;
    f32 refDistance = 2.0f;     ///< up to here the sound is at full volume
    f32 maxDistance = 400.0f;   ///< beyond it nothing is heard
    f32 rolloff = 1.0f;         ///< 1 = the real inverse law
    bool doppler = true;
    /// false: no travel time and no Doppler, because whoever plays the sound has already timed it (thunder that
    /// is played when it arrives). Distance, occlusion, reverb and the ears still apply.
    bool propagate = true;
    f32 startOffsetSeconds = 0.0f;
};

using VoiceId = u32;

struct VolumetricSettings {
    f32 speedOfSound = 343.0f;
    f32 dopplerScale = 1.0f;     ///< 0 = no Doppler (the delay is frozen), 1 = real
    bool propagationDelay = true;
    bool airAbsorption = true;
    bool occlusion = true;
    bool diffraction = true;
    bool reverb = true;
    bool ears = true;
    f32 masterVolume = 1.0f;
};

/// What a voice is doing, for tests, the editor's debug view and the demo videos.
struct VoiceDebug {
    bool active = false;
    f32 distance = 0.0f;
    f32 delaySeconds = 0.0f;
    f32 pitchRatio = 1.0f;      ///< the Doppler shift: > 1 while it approaches
    f32 gain = 0.0f;            ///< direct path, after distance and what the walls keep
    f32 cutoffHz = 20000.0f;
    f32 transmissionLow = 1.0f;
    f32 transmissionHigh = 1.0f;
    bool blocked = false;
    bool diffracted = false;
    f32 diffractionGain = 0.0f;
    Vec3 apparentDirection{0.0f, 0.0f, 1.0f};  ///< where it seems to come from, in the world
    f32 reverbSend = 0.0f;
};

class VolumetricMixer {
public:
    explicit VolumetricMixer(u32 sampleRate = 44100);
    ~VolumetricMixer();
    VolumetricMixer(const VolumetricMixer&) = delete;
    VolumetricMixer& operator=(const VolumetricMixer&) = delete;

    u32 sampleRate() const;
    VolumetricSettings& settings();
    const VolumetricSettings& settings() const;

    /// The geometry sound travels through (not owned; may be null: free field).
    void setWorld(const AcousticWorld* world);
    void setListener(const Vec3& position, const Vec3& forward, const Vec3& up = Vec3(0.0f, 1.0f, 0.0f));

    VoiceId play(const VoiceParams& params);
    void stop(VoiceId voice);
    void stopAll();
    bool playing(VoiceId voice) const;
    void setPosition(VoiceId voice, const Vec3& position);
    /// Moves a voice that has been moving: `interval` is the time (seconds of audio) since the last move. The mixer
    /// estimates its velocity and carries the voice on along it until the next call, so the delay (and the Doppler shift
    /// it makes) changes smoothly even when the caller updates less often than the mixer renders.
    void moveVoice(VoiceId voice, const Vec3& position, f32 interval);
    /// The same for the listener.
    void moveListener(const Vec3& position, const Vec3& forward, const Vec3& up, f32 interval);
    void setVolume(VoiceId voice, f32 volume);
    u32 activeVoices() const;

    void setLayerVolume(AudioLayer layer, f32 volume);
    f32 layerVolume(AudioLayer layer) const;

    /// Mixes `frames` frames of interleaved stereo into `out` (overwritten) and advances time. Move voices and the
    /// listener between calls: small blocks (about 5 ms) make movement smooth and the Doppler shift continuous.
    void render(f32* out, u32 frames);

    f64 time() const;
    VoiceDebug debug(VoiceId voice) const;
    const RoomEstimate& room() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace Caffeine::Audio
