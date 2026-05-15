#pragma once

#ifdef CF_HAS_SDL3

#include "audio/AudioComponents.hpp"
#include "core/Types.hpp"
#include "math/Vec2.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL.h>

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Caffeine::Audio {

using namespace Caffeine;

class AudioSource {
public:
    AudioSource() = default;
    ~AudioSource() { release(); }

    AudioSource(const AudioSource&)            = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    AudioSource(AudioSource&& o) noexcept
        : m_stream(o.m_stream), m_clip(o.m_clip), m_volume(o.m_volume),
          m_pan(o.m_pan), m_pitch(o.m_pitch), m_loop(o.m_loop),
          m_active(o.m_active), m_paused(o.m_paused), m_currentTime(o.m_currentTime) {
        o.m_stream = nullptr;
        o.m_active = false;
    }

    AudioSource& operator=(AudioSource&& o) noexcept {
        if (this != &o) {
            release();
            m_stream      = o.m_stream;
            m_clip        = o.m_clip;
            m_volume      = o.m_volume;
            m_pan         = o.m_pan;
            m_pitch       = o.m_pitch;
            m_loop        = o.m_loop;
            m_active      = o.m_active;
            m_paused      = o.m_paused;
            m_currentTime = o.m_currentTime;
            o.m_stream    = nullptr;
            o.m_active    = false;
        }
        return *this;
    }

    void bind(SDL_AudioDeviceID device, const AudioClip* clip, f32 volume, f32 pan) {
        release();
        if (!clip || !clip->data || device == 0) return;

        SDL_AudioSpec srcSpec;
        srcSpec.format   = SDL_AUDIO_S16;
        srcSpec.channels = static_cast<int>(clip->channels);
        srcSpec.freq     = static_cast<int>(clip->sampleRate);

        m_stream = SDL_CreateAudioStream(&srcSpec, nullptr);
        if (!m_stream) return;

        SDL_BindAudioStream(device, m_stream);

        m_clip        = clip;
        m_volume      = volume;
        m_pan         = pan;
        m_active      = true;
        m_paused      = false;
        m_currentTime = 0.0f;

        applyGain();
        SDL_PutAudioStreamData(m_stream, clip->data, static_cast<int>(clip->size));
    }

    void release() {
        if (m_stream) {
            SDL_UnbindAudioStream(m_stream);
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }
        m_clip    = nullptr;
        m_active  = false;
        m_paused  = false;
        m_currentTime = 0.0f;
    }

    void play() {
        if (!m_stream || !m_active) return;
        m_paused = false;
        applyGain();
    }

    void pause() {
        if (!m_stream || !m_active) return;
        m_paused = true;
        SDL_SetAudioStreamGain(m_stream, 0.0f);
    }

    void stop() {
        if (!m_stream) return;
        SDL_ClearAudioStream(m_stream);
        m_active  = false;
        m_paused  = false;
        m_currentTime = 0.0f;
    }

    void seek(f32 seconds) {
        if (!m_stream || !m_clip || !m_clip->data) return;
        m_currentTime = std::max(0.0f, std::min(seconds, m_clip->duration));
        SDL_ClearAudioStream(m_stream);
        u32 bytesPerSample = static_cast<u32>(m_clip->bitsPerSample / 8) * m_clip->channels;
        u32 byteOffset     = static_cast<u32>(m_currentTime * static_cast<f32>(m_clip->sampleRate)) * bytesPerSample;
        byteOffset = std::min(byteOffset, static_cast<u32>(m_clip->size));
        SDL_PutAudioStreamData(m_stream, m_clip->data + byteOffset,
                               static_cast<int>(m_clip->size - byteOffset));
    }

    void setVolume(f32 v) {
        m_volume = std::max(0.0f, std::min(v, 1.0f));
        if (!m_paused) applyGain();
    }

    void setPan(f32 pan) {
        m_pan = std::max(-1.0f, std::min(pan, 1.0f));
    }

    void setPitch(f32 pitch) {
        m_pitch = std::max(0.1f, std::min(pitch, 4.0f));
    }

    void setLoop(bool loop) { m_loop = loop; }

    bool isPlaying()     const { return m_active && !m_paused; }
    bool isPaused()      const { return m_active && m_paused; }
    bool isFree()        const { return !m_active; }
    f32  currentTime()   const { return m_currentTime; }
    f32  duration()      const { return m_clip ? m_clip->duration : 0.0f; }
    f32  volume()        const { return m_volume; }
    f32  pan()           const { return m_pan; }
    f32  pitch()         const { return m_pitch; }
    bool loop()          const { return m_loop; }

    void tick(f32 dt) {
        if (!m_active || m_paused || !m_stream) return;
        m_currentTime += dt;
        if (m_clip && m_clip->duration > 0.0f && m_currentTime >= m_clip->duration) {
            if (m_loop) {
                m_currentTime = 0.0f;
                SDL_PutAudioStreamData(m_stream, m_clip->data, static_cast<int>(m_clip->size));
            } else {
                if (SDL_GetAudioStreamAvailable(m_stream) == 0) {
                    m_active = false;
                }
            }
        }
    }

private:
    void applyGain() {
        if (!m_stream) return;
        f32 leftGain  = m_volume * (1.0f - std::max(0.0f, m_pan));
        f32 rightGain = m_volume * (1.0f + std::min(0.0f, m_pan));
        f32 gain      = std::max(leftGain, rightGain);
        SDL_SetAudioStreamGain(m_stream, gain);
    }

    SDL_AudioStream* m_stream      = nullptr;
    const AudioClip* m_clip        = nullptr;
    f32              m_volume      = 1.0f;
    f32              m_pan         = 0.0f;
    f32              m_pitch       = 1.0f;
    bool             m_loop        = false;
    bool             m_active      = false;
    bool             m_paused      = false;
    f32              m_currentTime = 0.0f;
};

class AudioSystem {
public:
    AudioSystem()  = default;
    ~AudioSystem() { shutdown(); }

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool init(u32 sfxChannelCount = 32, u32 musicChannelCount = 2) {
        (void)musicChannelCount;
        if (m_initialized) return true;

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;

        SDL_AudioSpec spec;
        spec.format   = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq     = 44100;

        m_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (m_device == 0) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }

        m_sfxPool.resize(sfxChannelCount);
        m_clipStore.reserve(256);
        m_initialized = true;
        return true;
    }

    void shutdown() {
        if (!m_initialized) return;
        for (auto& src : m_sfxPool) src.release();
        m_sfxPool.clear();
        m_clipStore.clear();
        if (m_device) {
            SDL_CloseAudioDevice(m_device);
            m_device = 0;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_initialized = false;
    }

    AudioClip* registerClip(const u8* data, u64 size, u32 sampleRate = 44100,
                             u16 channels = 2, u16 bitsPerSample = 16, f32 duration = 0.0f) {
        AudioClip clip;
        clip.data          = data;
        clip.size          = size;
        clip.sampleRate    = sampleRate;
        clip.channels      = channels;
        clip.bitsPerSample = bitsPerSample;
        clip.duration      = duration;
        m_clipStore.push_back(clip);
        return &m_clipStore.back();
    }

    AudioSource* playSFX(const AudioClip* clip, f32 volume = 1.0f, f32 pan = 0.0f) {
        if (!m_initialized || !clip) return nullptr;
        AudioSource* src = findFreeSource();
        if (!src) return nullptr;
        src->bind(m_device, clip, volume * m_masterVolume, pan);
        return src;
    }

    AudioSource* playSFXAt(const AudioClip* clip, Vec2 worldPos, f32 maxDistance = 500.0f) {
        if (!m_initialized || !clip) return nullptr;
        f32 vol = computeVolume(m_listenerPos, worldPos, maxDistance);
        f32 pan = computePan(m_listenerPos, worldPos, maxDistance);
        return playSFX(clip, vol, pan);
    }

    void setListenerPosition(Vec2 pos, Vec2 forward = {1.0f, 0.0f}) {
        (void)forward;
        m_listenerPos = pos;
    }

    void setSourcePosition(AudioSource* source, Vec2 worldPos, f32 maxDistance = 500.0f) {
        if (!source) return;
        f32 vol = computeVolume(m_listenerPos, worldPos, maxDistance);
        f32 pan = computePan(m_listenerPos, worldPos, maxDistance);
        source->setVolume(vol * m_masterVolume);
        source->setPan(pan);
    }

    void setMasterVolume(f32 volume) {
        m_masterVolume = std::max(0.0f, std::min(volume, 1.0f));
    }

    void setMasterPaused(bool paused) {
        m_masterPaused = paused;
        if (!m_initialized) return;
        if (paused) {
            SDL_PauseAudioDevice(m_device);
        } else {
            SDL_ResumeAudioDevice(m_device);
        }
    }

    void update(f64 dt) {
        if (!m_initialized) return;
        f32 dtf = static_cast<f32>(dt);
        for (auto& src : m_sfxPool) {
            if (!src.isFree()) src.tick(dtf);
        }
    }

    bool isInitialized() const { return m_initialized; }
    f32  masterVolume()   const { return m_masterVolume; }
    bool masterPaused()   const { return m_masterPaused; }
    Vec2 listenerPosition() const { return m_listenerPos; }

    struct Stats {
        u32  activeSFX;
        u32  sfxPoolUsed;
        u32  sfxPoolTotal;
        bool musicPlaying;
        f32  musicVolume;
    };

    Stats stats() const {
        u32 active = 0;
        for (const auto& src : m_sfxPool) {
            if (!src.isFree()) ++active;
        }
        return {active, active, static_cast<u32>(m_sfxPool.size()), false, 0.0f};
    }

private:
    AudioSource* findFreeSource() {
        for (auto& src : m_sfxPool) {
            if (src.isFree()) return &src;
        }
        return nullptr;
    }

    static f32 computeVolume(Vec2 listenerPos, Vec2 emitterPos, f32 maxDistance) {
        f32 dx  = emitterPos.x - listenerPos.x;
        f32 dy  = emitterPos.y - listenerPos.y;
        f32 dist = std::sqrt(dx * dx + dy * dy);
        if (maxDistance <= 0.0f) return 1.0f;
        return std::max(0.0f, std::min(1.0f - dist / maxDistance, 1.0f));
    }

    static f32 computePan(Vec2 listenerPos, Vec2 emitterPos, f32 maxDistance) {
        if (maxDistance <= 0.0f) return 0.0f;
        f32 dx = emitterPos.x - listenerPos.x;
        return std::max(-1.0f, std::min(dx / maxDistance, 1.0f));
    }

    SDL_AudioDeviceID        m_device       = 0;
    bool                     m_initialized  = false;
    std::vector<AudioSource> m_sfxPool;
    std::vector<AudioClip>   m_clipStore;
    Vec2                     m_listenerPos  = {0.0f, 0.0f};
    f32                      m_masterVolume = 1.0f;
    bool                     m_masterPaused = false;
};

}  // namespace Caffeine::Audio

#endif  // CF_HAS_SDL3
