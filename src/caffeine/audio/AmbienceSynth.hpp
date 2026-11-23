#pragma once

// Looping weather ambience made from noise: a rain bed and a wind bed, seamless at the loop point and
// deterministic for a seed. Like ThunderSynth there are no sample files.

#include "audio/ThunderSynth.hpp"

namespace Caffeine::Audio {

namespace detail {
/// Makes a clip loop without a click: the last `fade` samples are crossfaded into the first ones and dropped.
inline void makeSeamless(ThunderClip& clip, usize fade) {
    const usize count = clip.samples.size();
    fade = std::min(fade, count / 3);
    if (fade == 0) return;
    for (usize i = 0; i < fade; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(fade);
        clip.samples[i] = clip.samples[i] * t + clip.samples[count - fade + i] * (1.0f - t);
    }
    clip.samples.resize(count - fade);
}
}  // namespace detail

/// A steady rain bed: a broad hiss with a body of lower noise and the fine, irregular ticks of drops. About
/// `seconds` long once the loop point is closed.
inline ThunderClip synthesizeRainLoop(u32 seed, f32 seconds = 6.0f, u32 sampleRate = 22050) {
    ThunderClip clip;
    clip.sampleRate = std::max<u32>(sampleRate, 8000);
    const f32 rate = static_cast<f32>(clip.sampleRate);
    const usize fade = static_cast<usize>(0.4f * rate);
    const usize count = static_cast<usize>(std::max(seconds, 1.0f) * rate) + fade;
    clip.samples.assign(count, 0.0f);
    u32 rng = seed * 2246822519U + 3U;
    f32 low = 0.0f;
    f32 band = 0.0f;
    f32 tick = 0.0f;
    const f32 lowAlpha = 1.0f - std::exp(-6.28318530718f * 900.0f / rate);
    const f32 bandAlpha = 1.0f - std::exp(-6.28318530718f * 4500.0f / rate);
    for (usize i = 0; i < count; ++i) {
        const f32 n = detail::thunderNoise(rng);
        low += lowAlpha * (n - low);
        band += bandAlpha * (n - band);
        const f32 hiss = band - low;  // a band between roughly 900 Hz and 4.5 kHz
        // Drops: sparse short bursts of the band's upper end.
        if ((detail::thunderHash(rng + static_cast<u32>(i)) & 0x3FFu) == 0u) tick = 1.0f;
        tick *= 0.985f;
        clip.samples[i] = 0.55f * hiss + 0.35f * low + 0.9f * tick * detail::thunderNoise(rng);
    }
    detail::makeSeamless(clip, fade);
    f32 peak = 1e-6f;
    for (f32 s : clip.samples) peak = std::max(peak, std::fabs(s));
    for (f32& s : clip.samples) s *= 0.8f / peak;
    return clip;
}

/// A wind bed: low noise whose loudness and brightness swell and ebb slowly.
inline ThunderClip synthesizeWindLoop(u32 seed, f32 seconds = 8.0f, u32 sampleRate = 22050) {
    ThunderClip clip;
    clip.sampleRate = std::max<u32>(sampleRate, 8000);
    const f32 rate = static_cast<f32>(clip.sampleRate);
    const usize fade = static_cast<usize>(0.8f * rate);
    const usize count = static_cast<usize>(std::max(seconds, 2.0f) * rate) + fade;
    clip.samples.assign(count, 0.0f);
    u32 rng = seed * 3266489917U + 11U;
    f32 low = 0.0f;
    f32 body = 0.0f;
    for (usize i = 0; i < count; ++i) {
        const f32 t = static_cast<f32>(i) / rate;
        const f32 swell = 0.55f + 0.3f * std::sin(0.9f * t + 0.3f) + 0.15f * std::sin(2.3f * t + 1.1f);
        const f32 cutoff = 160.0f + 520.0f * swell * swell;
        const f32 alpha = 1.0f - std::exp(-6.28318530718f * cutoff / rate);
        const f32 n = detail::thunderNoise(rng);
        low += alpha * (n - low);
        body += (1.0f - std::exp(-6.28318530718f * 60.0f / rate)) * (n - body);
        clip.samples[i] = swell * (1.3f * low + 2.5f * body);
    }
    detail::makeSeamless(clip, fade);
    f32 peak = 1e-6f;
    for (f32 s : clip.samples) peak = std::max(peak, std::fabs(s));
    for (f32& s : clip.samples) s *= 0.8f / peak;
    return clip;
}

}  // namespace Caffeine::Audio
