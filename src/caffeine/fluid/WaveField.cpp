#include "WaveField.hpp"

#include "math/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Fluids {
namespace {

/// Small deterministic RNG so the same seed always produces the same sea.
struct Rng {
    u32 state = 1u;

    explicit Rng(u32 seed) : state(seed ? seed : 1u) {}

    u32 next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    f32 unit() { return static_cast<f32>(next()) / 4294967295.0f; }

    f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * unit(); }
};

f32 smoothstep(f32 edge0, f32 edge1, f32 value) {
    const f32 t = Math::saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

int defaultWaveCount(WaterQuality quality) {
    switch (quality) {
        case WaterQuality::Flat: return 0;
        case WaterQuality::Wind: return 5;
        case WaterQuality::Realistic: return 12;
    }
    return 0;
}

f32 defaultMaxWavelength(WaterQuality quality) {
    return quality == WaterQuality::Wind ? 42.0f : 96.0f;
}

f32 defaultMinWavelength(WaterQuality quality) {
    return quality == WaterQuality::Wind ? 3.5f : 0.65f;
}

f32 defaultSpreadDeg(WaterQuality quality) {
    return quality == WaterQuality::Wind ? 28.0f : 68.0f;
}

}  // namespace

void WaveField::configure(const WaveSettings& settings) {
    m_settings = settings;
    m_waves.clear();

    if (settings.quality == WaterQuality::Flat || settings.windSpeed <= 0.01f) {
        ++m_revision;
        return;
    }

    const int count = settings.waveCount > 0 ? settings.waveCount
                                              : defaultWaveCount(settings.quality);
    if (count <= 0) {
        ++m_revision;
        return;
    }

    Rng rng(settings.seed);
    const f32 baseAngle = Math::degToRad(settings.windDirectionDeg);
    const f32 spread = Math::degToRad(defaultSpreadDeg(settings.quality));

    // Fully developed sea significant wave height (Pierson-Moskowitz).
    const f32 wind = std::max(settings.windSpeed, 0.0f);
    const f32 significant = 0.21f * wind * wind / std::max(settings.gravity, 0.01f);
    const f32 maxWavelength = defaultMaxWavelength(settings.quality) * settings.wavelengthScale;
    const f32 minWavelength = defaultMinWavelength(settings.quality) * settings.wavelengthScale;

    std::vector<f32> weights;
    weights.reserve(static_cast<size_t>(count));
    std::vector<GerstnerWave> waves;
    waves.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        // Low frequencies carry more energy, so the spectrum is denser there.
        const f32 t = static_cast<f32>(i) / static_cast<f32>(std::max(1, count - 1));
        const f32 shaped = t * t;
        const f32 wavelength =
            std::max(0.05f, (maxWavelength + (minWavelength - maxWavelength) * shaped) *
                                rng.range(0.92f, 1.08f));
        const f32 angle = baseAngle + rng.range(-spread, spread);
        const f32 weight = 1.0f / (1.0f + t * 2.6f);
        weights.push_back(weight);

        GerstnerWave wave;
        wave.direction = Vec2(std::cos(angle), std::sin(angle)).normalized();
        wave.wavelength = wavelength;
        wave.steepness = 0.0f;
        wave.phase = rng.range(0.0f, Math::TAU);
        waves.push_back(wave);
    }

    f32 weightSum = 0.0f;
    for (f32 weight : weights) weightSum += weight;
    if (weightSum <= 0.0f) weightSum = 1.0f;

    // Peak-to-trough is roughly 2 * sum(A), so sum(A) is half the significant height.
    const f32 amplitudeBudget = significant * 0.5f * settings.amplitudeScale;
    for (size_t i = 0; i < waves.size(); ++i) {
        GerstnerWave& wave = waves[i];
        const f32 k = Math::TAU / wave.wavelength;
        wave.amplitude = amplitudeBudget * (weights[i] / weightSum);
        wave.amplitude = std::max(wave.amplitude, 0.0005f);

        const f32 kd = k * std::max(settings.depth, 0.01f);
        const f32 tanhKd = std::tanh(kd);
        wave.omega = std::sqrt(std::max(settings.gravity * k * tanhKd, 0.0f));

        // Keep sum(Q * k * A) below 1 so crests never self-intersect. Using
        // most of the budget makes storm crests compress enough to form foam.
        const f32 perWaveLimit = 0.85f / static_cast<f32>(waves.size());
        const f32 q = settings.choppiness * perWaveLimit / std::max(k * wave.amplitude, 1.0e-5f);
        wave.steepness = std::clamp(q, 0.0f, 1.2f);
    }

    std::sort(waves.begin(), waves.end(),
              [](const GerstnerWave& a, const GerstnerWave& b) {
                  return a.wavelength > b.wavelength;
              });

    m_waves = std::move(waves);
    ++m_revision;
}

WaveSample WaveField::sample(f32 x, f32 z, f32 time) const {
    WaveSample out;
    if (m_waves.empty()) return out;

    f32 dx = 0.0f;
    f32 dz = 0.0f;
    f32 height = 0.0f;

    // Partials of P(x, z).
    f32 px_x = 0.0f, px_y = 0.0f, px_z = 0.0f;
    f32 pz_x = 0.0f, pz_y = 0.0f, pz_z = 0.0f;
    f32 steepness = 0.0f;

    for (const GerstnerWave& wave : m_waves) {
        const f32 k = Math::TAU / wave.wavelength;
        const f32 theta =
            k * (wave.direction.x * x + wave.direction.y * z) - wave.omega * time + wave.phase;
        const f32 c = std::cos(theta);
        const f32 s = std::sin(theta);

        const f32 qa = wave.steepness * wave.amplitude;
        dx += qa * wave.direction.x * c;
        dz += qa * wave.direction.y * c;
        height += wave.amplitude * s;

        const f32 ka = k * wave.amplitude;
        px_x -= qa * wave.direction.x * wave.direction.x * k * s;
        px_y += ka * wave.direction.x * c;
        px_z -= qa * wave.direction.x * wave.direction.y * k * s;

        pz_x -= qa * wave.direction.x * wave.direction.y * k * s;
        pz_y += ka * wave.direction.y * c;
        pz_z -= qa * wave.direction.y * wave.direction.y * k * s;

        steepness += wave.steepness * ka;
    }

    const Vec3 dpdx(1.0f + px_x, px_y, px_z);
    const Vec3 dpdz(pz_x, pz_y, 1.0f + pz_z);
    Vec3 normal = dpdz.cross(dpdx);
    const f32 len = normal.length();
    normal = len > 1.0e-6f ? normal / len : Vec3(0.0f, 1.0f, 0.0f);

    const f32 jacobian = (1.0f + px_x) * (1.0f + pz_z) - px_z * pz_x;

    // Whitecaps form where the surface compresses and the sea is steep enough.
    const f32 crest = smoothstep(0.06f, 0.45f, steepness);
    const f32 compressed = 1.0f - smoothstep(m_settings.foamThreshold - 0.12f,
                                             m_settings.foamThreshold + 0.25f, jacobian);
    out.foam = Math::saturate(compressed * crest);

    out.displacement = Vec3(dx, height, dz);
    out.normal = normal;
    out.height = height;
    out.jacobian = jacobian;
    out.steepness = steepness;
    return out;
}

f32 WaveField::heightAt(f32 x, f32 z, f32 time) const {
    return sample(x, z, time).height;
}

Vec3 WaveField::normalAt(f32 x, f32 z, f32 time) const {
    return sample(x, z, time).normal;
}

f32 WaveField::significantWaveHeight() const {
    if (m_settings.quality == WaterQuality::Flat) return 0.0f;
    const f32 wind = std::max(m_settings.windSpeed, 0.0f);
    return 0.21f * wind * wind / std::max(m_settings.gravity, 0.01f) *
           m_settings.amplitudeScale;
}

}  // namespace Caffeine::Fluids
