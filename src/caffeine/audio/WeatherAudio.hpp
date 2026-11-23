#pragma once

// The sound of the weather: a rain bed whose loudness follows the rain, a wind bed that follows the wind and its
// gusts, and a clap of thunder for every thunder event, panned to where the strike was and dulled by its distance.
//
// Everything is made from noise (AmbienceSynth.hpp, ThunderSynth.hpp), so there are no files to ship. The player
// talks to an `AudioSink` so the rules can be tested without a sound device; `AudioService` is adapted in
// AudioServiceSink (see AudioService.hpp).

#include "audio/AmbienceSynth.hpp"
#include "audio/Spatial.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace Caffeine::Audio {

/// What the weather player needs from an audio device.
class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual bool ready() const = 0;
    /// Makes `samples` (mono, -1..1) playable under `name`.
    virtual bool registerMono(const std::string& name, const std::vector<f32>& samples, u32 sampleRate) = 0;
    /// Starts a clip; returns a handle (> 0) or 0.
    virtual u32 play(const std::string& name, f32 volume, f32 pan, bool loop) = 0;
    virtual bool setVolume(u32 handle, f32 volume) = 0;
    virtual bool stop(u32 handle) = 0;
};

/// A device that is never ready: the weather player runs on it to follow the weather without playing.
class NullAudioSink final : public AudioSink {
public:
    bool ready() const override { return false; }
    bool registerMono(const std::string&, const std::vector<f32>&, u32) override { return false; }
    u32 play(const std::string&, f32, f32, bool) override { return 0; }
    bool setVolume(u32, f32) override { return false; }
    bool stop(u32) override { return false; }
};

/// The weather as the sound sees it, one frame.
struct WeatherSound {
    bool rain = false;
    f32 rainIntensity = 0.0f;  ///< 0..1
    f32 windSpeed = 0.0f;      ///< m/s, the mean
    f32 gust = 1.0f;           ///< the wind's surge relative to its mean (1 = mean)
};

struct WeatherThunder {
    f32 distance = 0.0f;  ///< metres
    f32 azimuth = 0.0f;   ///< radians around +Y, 0 = +Z toward +X: the direction of the strike
    f32 volume = 1.0f;    ///< 0..1
};

/// The loudness of the two beds, 0..1 before the master volume.
struct WeatherLevels {
    f32 rain = 0.0f;
    f32 wind = 0.0f;
};

inline WeatherLevels weatherLevels(const WeatherSound& w) {
    WeatherLevels l;
    if (w.rain) l.rain = 0.62f * std::pow(std::clamp(w.rainIntensity, 0.0f, 1.0f), 0.8f);
    // A breeze is barely there; a gale roars. The gusts swell it by their own factor.
    const f32 speed = std::max(w.windSpeed, 0.0f) * std::max(w.gust, 0.0f);
    l.wind = 0.55f * std::clamp(speed / 28.0f, 0.0f, 1.0f);
    return l;
}

/// The pan of a strike at `azimuth` for a listener facing `listenerYaw` (same convention: forward is
/// (sin yaw, 0, cos yaw)): the right side is positive.
inline f32 thunderPan(f32 azimuth, f32 listenerYaw) { return std::sin(listenerYaw - azimuth); }

class WeatherAudio {
public:
    explicit WeatherAudio(AudioSink& sink) : m_sink(sink) {}
    ~WeatherAudio() { stop(); }
    WeatherAudio(const WeatherAudio&) = delete;
    WeatherAudio& operator=(const WeatherAudio&) = delete;

    void setVolume(f32 volume) { m_volume = std::clamp(volume, 0.0f, 1.0f); }
    f32 volume() const { return m_volume; }
    /// A muted player follows the weather and counts its claps but plays nothing (another mixer is playing them).
    void setMuted(bool muted) {
        if (muted && !m_muted) stopBeds();
        m_muted = muted;
    }

    struct Frame {
        WeatherSound weather;
        f32 listenerYaw = 0.0f;
        const WeatherThunder* thunder = nullptr;
        u32 thunderCount = 0;
        f32 dt = 1.0f / 60.0f;
    };

    /// Call once per frame. The levels are followed and the events counted whatever the device does; without one
    /// (CI, a headless editor) nothing is played.
    void update(const Frame& frame) {
        const WeatherLevels target = weatherLevels(frame.weather);
        // The beds follow the weather smoothly: rain does not begin or stop at a click.
        const f32 k = 1.0f - std::exp(-std::max(frame.dt, 0.0f) / 0.6f);
        m_rain += (target.rain - m_rain) * k;
        m_wind += (target.wind - m_wind) * k;
        m_thunderHeard += frame.thunderCount;
        if (m_muted || !m_sink.ready()) return;
        drive(m_rainLoop, "weather:rain", m_rain, [&] { return synthesizeRainLoop(7); });
        drive(m_windLoop, "weather:wind", m_wind, [&] { return synthesizeWindLoop(11); });
        for (u32 i = 0; i < frame.thunderCount; ++i) playThunder(frame.thunder[i], frame.listenerYaw);
    }

    /// Stops every bed and forgets the levels.
    void stop() {
        stopBeds();
        m_rain = m_wind = 0.0f;
    }

    f32 rainLevel() const { return m_rain; }
    f32 windLevel() const { return m_wind; }
    bool rainPlaying() const { return m_rainLoop != 0; }
    bool windPlaying() const { return m_windLoop != 0; }
    /// Claps handed to the player, and the ones that actually started (a device was there).
    u32 thunderHeard() const { return m_thunderHeard; }
    u32 thunderPlayed() const { return m_thunderPlayed; }

private:
    void stopBeds() {
        if (m_rainLoop) m_sink.stop(m_rainLoop);
        if (m_windLoop) m_sink.stop(m_windLoop);
        m_rainLoop = m_windLoop = 0;
    }

    template <typename Make>
    void drive(u32& handle, const char* name, f32 level, Make make) {
        const f32 gain = level * m_volume;
        if (handle == 0) {
            if (level < 0.01f) return;
            if (m_registered.insert(name).second) {
                const ThunderClip clip = make();
                m_sink.registerMono(name, clip.samples, clip.sampleRate);
            }
            handle = m_sink.play(name, gain, 0.0f, true);
        } else if (level < 0.003f) {
            m_sink.stop(handle);
            handle = 0;
        } else {
            m_sink.setVolume(handle, gain);
        }
    }

    void playThunder(const WeatherThunder& event, f32 listenerYaw) {
        // A handful of variants per distance band keeps the clips cached and the claps from sounding identical.
        const u32 band = static_cast<u32>(std::clamp(event.distance, 50.0f, 8000.0f) / 150.0f);
        const u32 variant = m_thunderVariant % 3u;
        const std::string name = "weather:thunder:" + std::to_string(band) + ":" + std::to_string(variant);
        if (m_registered.insert(name).second) {
            const ThunderClip clip = synthesizeThunder(static_cast<f32>(band) * 150.0f + 75.0f, band * 3u + variant);
            m_sink.registerMono(name, clip.samples, clip.sampleRate);
        }
        const u32 handle = m_sink.play(name, std::clamp(event.volume, 0.0f, 1.0f) * m_volume,
                                       thunderPan(event.azimuth, listenerYaw), false);
        if (handle != 0) ++m_thunderPlayed;
        ++m_thunderVariant;
    }

    AudioSink& m_sink;
    f32 m_volume = 0.8f;
    bool m_muted = false;
    f32 m_rain = 0.0f;
    f32 m_wind = 0.0f;
    u32 m_rainLoop = 0;
    u32 m_windLoop = 0;
    u32 m_thunderPlayed = 0;
    u32 m_thunderHeard = 0;
    u32 m_thunderVariant = 0;
    std::unordered_set<std::string> m_registered;
};

}  // namespace Caffeine::Audio
