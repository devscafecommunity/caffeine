#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::Fluids {

struct ShallowWaterSettings {
    int resolution = 64;
    f32 sizeX = 64.0f;
    f32 sizeZ = 64.0f;
    f32 baseDepth = 2.0f;
    f32 gravity = 9.81f;
    /// Momentum friction (1/s).
    f32 friction = 0.02f;
    /// Extra velocity damping (1/s).
    f32 damping = 0.004f;
    f32 windSpeed = 0.0f;
    f32 windDirectionDeg = 0.0f;
    /// Converts wind speed squared into surface acceleration.
    f32 windStrength = 0.0002f;
    u32 seed = 1u;
};

/// Explicit conservative shallow-water solver on a regular height-field grid.
///
/// The solver uses face-staggered velocities and upwind face depths, so a closed
/// basin keeps its water volume (minus boundary flux, which is zero here).
/// It is the "water with physics" layer: wind stress, terrain beds, drops and
/// buoyant objects all act on the same surface that the renderer samples.
class ShallowWater {
public:
    void configure(const ShallowWaterSettings& settings);
    const ShallowWaterSettings& settings() const { return m_settings; }

    void reset();
    void step(f32 dt);

    /// Adds `volume` cubic metres around a world-space point, with a Gaussian falloff.
    void addDrop(f32 worldX, f32 worldZ, f32 volume, f32 radius);
    void addImpulse(f32 worldX, f32 worldZ, f32 velocityX, f32 velocityZ, f32 radius);

    void setWind(f32 speed, f32 directionDeg);

    /// Raises the terrain bed under the water. Bed height is relative to y = 0.
    void setBed(f32 worldX, f32 worldZ, f32 height);
    void setFlatBed(f32 height = 0.0f);

    int resolution() const { return m_resolution; }
    f32 dx() const { return m_dx; }
    f32 dz() const { return m_dz; }

    f32 depth(int i, int j) const;
    /// Terrain height plus water depth at the cell centre.
    f32 surface(int i, int j) const;
    f32 bed(int i, int j) const;

    /// Bilinear water surface height at a world-space position.
    f32 heightAtWorld(f32 x, f32 z) const;
    Vec3 normalAtWorld(f32 x, f32 z) const;
    f32 depthAtWorld(f32 x, f32 z) const;

    f32 totalVolume() const;
    f32 maxDepth() const;
    f32 totalKineticEnergy() const;
    f32 windSpeed() const { return m_settings.windSpeed; }

private:
    int index(int i, int j) const { return j * m_resolution + i; }
    int uIndex(int i, int j) const { return j * (m_resolution + 1) + i; }
    int vIndex(int i, int j) const { return j * m_resolution + i; }

    void substep(f32 dt);
    void enforceCfl(f32 dt);

    ShallowWaterSettings m_settings;
    int m_resolution = 64;
    f32 m_dx = 1.0f;
    f32 m_dz = 1.0f;

    std::vector<f32> m_depth;
    std::vector<f32> m_bed;
    /// x-faces: (resolution + 1) * resolution.
    std::vector<f32> m_u;
    /// z-faces: resolution * (resolution + 1).
    std::vector<f32> m_v;
};

}  // namespace Caffeine::Fluids
