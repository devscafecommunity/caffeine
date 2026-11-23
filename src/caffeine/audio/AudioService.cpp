#include "audio/AudioService.hpp"

#include "debug/LogSystem.hpp"

#ifdef CF_HAS_SDL3
#include "audio/AudioSystem.hpp"
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Caffeine::Audio {

#ifdef CF_HAS_SDL3

struct AudioService::Impl {
    struct Loaded {
        std::vector<u8> pcm;  // signed 16-bit interleaved
        AudioClip clip;
    };

    AudioSystem system;
    std::string root;
    std::unordered_map<std::string, std::unique_ptr<Loaded>> clips;
    std::unordered_map<u32, AudioSource*> handles;
    u32 nextHandle = 1;

    std::string resolve(const std::string& path) const {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(path, ec)) return path;
        if (!root.empty()) {
            const fs::path rooted = fs::path(root) / path;
            if (fs::exists(rooted, ec)) return rooted.string();
        }
        return path;
    }

    Loaded* load(const std::string& path, std::string* error) {
        auto it = clips.find(path);
        if (it != clips.end()) return it->second.get();

        const std::string file = resolve(path);
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;
        if (!SDL_LoadWAV(file.c_str(), &spec, &data, &length)) {
            if (error) *error = std::string("cannot read WAV: ") + file;
            return nullptr;
        }
        auto loaded = std::make_unique<Loaded>();
        SDL_AudioSpec dst = spec;
        dst.format = SDL_AUDIO_S16;
        if (spec.format == SDL_AUDIO_S16) {
            loaded->pcm.assign(data, data + length);
        } else {
            Uint8* converted = nullptr;
            int convertedLength = 0;
            if (!SDL_ConvertAudioSamples(&spec, data, static_cast<int>(length), &dst, &converted, &convertedLength)) {
                SDL_free(data);
                if (error) *error = "cannot convert audio format";
                return nullptr;
            }
            loaded->pcm.assign(converted, converted + convertedLength);
            SDL_free(converted);
        }
        SDL_free(data);

        const u32 bytesPerFrame = static_cast<u32>(dst.channels) * 2u;
        loaded->clip.data = loaded->pcm.data();
        loaded->clip.size = loaded->pcm.size();
        loaded->clip.sampleRate = static_cast<u32>(dst.freq);
        loaded->clip.channels = static_cast<u16>(dst.channels);
        loaded->clip.bitsPerSample = 16;
        loaded->clip.duration = bytesPerFrame && dst.freq
            ? static_cast<f32>(loaded->pcm.size() / bytesPerFrame) / static_cast<f32>(dst.freq) : 0.0f;
        Loaded* raw = loaded.get();
        clips.emplace(path, std::move(loaded));
        return raw;
    }

    struct Spatial {
        Vec3 position;
        f32 refDistance = 1.0f;
        f32 maxDistance = 100.0f;
        f32 volume = 1.0f;
    };
    Listener3D listener;
    std::unordered_map<u32, Spatial> spatial;

    u32 track(AudioSource* source) {
        if (!source) return 0;
        // Drop handles whose sound ended so the table stays small.
        for (auto it = handles.begin(); it != handles.end();) {
            it = it->second->isFree() ? handles.erase(it) : std::next(it);
        }
        const u32 id = nextHandle++;
        handles[id] = source;
        return id;
    }

    AudioSource* find(u32 handle) const {
        auto it = handles.find(handle);
        if (it == handles.end() || it->second->isFree()) return nullptr;
        return it->second;
    }
};

AudioService::AudioService() : m_impl(new Impl()) {}
AudioService::~AudioService() { shutdown(); }

bool AudioService::init() { return m_impl->system.init(); }
void AudioService::shutdown() {
    if (!m_impl) return;
    m_impl->handles.clear();
    m_impl->spatial.clear();
    m_impl->system.shutdown();
}
bool AudioService::ready() const { return m_impl->system.isInitialized(); }
void AudioService::setSearchRoot(const std::string& root) { m_impl->root = root; }

bool AudioService::preload(const std::string& path, std::string* error) {
    return m_impl->load(path, error) != nullptr;
}

f32 AudioService::duration(const std::string& path) const {
    auto it = m_impl->clips.find(path);
    return it == m_impl->clips.end() ? 0.0f : it->second->clip.duration;
}

u32 AudioService::play(const std::string& path, f32 volume, f32 pan, bool loop) {
    if (!ready()) return 0;
    std::string error;
    Impl::Loaded* clip = m_impl->load(path, &error);
    if (!clip) { CF_WARN("Audio", "%s", error.c_str()); return 0; }
    AudioSource* source = m_impl->system.playSFX(&clip->clip, volume, pan);
    if (source) source->setLoop(loop);
    return m_impl->track(source);
}

u32 AudioService::playAt(const std::string& path, f32 x, f32 y, f32 maxDistance, f32 volume) {
    if (!ready()) return 0;
    std::string error;
    Impl::Loaded* clip = m_impl->load(path, &error);
    if (!clip) { CF_WARN("Audio", "%s", error.c_str()); return 0; }
    AudioSource* source = m_impl->system.playSFXAt(&clip->clip, Vec2{x, y}, maxDistance);
    if (source) source->setVolume(source->volume() * volume);
    return m_impl->track(source);
}

bool AudioService::registerSamples(const std::string& name, const std::vector<f32>& samples, u32 sampleRate,
                                   u32 channels) {
    if (name.empty() || samples.empty() || sampleRate == 0 || channels == 0) return false;
    // A name registered once stays: a looping source reads its clip's memory as it plays.
    if (m_impl->clips.count(name)) return true;
    auto loaded = std::make_unique<Impl::Loaded>();
    loaded->pcm.resize(samples.size() * 2);
    for (usize i = 0; i < samples.size(); ++i) {
        const i16 v = static_cast<i16>(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
        loaded->pcm[i * 2] = static_cast<u8>(static_cast<u16>(v) & 0xFFu);
        loaded->pcm[i * 2 + 1] = static_cast<u8>(static_cast<u16>(v) >> 8);
    }
    loaded->clip.data = loaded->pcm.data();
    loaded->clip.size = loaded->pcm.size();
    loaded->clip.sampleRate = sampleRate;
    loaded->clip.channels = static_cast<u16>(channels);
    loaded->clip.bitsPerSample = 16;
    loaded->clip.duration = static_cast<f32>(samples.size() / channels) / static_cast<f32>(sampleRate);
    m_impl->clips.emplace(name, std::move(loaded));
    return true;
}

void AudioService::setListener3D(const Listener3D& listener) { m_impl->listener = listener; }

u32 AudioService::playAt3D(const std::string& path, const Vec3& position, f32 refDistance, f32 maxDistance,
                           f32 volume, bool loop) {
    if (!ready()) return 0;
    std::string error;
    Impl::Loaded* clip = m_impl->load(path, &error);
    if (!clip) { CF_WARN("Audio", "%s", error.c_str()); return 0; }
    const SpatialParams p = spatialize(m_impl->listener, position, refDistance, maxDistance);
    AudioSource* source = m_impl->system.playSFX(&clip->clip, p.gain * volume, p.pan);
    if (!source) return 0;
    source->setLoop(loop);
    const u32 handle = m_impl->track(source);
    m_impl->spatial[handle] = Impl::Spatial{position, refDistance, maxDistance, volume};
    return handle;
}

bool AudioService::setSourcePosition3D(u32 handle, const Vec3& position) {
    auto* source = m_impl->find(handle);
    auto it = m_impl->spatial.find(handle);
    if (!source || it == m_impl->spatial.end()) return false;
    it->second.position = position;
    const SpatialParams p = spatialize(m_impl->listener, position, it->second.refDistance, it->second.maxDistance);
    source->setVolume(p.gain * it->second.volume);
    source->setPan(p.pan);
    return true;
}

bool AudioService::stop(u32 h) { auto* s = m_impl->find(h); if (!s) return false; s->stop(); return true; }
bool AudioService::pause(u32 h) { auto* s = m_impl->find(h); if (!s) return false; s->pause(); return true; }
bool AudioService::resume(u32 h) { auto* s = m_impl->find(h); if (!s) return false; s->play(); return true; }
bool AudioService::setVolume(u32 h, f32 v) { auto* s = m_impl->find(h); if (!s) return false; s->setVolume(v); return true; }
bool AudioService::setLoop(u32 h, bool l) { auto* s = m_impl->find(h); if (!s) return false; s->setLoop(l); return true; }
bool AudioService::isPlaying(u32 h) const { auto* s = m_impl->find(h); return s && s->isPlaying(); }

void AudioService::stopAll() {
    for (auto& [_, s] : m_impl->handles) if (!s->isFree()) s->stop();
    m_impl->handles.clear();
    m_impl->spatial.clear();
}

void AudioService::setMasterVolume(f32 v) { m_impl->system.setMasterVolume(v); }
f32 AudioService::masterVolume() const { return m_impl->system.masterVolume(); }
void AudioService::setListener(f32 x, f32 y) { m_impl->system.setListenerPosition(Vec2{x, y}); }
void AudioService::update(f32 dt) { m_impl->system.update(static_cast<f64>(dt)); }

#else  // no SDL3: every call is a no-op

struct AudioService::Impl {};
AudioService::AudioService() : m_impl(new Impl()) {}
AudioService::~AudioService() = default;
bool AudioService::init() { return false; }
void AudioService::shutdown() {}
bool AudioService::ready() const { return false; }
void AudioService::setSearchRoot(const std::string&) {}
bool AudioService::preload(const std::string&, std::string* error) { if (error) *error = "audio not built in"; return false; }
f32 AudioService::duration(const std::string&) const { return 0.0f; }
u32 AudioService::play(const std::string&, f32, f32, bool) { return 0; }
u32 AudioService::playAt(const std::string&, f32, f32, f32, f32) { return 0; }
bool AudioService::registerSamples(const std::string&, const std::vector<f32>&, u32, u32) { return false; }
void AudioService::setListener3D(const Listener3D&) {}
u32 AudioService::playAt3D(const std::string&, const Vec3&, f32, f32, f32, bool) { return 0; }
bool AudioService::setSourcePosition3D(u32, const Vec3&) { return false; }
bool AudioService::stop(u32) { return false; }
bool AudioService::pause(u32) { return false; }
bool AudioService::resume(u32) { return false; }
bool AudioService::setVolume(u32, f32) { return false; }
bool AudioService::setLoop(u32, bool) { return false; }
bool AudioService::isPlaying(u32) const { return false; }
void AudioService::stopAll() {}
void AudioService::setMasterVolume(f32) {}
f32 AudioService::masterVolume() const { return 1.0f; }
void AudioService::setListener(f32, f32) {}
void AudioService::update(f32) {}

#endif

}  // namespace Caffeine::Audio
