#include "audio/volumetric/VolumetricAudio.hpp"

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>

namespace Caffeine::Audio {

#ifdef CF_HAS_SDL3

struct VolumetricAudio::Device {
    SDL_AudioStream* stream = nullptr;
    VolumetricMixer* mixer = nullptr;
    std::mutex* mutex = nullptr;
};

namespace {
void SDLCALL pull(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<VolumetricAudio::Device*>(userdata);
    constexpr u32 kBlock = 256;
    std::vector<f32> block(kBlock * 2);
    int needed = additional;
    while (needed > 0) {
        {
            std::lock_guard<std::mutex> lock(*self->mutex);
            self->mixer->render(block.data(), kBlock);
        }
        SDL_PutAudioStreamData(stream, block.data(), static_cast<int>(block.size() * sizeof(f32)));
        needed -= static_cast<int>(block.size() * sizeof(f32));
    }
}
}  // namespace

VolumetricAudio::VolumetricAudio(u32 sampleRate) : m_mixer(sampleRate) {}
VolumetricAudio::~VolumetricAudio() { closeDevice(); }

namespace {
/// The playback devices in a stable order (by name, then id): SDL lists them in the order the audio server reports them, which
/// can change from one call to the next, and an index into the list has to mean the same device when it is used.
std::vector<std::pair<std::string, SDL_AudioDeviceID>> sortedDevices() {
    std::vector<std::pair<std::string, SDL_AudioDeviceID>> devices;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetAudioDeviceName(ids[i]);
            devices.emplace_back(name ? name : "Audio device", ids[i]);
        }
        SDL_free(ids);
    }
    std::sort(devices.begin(), devices.end());
    return devices;
}
}  // namespace

std::vector<std::string> VolumetricAudio::outputDevices() {
    std::vector<std::string> names;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return names;
    for (const auto& device : sortedDevices()) names.push_back(device.first);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return names;
}

bool VolumetricAudio::openDevice(i32 index) {
    closeDevice();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    SDL_AudioDeviceID target = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (index >= 0) {
        const auto devices = sortedDevices();
        if (static_cast<size_t>(index) < devices.size()) target = devices[static_cast<size_t>(index)].second;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = static_cast<int>(m_mixer.sampleRate());
    auto device = std::make_unique<Device>();
    device->mixer = &m_mixer;
    device->mutex = &m_mutex;
    device->stream = SDL_OpenAudioDeviceStream(target, &spec, pull, device.get());
    if (!device->stream) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(device->stream);
    m_device = std::move(device);
    return true;
}

void VolumetricAudio::closeDevice() {
    if (!m_device) return;
    SDL_DestroyAudioStream(m_device->stream);  // closes the device it opened
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_device.reset();
}

bool VolumetricAudio::deviceOpen() const { return m_device != nullptr; }

#else  // no SDL: no device

struct VolumetricAudio::Device {};
VolumetricAudio::VolumetricAudio(u32 sampleRate) : m_mixer(sampleRate) {}
VolumetricAudio::~VolumetricAudio() = default;
std::vector<std::string> VolumetricAudio::outputDevices() { return {}; }
bool VolumetricAudio::openDevice(i32) { return false; }
void VolumetricAudio::closeDevice() {}
bool VolumetricAudio::deviceOpen() const { return false; }

#endif

void VolumetricAudio::monitor(f32 seconds) {
    if (deviceOpen() || seconds <= 0.0f) return;
    const u32 frames = static_cast<u32>(seconds * static_cast<f32>(m_mixer.sampleRate()));
    constexpr u32 kBlock = 256;
    if (m_scratch.size() < kBlock * 2) m_scratch.resize(kBlock * 2);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (u32 done = 0; done < frames; done += kBlock) m_mixer.render(m_scratch.data(), std::min(kBlock, frames - done));
}

u32 VolumetricAudio::renderOffline(f32* out, u32 frames) {
    if (deviceOpen() || !out) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    constexpr u32 kBlock = 256;
    u32 done = 0;
    while (done < frames) {
        const u32 n = std::min(kBlock, frames - done);
        m_mixer.render(out + static_cast<size_t>(done) * 2, n);
        done += n;
    }
    return done;
}

}  // namespace Caffeine::Audio
