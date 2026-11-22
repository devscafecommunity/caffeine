#pragma once

// A clap of thunder made from noise: no sample files, deterministic for a given distance and seed.
//
// The engine raises thunder events (ThunderEvent, caffeine_session_poll_thunder) and plays nothing itself; a game or
// a script that wants to hear them turns each event into a clip with `synthesizeThunder`, writes it with `writeWav16`
// and plays it with the AudioService. Close strikes crack and rumble in full bandwidth; far ones lose their highs
// (the air takes them) and roll for longer.

#include "core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Caffeine::Audio {

struct ThunderClip {
    u32 sampleRate = 22050;
    std::vector<f32> samples;  ///< mono, -1..1
    f32 seconds() const { return sampleRate ? static_cast<f32>(samples.size()) / static_cast<f32>(sampleRate) : 0.0f; }
};

namespace detail {
inline u32 thunderHash(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline f32 thunderNoise(u32& state) {
    state = thunderHash(state + 0x9e3779b9U);
    return static_cast<f32>(state >> 8) / 8388608.0f - 1.0f;
}
}  // namespace detail

/// The clip for a strike `distanceM` metres away. `seed` varies the rolls (the same seed gives the same clip).
inline ThunderClip synthesizeThunder(f32 distanceM, u32 seed, u32 sampleRate = 22050) {
    ThunderClip clip;
    clip.sampleRate = std::max<u32>(sampleRate, 8000);
    const f32 distance = std::max(distanceM, 50.0f);
    const f32 seconds = std::clamp(2.2f + distance / 900.0f, 2.2f, 8.0f);
    const usize count = static_cast<usize>(seconds * static_cast<f32>(clip.sampleRate));
    clip.samples.assign(count, 0.0f);

    // The air takes the highs: the cutoff falls with the distance.
    const f32 cutoff = std::clamp(2600.0f / (1.0f + distance / 350.0f), 90.0f, 2600.0f);
    const f32 alpha = 1.0f - std::exp(-6.28318530718f * cutoff / static_cast<f32>(clip.sampleRate));
    // A second, lower band for the rumble that carries.
    const f32 rumbleAlpha = 1.0f - std::exp(-6.28318530718f * 70.0f / static_cast<f32>(clip.sampleRate));

    // The rolls: a crack at the start (only close ones are sharp) and a few swells that die away.
    struct Roll {
        f32 start;
        f32 attack;
        f32 decay;
        f32 gain;
    };
    u32 rng = seed * 2654435761U + 17U;
    std::vector<Roll> rolls;
    const f32 crack = 1.0f / (1.0f + distance / 250.0f);
    rolls.push_back({0.0f, 0.004f + 0.00004f * distance, 0.18f + distance / 4000.0f, 0.6f + 1.2f * crack});
    const int swells = 4 + static_cast<int>(std::min(distance / 800.0f, 4.0f));
    for (int i = 0; i < swells; ++i) {
        const f32 u = (detail::thunderNoise(rng) + 1.0f) * 0.5f;
        const f32 v = (detail::thunderNoise(rng) + 1.0f) * 0.5f;
        rolls.push_back({0.15f + u * (seconds * 0.55f), 0.08f + 0.1f * v, 0.5f + 0.9f * v + distance / 3000.0f,
                         (0.45f + 0.4f * u) / (1.0f + 0.25f * static_cast<f32>(i))});
    }

    f32 low = 0.0f;
    f32 rumble = 0.0f;
    u32 noiseState = seed ^ 0xA5A5A5A5U;
    for (usize i = 0; i < count; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(clip.sampleRate);
        f32 envelope = 0.0f;
        for (const Roll& roll : rolls) {
            const f32 dt = t - roll.start;
            if (dt < 0.0f) continue;
            const f32 rise = 1.0f - std::exp(-dt / std::max(roll.attack, 1e-3f));
            envelope += roll.gain * rise * std::exp(-dt / roll.decay);
        }
        const f32 n = detail::thunderNoise(noiseState);
        low += alpha * (n - low);
        rumble += rumbleAlpha * (n - rumble);
        clip.samples[i] = envelope * (0.75f * low + 0.9f * rumble * 4.0f);
    }
    // A short fade at the end, then normalise: the loudness comes from the player's volume, not from the clip.
    f32 peak = 1e-6f;
    for (f32 s : clip.samples) peak = std::max(peak, std::fabs(s));
    const usize fade = std::min<usize>(count, static_cast<usize>(0.15f * static_cast<f32>(clip.sampleRate)));
    for (usize i = 0; i < count; ++i) {
        f32 gain = 0.9f / peak;
        if (i + fade > count) gain *= static_cast<f32>(count - i) / static_cast<f32>(fade);
        clip.samples[i] *= gain;
    }
    return clip;
}

/// Writes the clip as a 16-bit mono PCM WAV (what AudioService::play reads). Returns false when the file cannot be
/// written.
inline bool writeWav16(const std::string& path, const ThunderClip& clip) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const u32 dataBytes = static_cast<u32>(clip.samples.size() * 2);
    auto put32 = [&](u32 v) { std::fwrite(&v, 4, 1, file); };
    auto put16 = [&](u16 v) { std::fwrite(&v, 2, 1, file); };
    std::fwrite("RIFF", 1, 4, file);
    put32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, file);
    put32(16);
    put16(1);  // PCM
    put16(1);  // mono
    put32(clip.sampleRate);
    put32(clip.sampleRate * 2);
    put16(2);
    put16(16);
    std::fwrite("data", 1, 4, file);
    put32(dataBytes);
    for (f32 s : clip.samples) {
        const i16 v = static_cast<i16>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&v, 2, 1, file);
    }
    return std::fclose(file) == 0;
}

}  // namespace Caffeine::Audio
