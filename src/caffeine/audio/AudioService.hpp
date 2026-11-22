#pragma once

// Script-facing audio: loads WAV files from the project, plays them on the shared SFX pool and
// hands out numeric handles, so Lua and Dryad can control a sound after starting it.
//
// Handles are only valid while the sound plays: once it ends, its pool slot can be reused by a
// later sound and an old handle then refers to that one. Without an audio device (CI, headless)
// every call is a safe no-op that returns 0 / false.

#include "audio/Spatial.hpp"
#include "core/Types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Caffeine::Audio {

class AudioService {
public:
    AudioService();
    ~AudioService();
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    /// Opens the default playback device. Returns false when there is none.
    bool init();
    void shutdown();
    bool ready() const;

    /// Relative clip paths resolve against this root (and are tried as given first).
    void setSearchRoot(const std::string& root);

    /// Decodes a WAV into memory without needing a device (used by tests and preloading).
    /// Returns false when the file is missing or not a readable WAV.
    bool preload(const std::string& path, std::string* error = nullptr);
    /// Duration in seconds of a loaded clip, 0 when unknown.
    f32 duration(const std::string& path) const;

    /// Starts a sound; returns a handle (> 0) or 0 when it could not play.
    u32 play(const std::string& path, f32 volume = 1.0f, f32 pan = 0.0f, bool loop = false);
    /// Positional 2D sound: volume and pan come from the distance to the listener.
    u32 playAt(const std::string& path, f32 x, f32 y, f32 maxDistance = 500.0f, f32 volume = 1.0f);

    /// Makes in-memory samples (interleaved, -1..1, `channels` per frame) playable under `name`, as if the file
    /// had been preloaded: made-up sounds (the weather's rain and thunder) need no file. Works without a device.
    /// A name that is already registered keeps its first samples (a looping source reads them as it plays).
    bool registerSamples(const std::string& name, const std::vector<f32>& samples, u32 sampleRate, u32 channels = 1);

    /// 3D sound. The listener is where the ears are; a source at a world position gets its volume from the
    /// distance (`Rolloff::Inverse`) and its pan from the side it is on. The values are computed when the call is made
    /// and again by `setSourcePosition3D`, not every frame by the service.
    void setListener3D(const Listener3D& listener);
    u32 playAt3D(const std::string& path, const Vec3& position, f32 refDistance = 1.0f, f32 maxDistance = 100.0f,
                 f32 volume = 1.0f, bool loop = false);
    /// Moves a source started with `playAt3D`; false when it has ended or was not a 3D source.
    bool setSourcePosition3D(u32 handle, const Vec3& position);

    bool stop(u32 handle);
    bool pause(u32 handle);
    bool resume(u32 handle);
    bool setVolume(u32 handle, f32 volume);
    bool setLoop(u32 handle, bool loop);
    bool isPlaying(u32 handle) const;
    void stopAll();

    void setMasterVolume(f32 volume);
    f32 masterVolume() const;
    void setListener(f32 x, f32 y);

    /// Call once per frame.
    void update(f32 dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace Caffeine::Audio
