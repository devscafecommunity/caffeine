#include "audio/volumetric/SceneAudio.hpp"

#include "audio/AudioComponents.hpp"
#include "audio/VolumetricComponents.hpp"
#include "audio/WeatherAudio.hpp"
#include "ecs/World.hpp"
#include "scene/HierarchySystem.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace Caffeine::Audio {

VolumetricClipPtr loadVolumetricClip(const std::string& path, std::string* error) {
#ifdef CF_HAS_SDL3
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length)) {
        if (error) *error = "cannot read WAV: " + path;
        return nullptr;
    }
    SDL_AudioSpec target = spec;
    target.format = SDL_AUDIO_F32;
    if (target.channels > 2) target.channels = 2;
    Uint8* converted = nullptr;
    int convertedLength = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, data, static_cast<int>(length), &target, &converted, &convertedLength);
    SDL_free(data);
    if (!ok || !converted) {
        if (error) *error = "cannot convert audio: " + path;
        return nullptr;
    }
    auto clip = std::make_shared<VolumetricClip>();
    clip->sampleRate = static_cast<u32>(target.freq);
    clip->channels = static_cast<u32>(target.channels);
    clip->samples.resize(static_cast<usize>(convertedLength) / sizeof(f32));
    std::memcpy(clip->samples.data(), converted, clip->samples.size() * sizeof(f32));
    SDL_free(converted);
    return clip;
#else
    if (error) *error = "audio not built in";
    (void)path;
    return nullptr;
#endif
}

namespace {

AcousticMaterial presetMaterial(u8 preset) {
    switch (preset) {
        case 1: return AcousticMaterial::brick();
        case 2: return AcousticMaterial::wood();
        case 3: return AcousticMaterial::glass();
        case 4: return AcousticMaterial::foliage();
        case 5: return AcousticMaterial::curtain();
        default: return AcousticMaterial::concrete();
    }
}

Vec3 column(const Mat4& m, int c) { return Vec3(m(0, c), m(1, c), m(2, c)); }

}  // namespace

struct SceneAudio::Impl {
    struct Tracked {
        VoiceId voice = 0;
        std::string clipPath;
        VolumetricClipPtr clip;
        AudioLayer layer = AudioLayer::World;
        bool finished = false;
        bool loaded = false;
    };
    explicit Impl(VolumetricAudio& a) : audio(a) {}
    VolumetricAudio& audio;
    std::vector<std::string> roots;
    std::unordered_map<std::string, VolumetricClipPtr> clips;
    std::unordered_set<std::string> failed;
    std::unordered_map<u32, Tracked> tracked;
    std::unique_ptr<AcousticBoxWorld> current;
    u64 obstacleHash = 0;
    // The weather's voices.
    VoiceId rainVoice = 0, windVoice = 0;
    VolumetricClipPtr rainClip, windClip;
    std::unordered_map<std::string, VolumetricClipPtr> thunderClips;
    u32 thunderVariant = 0;
    Vec3 listener{0.0f, 0.0f, 0.0f};

    VolumetricClipPtr clip(const std::string& path) {
        auto it = clips.find(path);
        if (it != clips.end()) return it->second;
        if (failed.count(path)) return nullptr;
        namespace fs = std::filesystem;
        std::vector<std::string> candidates{path};
        for (const std::string& root : roots) candidates.push_back((fs::path(root) / path).string());
        for (const std::string& file : candidates) {
            std::error_code ec;
            if (!fs::exists(file, ec)) continue;
            if (VolumetricClipPtr loaded = loadVolumetricClip(file)) {
                clips[path] = loaded;
                return loaded;
            }
        }
        failed.insert(path);
        return nullptr;
    }
};

SceneAudio::SceneAudio(VolumetricAudio& audio) : m_impl(new Impl(audio)) {}
SceneAudio::~SceneAudio() = default;
void SceneAudio::setSearchRoots(std::vector<std::string> roots) {
    m_impl->roots = std::move(roots);
    m_impl->failed.clear();
}

void SceneAudio::sync(ECS::World& world, const Vec3& listenerPosition, const Vec3& listenerForward, f32 interval) {
    Impl& m = *m_impl;
    // ---- The settings component.
    VolumetricAudioComponent settings;
    bool found = false;
    {
        ECS::ComponentQuery q;
        q.with<VolumetricAudioComponent>();
        world.forEach<VolumetricAudioComponent>(q, [&](ECS::Entity, VolumetricAudioComponent& c) {
            if (c.enabled && !found) {
                settings = c;
                found = true;
            }
        });
    }
    m_enabled = found;
    m.listener = listenerPosition;
    if (!found) {
        if (!m.tracked.empty()) {
            auto mixer = m.audio.lock();
            mixer->stopAll();
            mixer->setWorld(nullptr);
            m.tracked.clear();
        }
        m.rainVoice = m.windVoice = 0;
        m_rainLevel = m_windLevel = 0.0f;
        m_obstacles = 0;
        m_missing = 0;
        return;
    }

    // ---- The obstacles: a new box world only when they changed (the audio thread keeps using the old one meanwhile).
    std::vector<AcousticOrientedBox> boxes;
    u64 hash = 1469598103934665603ull;
    auto mix = [&hash](f32 v) {
        u32 bits;
        std::memcpy(&bits, &v, 4);
        hash = (hash ^ bits) * 1099511628211ull;
    };
    {
        ECS::ComponentQuery q;
        q.with<AcousticObstacle>();
        world.forEach<AcousticObstacle>(q, [&](ECS::Entity e, AcousticObstacle& o) {
            if (!o.enabled) return;
            const Mat4 matrix = Scene::computeWorldMatrix(world, e);
            AcousticOrientedBox box;
            box.center = Vec3(matrix(0, 3), matrix(1, 3), matrix(2, 3));
            const Vec3 c0 = column(matrix, 0), c1 = column(matrix, 1), c2 = column(matrix, 2);
            const f32 l0 = std::max(c0.length(), 1e-4f), l1 = std::max(c1.length(), 1e-4f), l2 = std::max(c2.length(), 1e-4f);
            box.axis[0] = c0 / l0;
            box.axis[1] = c1 / l1;
            box.axis[2] = c2 / l2;
            box.half = Vec3(l0 * 0.5f, l1 * 0.5f, l2 * 0.5f);  // a unit cube scaled by the transform
            box.material = o.custom ? AcousticMaterial{o.transmissionLow, o.transmissionHigh, o.reflectivity} : presetMaterial(o.material);
            boxes.push_back(box);
            for (f32 v : {box.center.x, box.center.y, box.center.z, box.half.x, box.half.y, box.half.z, box.axis[0].x, box.axis[0].z,
                          box.axis[2].x, box.axis[2].z, box.material.transmissionLow, box.material.transmissionHigh,
                          box.material.reflectivity})
                mix(v);
        });
    }
    m_obstacles = static_cast<u32>(boxes.size());
    std::unique_ptr<AcousticBoxWorld> fresh;
    if (!m.current || hash != m.obstacleHash) {
        fresh = std::make_unique<AcousticBoxWorld>();
        for (const auto& b : boxes) fresh->add(b);
        m.obstacleHash = hash;
    }

    // ---- The emitters, as they are now.
    struct Wanted {
        u32 entity;
        AudioEmitter emitter;
        Vec3 position;
    };
    std::vector<Wanted> wanted;
    {
        ECS::ComponentQuery q;
        q.with<AudioEmitter>();
        world.forEach<AudioEmitter>(q, [&](ECS::Entity e, AudioEmitter& emitter) {
            const Mat4 matrix = Scene::computeWorldMatrix(world, e);
            wanted.push_back({e.id(), emitter, Vec3(matrix(0, 3), matrix(1, 3), matrix(2, 3))});
        });
    }

    // Clips are decoded here, on the caller's thread, before the lock is taken.
    struct Plan {
        const Wanted* w;
        VolumetricClipPtr clip;
    };
    std::vector<Plan> plans;
    m_missing = 0;
    for (const Wanted& w : wanted) {
        const std::string path = w.emitter.clipPath.cStr();
        VolumetricClipPtr clip;
        auto it = m.tracked.find(w.entity);
        if (it != m.tracked.end() && it->second.clipPath == path && it->second.clip) clip = it->second.clip;
        else if (!path.empty()) clip = m.clip(path);
        if (!path.empty() && !clip) ++m_missing;
        plans.push_back({&w, clip});
    }

    auto mixer = m.audio.lock();
    if (fresh) {
        mixer->setWorld(fresh.get());
        m.current.swap(fresh);  // `fresh` now holds the old world, freed after the lock is released below
    }
    VolumetricSettings& s = mixer->settings();
    s.occlusion = settings.occlusion;
    s.diffraction = settings.diffraction;
    s.reverb = settings.reverb;
    s.airAbsorption = settings.airAbsorption;
    s.propagationDelay = settings.propagationDelay;
    s.dopplerScale = std::max(settings.dopplerScale, 0.0f);
    s.speedOfSound = std::max(settings.speedOfSound, 50.0f);
    s.masterVolume = std::max(settings.masterVolume, 0.0f);
    mixer->setLayerVolume(AudioLayer::World, settings.worldVolume);
    mixer->setLayerVolume(AudioLayer::Ambient, settings.ambientVolume);
    mixer->setLayerVolume(AudioLayer::Music, settings.musicVolume);
    mixer->moveListener(listenerPosition, listenerForward, Vec3(0.0f, 1.0f, 0.0f), interval);

    std::unordered_set<u32> seen;
    for (const Plan& plan : plans) {
        const Wanted& w = *plan.w;
        seen.insert(w.entity);
        const std::string path = w.emitter.clipPath.cStr();
        const AudioLayer layer = static_cast<AudioLayer>(std::min<u8>(w.emitter.layer, 3));
        auto it = m.tracked.find(w.entity);
        if (it != m.tracked.end() && (it->second.clipPath != path || it->second.layer != layer)) {
            mixer->stop(it->second.voice);
            m.tracked.erase(it);
            it = m.tracked.end();
        }
        if (it == m.tracked.end()) {
            Impl::Tracked t;
            t.clipPath = path;
            t.clip = plan.clip;
            t.layer = layer;
            t.loaded = plan.clip != nullptr;
            if (plan.clip && w.emitter.playOnSpawn) {
                VoiceParams p;
                p.clip = plan.clip;
                p.position = w.position;
                p.volume = w.emitter.volume;
                p.loop = w.emitter.loop;
                p.layer = layer;
                p.refDistance = std::max(w.emitter.refDistance, 0.05f);
                p.maxDistance = std::max(w.emitter.maxDistance, p.refDistance + 1.0f);
                p.doppler = w.emitter.doppler;
                t.voice = mixer->play(p);
            } else {
                t.finished = !w.emitter.playOnSpawn;
            }
            it = m.tracked.emplace(w.entity, std::move(t)).first;
        }
        Impl::Tracked& t = it->second;
        if (t.voice && mixer->playing(t.voice)) {
            mixer->moveVoice(t.voice, w.position, interval);
            mixer->setVolume(t.voice, w.emitter.volume);
        } else if (t.voice) {
            t.voice = 0;
            t.finished = true;
        }
    }
    for (auto it = m.tracked.begin(); it != m.tracked.end();) {
        if (!seen.count(it->first)) {
            mixer->stop(it->second.voice);
            it = m.tracked.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {
VolumetricClipPtr toClip(const ThunderClip& made) {
    auto clip = std::make_shared<VolumetricClip>();
    clip->sampleRate = made.sampleRate;
    clip->channels = 1;
    clip->samples = made.samples;
    return clip;
}
}  // namespace

void SceneAudio::feedWeather(const WeatherFeed& weather, const ThunderClap* claps, u32 count, f32 interval) {
    Impl& m = *m_impl;
    if (!m_enabled) return;
    WeatherSound sound;
    sound.rain = weather.rain;
    sound.rainIntensity = weather.rainIntensity;
    sound.windSpeed = weather.windSpeed;
    sound.gust = weather.gust;
    const WeatherLevels target = weather.enabled ? weatherLevels(sound) : WeatherLevels{};
    // The beds follow the weather smoothly: rain does not begin or stop at a click.
    const f32 k = 1.0f - std::exp(-std::max(interval, 0.0f) / 0.6f);
    m_rainLevel += (target.rain - m_rainLevel) * k;
    m_windLevel += (target.wind - m_windLevel) * k;
    const f32 volume = std::clamp(weather.volume, 0.0f, 1.0f);

    auto bed = [&](VoiceId& voice, VolumetricClipPtr& clip, f32 level, auto make) {
        auto mixer = m.audio.lock();
        if (voice != 0 && !mixer->playing(voice)) voice = 0;
        if (voice == 0) {
            if (level < 0.01f) return;
            if (!clip) clip = toClip(make());
            VoiceParams p;
            p.clip = clip;
            p.loop = true;
            p.layer = AudioLayer::Ambient;
            p.volume = level * volume;
            voice = mixer->play(p);
        } else if (level < 0.003f) {
            mixer->stop(voice);
            voice = 0;
        } else {
            mixer->setVolume(voice, level * volume);
        }
    };
    bed(m.rainVoice, m.rainClip, m_rainLevel, [] { return synthesizeRainLoop(7); });
    bed(m.windVoice, m.windClip, m_windLevel, [] { return synthesizeWindLoop(11); });

    if (!weather.enabled || count == 0) return;
    auto mixer = m.audio.lock();
    for (u32 i = 0; i < count; ++i) {
        const ThunderClap& clap = claps[i];
        // A few variants per distance band keep the clips cached and the claps from sounding alike.
        const u32 band = static_cast<u32>(std::clamp(clap.distance, 50.0f, 8000.0f) / 150.0f);
        const u32 variant = m.thunderVariant++ % 3u;
        const std::string name = std::to_string(band) + ":" + std::to_string(variant);
        auto it = m.thunderClips.find(name);
        if (it == m.thunderClips.end()) {
            it = m.thunderClips.emplace(name, toClip(synthesizeThunder(static_cast<f32>(band) * 150.0f + 75.0f, band * 3u + variant))).first;
        }
        // The strike is where it was in the sky: its bearing and distance from the listener, up at the cloud base.
        const f32 elevation = std::atan2(1500.0f, std::max(clap.distance, 50.0f));
        const Vec3 direction(std::sin(clap.azimuth) * std::cos(elevation), std::sin(elevation), std::cos(clap.azimuth) * std::cos(elevation));
        VoiceParams p;
        p.clip = it->second;
        p.position = m.listener + direction * clap.distance;
        p.volume = std::clamp(clap.volume, 0.0f, 1.0f) * volume * 1.5f;
        p.layer = AudioLayer::World;
        // The clip and the event's volume already carry the distance; the sound has also already travelled.
        p.refDistance = std::max(clap.distance, 1.0f);
        p.maxDistance = clap.distance * 4.0f + 100.0f;
        p.propagate = false;
        if (mixer->play(p) != 0) ++m_thunderPlayed;
    }
}

bool SceneAudio::weatherRainPlaying() const { return m_impl->rainVoice != 0; }
bool SceneAudio::weatherWindPlaying() const { return m_impl->windVoice != 0; }

std::vector<SceneAudio::VoiceInfo> SceneAudio::voices() const {
    std::vector<VoiceInfo> out;
    auto mixer = m_impl->audio.lock();
    for (const auto& [entity, t] : m_impl->tracked) {
        VoiceInfo info;
        info.entity = entity;
        info.clip = t.clipPath;
        info.layer = t.layer;
        info.loaded = t.loaded;
        info.playing = t.voice != 0 && mixer->playing(t.voice);
        if (info.playing) info.debug = mixer->debug(t.voice);
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const VoiceInfo& a, const VoiceInfo& b) { return a.entity < b.entity; });
    return out;
}

RoomEstimate SceneAudio::room() const {
    auto mixer = m_impl->audio.lock();
    return mixer->room();
}

}  // namespace Caffeine::Audio
