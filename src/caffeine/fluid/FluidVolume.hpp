#pragma once

#include "EulerFluid.hpp"

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::Fluids {

struct FluidVolumeSettings {
    int resolutionX = 28;
    int resolutionY = 28;
    int resolutionZ = 28;
    f32 sizeX = 16.0f;
    f32 sizeY = 16.0f;
    f32 sizeZ = 16.0f;
    int pressureIterations = 16;
    u32 seed = 1u;
};

/// 3D stable-fluids volume. This is the real volumetric layer behind fire, fog
/// and smoke: density and temperature are advected by an incompressible 3D
/// velocity field. A renderer can raymarch `densityField()` / `temperatureField()`.
class FluidVolume {
public:
    void configure(const FluidVolumeSettings& settings);
    const FluidVolumeSettings& settings() const { return m_settings; }

    void reset();
    void setMaterial(FluidKind kind);
    void setMaterial(const FluidMaterial& material);
    FluidKind materialKind() const { return m_material.kind; }
    const FluidMaterial& material() const { return m_material; }

    void step(f32 dt);
    void setWind(Vec3 wind) { m_wind = wind; }

    void addDensity(f32 x, f32 y, f32 z, f32 amount, f32 radius);
    void addTemperature(f32 x, f32 y, f32 z, f32 amount, f32 radius);
    void addVelocity(f32 x, f32 y, f32 z, f32 velocityX, f32 velocityY, f32 velocityZ,
                     f32 radius);

    int resolutionX() const { return m_nx; }
    int resolutionY() const { return m_ny; }
    int resolutionZ() const { return m_nz; }
    f32 cellSize() const { return m_dx; }

    f32 densityAt(int i, int j, int k) const;
    f32 temperatureAt(int i, int j, int k) const;
    f32 velocityZAt(int i, int j, int k) const;

    const std::vector<f32>& densityField() const { return m_density; }
    const std::vector<f32>& temperatureField() const { return m_temperature; }

    f32 totalDensity() const;
    Vec3 densityCenterOfMass() const;
    Vec3 temperatureCenterOfMass() const;
    f32 divergenceRms() const;

private:
    int index(int i, int j, int k) const { return (k * m_ny + j) * m_nx + i; }
    f32 sample(const std::vector<f32>& field, f32 x, f32 y, f32 z) const;
    void advect(std::vector<f32>& field, const std::vector<f32>& vx,
                const std::vector<f32>& vy, const std::vector<f32>& vz, f32 dt);
    void project();

    FluidVolumeSettings m_settings;
    FluidMaterial m_material;
    int m_nx = 28;
    int m_ny = 28;
    int m_nz = 28;
    f32 m_dx = 0.57f;
    Vec3 m_wind{0.0f, 0.0f, 0.0f};

    std::vector<f32> m_u;
    std::vector<f32> m_v;
    std::vector<f32> m_w;
    std::vector<f32> m_density;
    std::vector<f32> m_temperature;
    std::vector<f32> m_pressure;
    std::vector<f32> m_divergence;
    std::vector<f32> m_scratch;
};

}  // namespace Caffeine::Fluids
