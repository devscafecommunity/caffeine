#pragma once

// Lets the weather player (WeatherAudio) drive the real AudioService.

#include "audio/AudioService.hpp"
#include "audio/WeatherAudio.hpp"

namespace Caffeine::Audio {

class AudioServiceSink final : public AudioSink {
public:
    explicit AudioServiceSink(AudioService& service) : m_service(service) {}
    bool ready() const override { return m_service.ready(); }
    bool registerMono(const std::string& name, const std::vector<f32>& samples, u32 sampleRate) override {
        return m_service.registerSamples(name, samples, sampleRate, 1);
    }
    u32 play(const std::string& name, f32 volume, f32 pan, bool loop) override {
        return m_service.play(name, volume, pan, loop);
    }
    bool setVolume(u32 handle, f32 volume) override { return m_service.setVolume(handle, volume); }
    bool stop(u32 handle) override { return m_service.stop(handle); }

private:
    AudioService& m_service;
};

}  // namespace Caffeine::Audio
