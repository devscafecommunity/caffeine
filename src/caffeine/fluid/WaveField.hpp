#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::Fluids {

/// One directional Gerstner wave.
struct GerstnerWave {
    Vec2 direction{1.0f, 0.0f};
    f32 wavelength = 10.0f;
    f32 amplitude = 0.2f;
    /// Choppiness factor Q. The horizontal displacement is Q * A * dir * cos(theta).
    f32 steepness = 0.4f;
    /// Angular frequency from the finite-depth dispersion relation.
    f32 omega = 1.0f;
    f32 phase = 0.0f;
};

enum class WaterQuality : int {
    /// Level 1: perfectly flat water.
    Flat = 0,
    /// Level 2: a few wind-driven Gerstner waves.
    Wind = 1,
    /// Level 3: a broad directional spectrum with choppiness, foam and depth attenuation.
    Realistic = 2,
};

struct WaveSettings {
    WaterQuality quality = WaterQuality::Realistic;
    f32 windSpeed = 8.0f;
    f32 windDirectionDeg = 35.0f;
    f32 amplitudeScale = 1.0f;
    f32 choppiness = 0.65f;
    f32 wavelengthScale = 1.0f;
    /// Water depth in metres. Large values approach deep-water dispersion.
    f32 depth = 200.0f;
    f32 gravity = 9.81f;
    /// Jacobian below which whitecap foam appears.
    f32 foamThreshold = 0.72f;
    u32 seed = 1337u;
    /// 0 selects the default count for the quality level.
    int waveCount = 0;
};

struct WaveSample {
    /// Surface displacement from the rest position. y is the wave height.
    Vec3 displacement{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 height = 0.0f;
    /// Horizontal Jacobian. Values below 1 mean compressed crests.
    f32 jacobian = 1.0f;
    /// 0..1 whitecap foam amount.
    f32 foam = 0.0f;
    /// Sum of k * A over the waves, a cheap sea-state measure.
    f32 steepness = 0.0f;
};

/// Analytic Gerstner wave field. Deterministic for a given seed and settings,
/// so the CPU, the editor preview and a shader can share the same parameters.
class WaveField {
public:
    void configure(const WaveSettings& settings);
    const WaveSettings& settings() const { return m_settings; }
    const std::vector<GerstnerWave>& waves() const { return m_waves; }
    u32 revision() const { return m_revision; }

    /// Samples the displaced surface. (x, z) is the rest position.
    WaveSample sample(f32 x, f32 z, f32 time) const;

    /// Surface height at a rest position.
    f32 heightAt(f32 x, f32 z, f32 time) const;

    /// Surface normal at a rest position.
    Vec3 normalAt(f32 x, f32 z, f32 time) const;

    /// Significant wave height implied by the wind (0 for flat water).
    f32 significantWaveHeight() const;

private:
    WaveSettings m_settings;
    std::vector<GerstnerWave> m_waves;
    u32 m_revision = 1;
};

}  // namespace Caffeine::Fluids
