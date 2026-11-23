#pragma once

// A VolumetricMixer you can leave running: it owns the mixer, a lock, and an optional sound device that pulls blocks
// from the mixer on the audio thread. Without a device (CI, a headless editor, a machine with no sound) the owner calls
// `monitor` once a frame instead, which mixes into nothing so voices and their debug state keep moving.
//
// Every call that touches the mixer goes through `lock()`: the audio thread mixes while the lock is free.

#include "audio/volumetric/VolumetricMixer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Caffeine::Audio {

class VolumetricAudio {
public:
    explicit VolumetricAudio(u32 sampleRate = 48000);
    ~VolumetricAudio();
    VolumetricAudio(const VolumetricAudio&) = delete;
    VolumetricAudio& operator=(const VolumetricAudio&) = delete;

    /// The mixer, held under the lock for as long as the guard lives.
    class Guard {
    public:
        Guard(VolumetricMixer& mixer, std::mutex& mutex) : m_mixer(mixer), m_lock(mutex) {}
        VolumetricMixer* operator->() { return &m_mixer; }
        VolumetricMixer& operator*() { return m_mixer; }

    private:
        VolumetricMixer& m_mixer;
        std::unique_lock<std::mutex> m_lock;
    };
    Guard lock() { return Guard(m_mixer, m_mutex); }

    /// The names of the sound devices that can play (empty when there are none or SDL audio is not built).
    static std::vector<std::string> outputDevices();
    /// Opens a sound device and starts mixing into it: `index` into `outputDevices()`, or -1 for the system default. False
    /// when it cannot be opened (or SDL audio is not built). Opening again moves to the new device.
    bool openDevice(i32 index = -1);
    void closeDevice();
    bool deviceOpen() const;

    /// Mixes `seconds` into a scratch buffer when no device is doing it, so the mixer's time and debug state move on.
    /// Does nothing while a device is open.
    void monitor(f32 seconds);
    /// Mixes `frames` stereo frames for the caller (tests, captures). Only while no device is open; returns the
    /// frames written.
    u32 renderOffline(f32* interleavedStereo, u32 frames);

    struct Device;

private:
    VolumetricMixer m_mixer;
    std::mutex m_mutex;
    std::unique_ptr<Device> m_device;
    std::vector<f32> m_scratch;
};

}  // namespace Caffeine::Audio
