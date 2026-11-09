#pragma once

#include "EulerFluid.hpp"
#include "FluidVolume.hpp"
#include "ShallowWater.hpp"
#include "WaveField.hpp"

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::Fluids {

struct SurfaceMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;
    std::vector<u32> indices;
    Vec3 boundsMin{0.0f, 0.0f, 0.0f};
    Vec3 boundsMax{0.0f, 0.0f, 0.0f};
};

struct NaturalFluidSettings {
    WaveSettings waves;
    ShallowWaterSettings shallow;
    Euler2DSettings fluid2D;
    FluidVolumeSettings volume;

    /// Drives the surface from the shallow-water solver instead of analytic waves.
    bool simulateShallowWater = false;
    /// Enables the 2D fluid slice (water/fire/fog/smoke).
    bool simulateFluid2D = false;
    /// Enables the volumetric 3D solver (fire/fog/smoke/water).
    bool simulateVolume = false;

    FluidKind fluidPreset = FluidKind::Water;
    f32 timeScale = 1.0f;
};

/// High-level façade shared by the lab and the Doppio plugin.
///
/// Levels 1-3 use the analytic Gerstner surface (optionally with shallow-water
/// physics). Level 4 uses the Eulerian solvers for real fluids.
class FluidWorld {
public:
    void configure(const NaturalFluidSettings& settings);
    const NaturalFluidSettings& settings() const { return m_settings; }

    void reset();
    void update(f32 dt);

    f32 time() const { return m_time; }

    WaveSample sampleSurface(f32 x, f32 z) const;
    f32 surfaceHeight(f32 x, f32 z) const;
    Vec3 surfaceNormal(f32 x, f32 z) const;

    /// Builds a renderable water mesh. `resolution` is the number of quads per axis.
    void buildSurfaceMesh(int resolution, f32 sizeX, f32 sizeZ, SurfaceMesh& out) const;

    void addWaterDrop(f32 x, f32 z, f32 volume, f32 radius);
    void addWaterImpulse(f32 x, f32 z, f32 velocityX, f32 velocityZ, f32 radius);
    void setWind(f32 speed, f32 directionDeg);
    void addFluidSource(f32 x, f32 y, f32 amount, f32 radius);
    void addVolumeSource(f32 x, f32 y, f32 z, f32 amount, f32 radius);
    void addFluidVelocity(f32 x, f32 y, f32 velocityX, f32 velocityY, f32 radius);

    WaveField& waves() { return m_waveField; }
    const WaveField& waves() const { return m_waveField; }
    ShallowWater& shallow() { return m_shallow; }
    const ShallowWater& shallow() const { return m_shallow; }
    EulerFluid2D& fluid2D() { return m_fluid2D; }
    const EulerFluid2D& fluid2D() const { return m_fluid2D; }
    FluidVolume& volume() { return m_volume; }
    const FluidVolume& volume() const { return m_volume; }

private:
    void applyPreset();

    NaturalFluidSettings m_settings;
    WaveField m_waveField;
    ShallowWater m_shallow;
    EulerFluid2D m_fluid2D;
    FluidVolume m_volume;
    f32 m_time = 0.0f;
};

/// Applies one of the four product levels to a settings block.
NaturalFluidSettings waterPreset(WaterQuality quality);
NaturalFluidSettings fluidPreset(FluidKind kind);

}  // namespace Caffeine::Fluids
