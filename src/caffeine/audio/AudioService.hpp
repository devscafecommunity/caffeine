#pragma once

// Script-facing audio: loads WAV files from the project, plays them on the shared SFX pool and
// hands out numeric handles, so Lua and Dryad can control a sound after starting it.
//
// Handles are only valid while the sound plays: once it ends, its pool slot can be reused by a
// later sound and an old handle then refers to that one. Without an audio device (CI, headless)
// every call is a safe no-op that returns 0 / false.

#include "core/Types.hpp"

#include <memory>
#include <string>

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
