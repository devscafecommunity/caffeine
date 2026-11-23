#pragma once

// Connects the volumetric audio to a scene: the entities with an `Audio Emitter` become voices, the ones with an
// `Acoustic Obstacle` become the boxes sound cannot cross, and the scene's `Volumetric Audio` component sets the mixer.
// Call `sync` once a frame from the thread that owns the world; the audio thread only ever sees the mixer.

#include "audio/volumetric/VolumetricAudio.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::ECS { class World; }

namespace Caffeine::Audio {

class SceneAudio {
public:
    explicit SceneAudio(VolumetricAudio& audio);
    ~SceneAudio();
    SceneAudio(const SceneAudio&) = delete;
    SceneAudio& operator=(const SceneAudio&) = delete;

    /// Where a relative clip path is looked for (each root in turn, after the path as written).
    void setSearchRoots(std::vector<std::string> roots);

    /// Reads the scene and updates the mixer: voices, obstacles, settings, the listener. With no enabled
    /// `Volumetric Audio` component every voice is stopped.
    /// `interval` is the time, in seconds of audio, since the previous call: voices and the listener that moved are given
    /// a velocity from it, so the mixer carries them on smoothly (and the Doppler shift stays continuous) between calls.
    void sync(ECS::World& world, const Vec3& listenerPosition, const Vec3& listenerForward, f32 interval = 1.0f / 60.0f);

    struct VoiceInfo {
        u32 entity = 0;
        std::string clip;
        AudioLayer layer = AudioLayer::World;
        bool loaded = false;   ///< the clip was found and decoded
        bool playing = false;
        VoiceDebug debug;
    };
    /// The weather as the sound sees it, one frame (the host reads it from its renderer).
    struct WeatherFeed {
        bool enabled = false;      ///< the scene asks for the weather's sound (`Weather` > `Audio Enabled`)
        f32 volume = 0.8f;
        bool rain = false;
        f32 rainIntensity = 0.0f;  ///< 0..1
        f32 windSpeed = 0.0f;      ///< m/s, the mean
        f32 gust = 1.0f;           ///< the wind's surge relative to its mean
    };
    /// A clap of thunder that has just arrived (the host already waited out the sound's travel time).
    struct ThunderClap {
        f32 distance = 0.0f;  ///< metres
        f32 azimuth = 0.0f;   ///< radians around +Y, 0 = +Z toward +X: where the strike was
        f32 volume = 1.0f;
    };
    /// Plays the weather through the mixer: a rain bed and a wind bed on the Ambient layer (muffled and quieter indoors,
    /// like any ambient sound) and every clap as a World voice at its bearing and distance, so a wall muffles it, a hall
    /// rings with it and the ears place it. Call after `sync`, once a frame; `interval` as for `sync`. Does nothing while
    /// the scene's volumetric audio is off.
    void feedWeather(const WeatherFeed& weather, const ThunderClap* claps, u32 count, f32 interval = 1.0f / 60.0f);
    f32 weatherRainLevel() const { return m_rainLevel; }
    f32 weatherWindLevel() const { return m_windLevel; }
    u32 thunderPlayed() const { return m_thunderPlayed; }
    bool weatherRainPlaying() const;
    bool weatherWindPlaying() const;

    bool enabled() const { return m_enabled; }
    u32 obstacleCount() const { return m_obstacles; }
    u32 missingClips() const { return m_missing; }
    /// A snapshot of the voices (their debug state is read under the mixer's lock).
    std::vector<VoiceInfo> voices() const;
    RoomEstimate room() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_enabled = false;
    u32 m_obstacles = 0;
    u32 m_missing = 0;
    f32 m_rainLevel = 0.0f;
    f32 m_windLevel = 0.0f;
    u32 m_thunderPlayed = 0;
};

/// Decodes a WAV file into a clip (any sample format SDL can read; mono or stereo). Null when it cannot.
VolumetricClipPtr loadVolumetricClip(const std::string& path, std::string* error = nullptr);

}  // namespace Caffeine::Audio
