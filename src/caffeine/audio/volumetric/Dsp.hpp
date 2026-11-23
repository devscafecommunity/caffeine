#pragma once

// The small signal-processing blocks the volumetric mixer is made of. Plain C++, no allocation while mixing.

#include "core/Types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Caffeine::Audio::dsp {

constexpr f32 kTwoPi = 6.28318530718f;

/// The coefficient of a one-pole low-pass for `cutoffHz` at `sampleRate`.
inline f32 lowpassCoefficient(f32 cutoffHz, f32 sampleRate) {
    const f32 c = std::clamp(cutoffHz, 20.0f, sampleRate * 0.45f);
    return 1.0f - std::exp(-kTwoPi * c / sampleRate);
}

struct OnePole {
    f32 state = 0.0f;
    f32 process(f32 x, f32 coefficient) {
        state += coefficient * (x - state);
        return state;
    }
};

/// A ring buffer read at a fractional distance behind the write head (cubic interpolation).
class DelayLine {
public:
    explicit DelayLine(u32 capacity = 1) { resize(capacity); }
    void resize(u32 capacity) {
        m_buffer.assign(std::max<u32>(capacity, 8) + 4, 0.0f);
        m_write = 0;
    }
    void push(f32 x) {
        m_buffer[m_write] = x;
        m_write = (m_write + 1) % static_cast<u32>(m_buffer.size());
    }
    /// The sample written `delay` samples ago (delay >= 1 reads the newest at 1).
    f32 read(f32 delay) const {
        const u32 size = static_cast<u32>(m_buffer.size());
        delay = std::clamp(delay, 1.0f, static_cast<f32>(size - 4));
        const f32 position = static_cast<f32>(m_write) - delay;
        f32 floorPos = std::floor(position);
        const f32 t = position - floorPos;
        auto at = [&](i32 offset) {
            i64 i = static_cast<i64>(floorPos) + offset;
            i %= static_cast<i64>(size);
            if (i < 0) i += size;
            return m_buffer[static_cast<u32>(i)];
        };
        const f32 y0 = at(-1), y1 = at(0), y2 = at(1), y3 = at(2);
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }
    u32 capacity() const { return static_cast<u32>(m_buffer.size()) - 4; }

private:
    std::vector<f32> m_buffer;
    u32 m_write = 0;
};

/// A feedback delay network: eight delay lines mixed through an orthogonal matrix, each with a loss that gives the
/// chosen RT60 and a low-pass that darkens the tail. Mono in, stereo out.
class Reverb {
public:
    explicit Reverb(f32 sampleRate = 44100.0f) : m_rate(sampleRate) {
        static const u32 base[kLines] = {1117, 1451, 1693, 1951, 2203, 2477, 2741, 3011};
        for (u32 i = 0; i < kLines; ++i) {
            m_length[i] = static_cast<u32>(static_cast<f32>(base[i]) * sampleRate / 44100.0f);
            m_lines[i].resize(m_length[i] + 8);
        }
        m_pre.resize(static_cast<u32>(sampleRate * 0.2f));
        setDecay(1.0f, 0.5f);
    }

    /// `rt60` seconds, `damping` 0 (bright) .. 1 (dull), `predelay` seconds.
    void setDecay(f32 rt60, f32 damping, f32 predelay = 0.0f) {
        rt60 = std::max(rt60, 0.05f);
        for (u32 i = 0; i < kLines; ++i) {
            m_gain[i] = std::pow(10.0f, -3.0f * static_cast<f32>(m_length[i]) / (m_rate * rt60));
        }
        m_dampCoefficient = lowpassCoefficient(18000.0f * std::pow(1.0f - std::clamp(damping, 0.0f, 0.95f), 2.0f) + 1200.0f, m_rate);
        m_predelay = std::clamp(predelay, 0.0f, 0.19f) * m_rate;
    }

    void process(f32 input, f32& left, f32& right) {
        m_pre.push(input);
        const f32 in = m_predelay >= 1.0f ? m_pre.read(m_predelay) : input;
        f32 out[kLines];
        for (u32 i = 0; i < kLines; ++i) out[i] = m_lines[i].read(static_cast<f32>(m_length[i]));
        // Hadamard mixing (orthogonal): each line gets a signed sum of all of them.
        f32 mixed[kLines];
        for (u32 i = 0; i < kLines; ++i) {
            f32 sum = 0.0f;
            for (u32 j = 0; j < kLines; ++j) {
                const u32 bits = i & j;
                const bool odd = (__builtin_popcount(bits) & 1u) != 0u;
                sum += odd ? -out[j] : out[j];
            }
            mixed[i] = sum * 0.35355339f;  // 1 / sqrt(8)
        }
        for (u32 i = 0; i < kLines; ++i) {
            const f32 fed = m_damp[i].process(mixed[i] * m_gain[i], m_dampCoefficient);
            m_lines[i].push(fed + in * 0.35f);
        }
        left = (out[0] + out[2] + out[4] + out[6]) * 0.5f;
        right = (out[1] + out[3] + out[5] + out[7]) * 0.5f;
    }

private:
    static constexpr u32 kLines = 8;
    f32 m_rate;
    u32 m_length[kLines]{};
    f32 m_gain[kLines]{};
    DelayLine m_lines[kLines];
    OnePole m_damp[kLines];
    DelayLine m_pre;
    f32 m_dampCoefficient = 0.5f;
    f32 m_predelay = 0.0f;
};

}  // namespace Caffeine::Audio::dsp
