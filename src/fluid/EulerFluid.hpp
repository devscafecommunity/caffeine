#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::Fluids {

enum class FluidKind : int {
    Water = 0,
    Fire = 1,
    Fog = 2,
    Smoke = 3,
};

/// Physical + shading parameters for one fluid. The solver is the same for all
/// four; only buoyancy, dissipation, vorticity and emission change.
struct FluidMaterial {
    FluidKind kind = FluidKind::Water;
    Vec3 color{0.06f, 0.22f, 0.42f};
    /// Upward acceleration per unit temperature.
    f32 temperatureBuoyancy = 0.0f;
    /// Downward acceleration per unit density.
    f32 densityWeight = 1.2f;
    /// Density decay (1/s).
    f32 densityDissipation = 0.15f;
    /// Temperature decay (1/s).
    f32 temperatureDissipation = 0.40f;
    f32 viscosity = 0.02f;
    /// Vorticity confinement strength.
    f32 vorticity = 0.0f;
    /// Emissive multiplier used by the renderer.
    f32 emission = 0.0f;
    bool emissive = false;
};

FluidMaterial fluidMaterial(FluidKind kind);

struct Euler2DSettings {
    int resolution = 96;
    f32 width = 32.0f;
    f32 height = 32.0f;
    int pressureIterations = 24;
    u32 seed = 1u;
};

/// Stable-Fluids solver (semi-Lagrangian advection + Jacobi pressure projection).
/// Density and temperature are transported by the incompressible velocity field.
/// This is a real fluid simulation: it is not a particle effect or a scrolling
/// noise texture. Water, fire, fog and smoke share the solver and differ by material.
class EulerFluid2D {
public:
    void configure(const Euler2DSettings& settings);
    const Euler2DSettings& settings() const { return m_settings; }

    void reset();
    void setMaterial(FluidKind kind);
    void setMaterial(const FluidMaterial& material);
    FluidKind materialKind() const { return m_material.kind; }
    const FluidMaterial& material() const { return m_material; }

    void step(f32 dt);

    void setWind(Vec2 wind) { m_wind = wind; }
    Vec2 wind() const { return m_wind; }

    /// Adds `amount` density around a world-space point (Gaussian falloff).
    void addDensity(f32 x, f32 y, f32 amount, f32 radius);
    /// Adds temperature around a world-space point.
    void addTemperature(f32 x, f32 y, f32 amount, f32 radius);
    /// Adds a velocity impulse around a world-space point.
    void addVelocity(f32 x, f32 y, f32 velocityX, f32 velocityY, f32 radius);

    int resolution() const { return m_resolution; }
    f32 cellWidth() const { return m_dx; }
    f32 cellHeight() const { return m_dy; }

    f32 densityAt(int i, int j) const;
    f32 temperatureAt(int i, int j) const;
    f32 velocityXAt(int i, int j) const;
    f32 velocityYAt(int i, int j) const;

    const std::vector<f32>& densityField() const { return m_density; }
    const std::vector<f32>& temperatureField() const { return m_temperature; }

    f32 totalDensity() const;
    f32 totalTemperature() const;
    Vec2 densityCenterOfMass() const;
    Vec2 temperatureCenterOfMass() const;
    f32 divergenceRms() const;
    f32 maxSpeed() const;

private:
    int index(int i, int j) const { return j * m_resolution + i; }
    f32 sample(const std::vector<f32>& field, f32 x, f32 y) const;
    void advect(std::vector<f32>& field, const std::vector<f32>& velocityX,
                const std::vector<f32>& velocityY, f32 dt);
    void project();
    void applyVorticity(f32 dt);

    Euler2DSettings m_settings;
    FluidMaterial m_material;
    int m_resolution = 96;
    f32 m_dx = 1.0f;
    f32 m_dy = 1.0f;
    Vec2 m_wind{0.0f, 0.0f};

    std::vector<f32> m_u;
    std::vector<f32> m_v;
    std::vector<f32> m_density;
    std::vector<f32> m_temperature;
    std::vector<f32> m_pressure;
    std::vector<f32> m_divergence;
    std::vector<f32> m_scratchU;
    std::vector<f32> m_scratchV;
    std::vector<f32> m_scratch;
};

}  // namespace Caffeine::Fluids
