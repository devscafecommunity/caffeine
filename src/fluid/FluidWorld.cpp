#include "FluidWorld.hpp"

#include "math/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Fluids {
namespace {

f32 presetSize(WaterQuality quality) {
    switch (quality) {
        case WaterQuality::Flat: return 48.0f;
        case WaterQuality::Wind: return 64.0f;
        case WaterQuality::Realistic: return 96.0f;
    }
    return 64.0f;
}

f32 presetWind(WaterQuality quality) {
    switch (quality) {
        case WaterQuality::Flat: return 0.0f;
        case WaterQuality::Wind: return 8.5f;
        case WaterQuality::Realistic: return 12.0f;
    }
    return 0.0f;
}

}  // namespace

NaturalFluidSettings waterPreset(WaterQuality quality) {
    NaturalFluidSettings settings;
    const f32 size = presetSize(quality);

    settings.waves.quality = quality;
    settings.waves.windSpeed = presetWind(quality);
    settings.waves.windDirectionDeg = 35.0f;
    settings.waves.amplitudeScale = quality == WaterQuality::Realistic ? 1.0f : 0.8f;
    settings.waves.choppiness = quality == WaterQuality::Realistic ? 0.72f : 0.45f;
    settings.waves.waveCount = quality == WaterQuality::Realistic ? 14 : 5;
    settings.waves.depth = 200.0f;

    settings.shallow.resolution = 64;
    settings.shallow.sizeX = size;
    settings.shallow.sizeZ = size;
    settings.shallow.baseDepth = 2.5f;
    settings.shallow.windSpeed = settings.waves.windSpeed;
    settings.shallow.windDirectionDeg = settings.waves.windDirectionDeg;
    settings.shallow.windStrength = 0.00025f;

    settings.fluid2D.resolution = 96;
    settings.fluid2D.width = size;
    settings.fluid2D.height = size;

    settings.volume.resolutionX = 28;
    settings.volume.resolutionY = 28;
    settings.volume.resolutionZ = 28;
    settings.volume.sizeX = size * 0.35f;
    settings.volume.sizeY = size * 0.35f;
    settings.volume.sizeZ = size * 0.35f;

    // Level 2 combines analytic wind waves with real shallow-water physics.
    // Level 3 keeps the broad Gerstner spectrum; shallow physics stays opt-in.
    settings.simulateShallowWater = quality == WaterQuality::Wind;
    settings.fluidPreset = FluidKind::Water;
    return settings;
}

NaturalFluidSettings fluidPreset(FluidKind kind) {
    NaturalFluidSettings settings = waterPreset(WaterQuality::Realistic);
    settings.simulateShallowWater = false;
    settings.simulateVolume = true;
    settings.fluidPreset = kind;
    settings.volume.resolutionX = 28;
    settings.volume.resolutionY = 28;
    settings.volume.resolutionZ = 28;
    settings.volume.sizeX = 12.0f;
    settings.volume.sizeY = 12.0f;
    settings.volume.sizeZ = 12.0f;
    return settings;
}

void FluidWorld::configure(const NaturalFluidSettings& settings) {
    m_settings = settings;
    m_waveField.configure(m_settings.waves);
    m_shallow.configure(m_settings.shallow);
    m_fluid2D.configure(m_settings.fluid2D);
    m_volume.configure(m_settings.volume);
    applyPreset();
    m_time = 0.0f;
}

void FluidWorld::applyPreset() {
    m_fluid2D.setMaterial(m_settings.fluidPreset);
    m_volume.setMaterial(m_settings.fluidPreset);
}

void FluidWorld::reset() {
    m_waveField.configure(m_settings.waves);
    m_shallow.reset();
    m_fluid2D.reset();
    m_volume.reset();
    m_time = 0.0f;
}

void FluidWorld::update(f32 dt) {
    if (dt <= 0.0f) return;
    const f32 scaled = dt * m_settings.timeScale;
    m_time += scaled;

    if (m_settings.simulateShallowWater) {
        m_shallow.step(scaled);
    }
    if (m_settings.simulateFluid2D) {
        m_fluid2D.step(scaled);
    }
    if (m_settings.simulateVolume) {
        m_volume.step(scaled);
    }
}

WaveSample FluidWorld::sampleSurface(f32 x, f32 z) const {
    WaveSample sample = m_waveField.sample(x, z, m_time);
    if (!m_settings.simulateShallowWater) return sample;

    // Add the solver displacement on top of the analytic wind waves. The
    // wave field still supplies foam and choppiness; the shallow solver adds
    // drops, terrain-driven ripples and physical conservation.
    auto combinedHeight = [&](f32 sx, f32 sz) -> f32 {
        return m_waveField.heightAt(sx, sz, m_time) +
               (m_shallow.heightAtWorld(sx, sz) - m_settings.shallow.baseDepth);
    };
    const f32 height = combinedHeight(x, z);
    const f32 e = std::max(m_shallow.dx() * 0.5f, 0.05f);
    const f32 hL = combinedHeight(x - e, z);
    const f32 hR = combinedHeight(x + e, z);
    const f32 hB = combinedHeight(x, z - e);
    const f32 hT = combinedHeight(x, z + e);
    Vec3 normal(-(hR - hL) / (2.0f * e), 1.0f, -(hT - hB) / (2.0f * e));
    sample.normal = normal.length() > 1.0e-6f ? normal.normalized() : Vec3(0.0f, 1.0f, 0.0f);
    sample.height = height;
    sample.displacement.y = height;
    return sample;
}

f32 FluidWorld::surfaceHeight(f32 x, f32 z) const {
    return sampleSurface(x, z).height;
}

Vec3 FluidWorld::surfaceNormal(f32 x, f32 z) const {
    return sampleSurface(x, z).normal;
}

void FluidWorld::buildSurfaceMesh(int resolution, f32 sizeX, f32 sizeZ, SurfaceMesh& out) const {
    resolution = std::clamp(resolution, 2, 512);
    out.positions.clear();
    out.normals.clear();
    out.texcoords.clear();
    out.indices.clear();

    const int stride = resolution + 1;
    const f32 stepX = sizeX / static_cast<f32>(resolution);
    const f32 stepZ = sizeZ / static_cast<f32>(resolution);
    const f32 originX = -sizeX * 0.5f;
    const f32 originZ = -sizeZ * 0.5f;

    Vec3 boundsMin(1.0e30f, 1.0e30f, 1.0e30f);
    Vec3 boundsMax(-1.0e30f, -1.0e30f, -1.0e30f);

    out.positions.reserve(static_cast<size_t>(stride) * stride);
    out.normals.reserve(static_cast<size_t>(stride) * stride);
    out.texcoords.reserve(static_cast<size_t>(stride) * stride);

    for (int j = 0; j <= resolution; ++j) {
        for (int i = 0; i <= resolution; ++i) {
            const f32 x = originX + static_cast<f32>(i) * stepX;
            const f32 z = originZ + static_cast<f32>(j) * stepZ;
            const WaveSample sample = sampleSurface(x, z);
            Vec3 position(x + sample.displacement.x, sample.height,
                          z + sample.displacement.z);
            if (m_settings.simulateShallowWater) position.y = sample.height;

            out.positions.push_back(position);
            out.normals.push_back(sample.normal);
            out.texcoords.push_back(Vec2(static_cast<f32>(i) / static_cast<f32>(resolution),
                                         static_cast<f32>(j) / static_cast<f32>(resolution)));

            boundsMin.x = std::min(boundsMin.x, position.x);
            boundsMin.y = std::min(boundsMin.y, position.y);
            boundsMin.z = std::min(boundsMin.z, position.z);
            boundsMax.x = std::max(boundsMax.x, position.x);
            boundsMax.y = std::max(boundsMax.y, position.y);
            boundsMax.z = std::max(boundsMax.z, position.z);
        }
    }

    out.indices.reserve(static_cast<size_t>(resolution) * resolution * 6);
    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            const u32 i0 = static_cast<u32>(j * stride + i);
            const u32 i1 = i0 + 1;
            const u32 i2 = i0 + static_cast<u32>(stride);
            const u32 i3 = i2 + 1;
            out.indices.push_back(i0);
            out.indices.push_back(i2);
            out.indices.push_back(i1);
            out.indices.push_back(i1);
            out.indices.push_back(i2);
            out.indices.push_back(i3);
        }
    }

    out.boundsMin = boundsMin;
    out.boundsMax = boundsMax;
}

void FluidWorld::addWaterDrop(f32 x, f32 z, f32 volume, f32 radius) {
    m_shallow.addDrop(x, z, volume, radius);
}

void FluidWorld::addWaterImpulse(f32 x, f32 z, f32 velocityX, f32 velocityZ, f32 radius) {
    m_shallow.addImpulse(x, z, velocityX, velocityZ, radius);
}

void FluidWorld::setWind(f32 speed, f32 directionDeg) {
    m_settings.waves.windSpeed = speed;
    m_settings.waves.windDirectionDeg = directionDeg;
    m_settings.shallow.windSpeed = speed;
    m_settings.shallow.windDirectionDeg = directionDeg;
    m_waveField.configure(m_settings.waves);

    const f32 radians = Math::degToRad(directionDeg);
    const f32 fluidWind = speed * 0.02f;
    m_fluid2D.setWind(Vec2(std::cos(radians) * fluidWind, std::sin(radians) * fluidWind));
    m_volume.setWind(Vec3(std::cos(radians) * fluidWind, 0.0f,
                          std::sin(radians) * fluidWind));
}

void FluidWorld::addFluidSource(f32 x, f32 y, f32 amount, f32 radius) {
    m_fluid2D.addDensity(x, y, amount, radius);
    if (m_fluid2D.material().emissive) {
        m_fluid2D.addTemperature(x, y, amount * 1.4f, radius);
    }
}

void FluidWorld::addVolumeSource(f32 x, f32 y, f32 z, f32 amount, f32 radius) {
    m_volume.addDensity(x, y, z, amount, radius);
    if (m_volume.material().emissive) {
        m_volume.addTemperature(x, y, z, amount * 1.4f, radius);
    }
}

void FluidWorld::addFluidVelocity(f32 x, f32 y, f32 velocityX, f32 velocityY, f32 radius) {
    m_fluid2D.addVelocity(x, y, velocityX, velocityY, radius);
}

}  // namespace Caffeine::Fluids
