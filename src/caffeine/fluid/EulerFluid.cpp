#include "EulerFluid.hpp"

#include "math/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Fluids {
namespace {

f32 gaussianWeight(f32 dx, f32 dy, f32 sigma) {
    const f32 inv = 1.0f / (2.0f * sigma * sigma);
    return std::exp(-(dx * dx + dy * dy) * inv);
}

}  // namespace

FluidMaterial fluidMaterial(FluidKind kind) {
    FluidMaterial material;
    material.kind = kind;
    switch (kind) {
        case FluidKind::Water:
            material.color = Vec3(0.05f, 0.24f, 0.46f);
            material.temperatureBuoyancy = 0.0f;
            material.densityWeight = 1.4f;
            material.densityDissipation = 0.10f;
            material.temperatureDissipation = 0.50f;
            material.viscosity = 0.015f;
            material.vorticity = 0.0f;
            material.emission = 0.0f;
            material.emissive = false;
            break;
        case FluidKind::Fire:
            material.color = Vec3(1.0f, 0.36f, 0.05f);
            material.temperatureBuoyancy = 1.8f;
            material.densityWeight = 0.0f;
            material.densityDissipation = 0.80f;
            material.temperatureDissipation = 1.10f;
            material.viscosity = 0.008f;
            material.vorticity = 0.35f;
            material.emission = 3.8f;
            material.emissive = true;
            break;
        case FluidKind::Fog:
            material.color = Vec3(0.74f, 0.78f, 0.83f);
            material.temperatureBuoyancy = 0.0f;
            material.densityWeight = -0.015f;
            material.densityDissipation = 0.035f;
            material.temperatureDissipation = 0.10f;
            material.viscosity = 0.02f;
            material.vorticity = 0.0f;
            material.emission = 0.0f;
            material.emissive = false;
            break;
        case FluidKind::Smoke:
            material.color = Vec3(0.24f, 0.24f, 0.27f);
            material.temperatureBuoyancy = 1.0f;
            material.densityWeight = 0.05f;
            material.densityDissipation = 0.25f;
            material.temperatureDissipation = 0.55f;
            material.viscosity = 0.012f;
            material.vorticity = 0.22f;
            material.emission = 0.0f;
            material.emissive = false;
            break;
    }
    return material;
}

void EulerFluid2D::configure(const Euler2DSettings& settings) {
    m_settings = settings;
    m_resolution = std::max(8, settings.resolution);
    m_dx = settings.width / static_cast<f32>(m_resolution);
    m_dy = settings.height / static_cast<f32>(m_resolution);
    const size_t cells = static_cast<size_t>(m_resolution) * m_resolution;
    m_u.assign(cells, 0.0f);
    m_v.assign(cells, 0.0f);
    m_density.assign(cells, 0.0f);
    m_temperature.assign(cells, 0.0f);
    m_pressure.assign(cells, 0.0f);
    m_divergence.assign(cells, 0.0f);
    m_scratchU.assign(cells, 0.0f);
    m_scratchV.assign(cells, 0.0f);
    m_scratch.assign(cells, 0.0f);
}

void EulerFluid2D::reset() {
    std::fill(m_u.begin(), m_u.end(), 0.0f);
    std::fill(m_v.begin(), m_v.end(), 0.0f);
    std::fill(m_density.begin(), m_density.end(), 0.0f);
    std::fill(m_temperature.begin(), m_temperature.end(), 0.0f);
    std::fill(m_pressure.begin(), m_pressure.end(), 0.0f);
    std::fill(m_divergence.begin(), m_divergence.end(), 0.0f);
}

void EulerFluid2D::setMaterial(FluidKind kind) {
    setMaterial(fluidMaterial(kind));
}

void EulerFluid2D::setMaterial(const FluidMaterial& material) {
    m_material = material;
}

void EulerFluid2D::addDensity(f32 x, f32 y, f32 amount, f32 radius) {
    if (m_density.empty() || amount == 0.0f) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    std::vector<f32> weights(m_density.size(), 0.0f);
    f32 peak = 0.0f;
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dy;
            const f32 w = gaussianWeight(cx - x, cy - y, sigma);
            weights[index(i, j)] = w;
            peak = std::max(peak, w);
        }
    }
    if (peak <= 0.0f) return;
    // `amount` is the peak density of the source, not a total spread over cells.
    for (size_t k = 0; k < m_density.size(); ++k) {
        m_density[k] = std::clamp(m_density[k] + amount * weights[k] / peak, 0.0f, 1.0f);
    }
}

void EulerFluid2D::addTemperature(f32 x, f32 y, f32 amount, f32 radius) {
    if (m_temperature.empty() || amount == 0.0f) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    std::vector<f32> weights(m_temperature.size(), 0.0f);
    f32 peak = 0.0f;
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dy;
            const f32 w = gaussianWeight(cx - x, cy - y, sigma);
            weights[index(i, j)] = w;
            peak = std::max(peak, w);
        }
    }
    if (peak <= 0.0f) return;
    // `amount` is the peak temperature of the source.
    for (size_t k = 0; k < m_temperature.size(); ++k) {
        m_temperature[k] = std::clamp(m_temperature[k] + amount * weights[k] / peak, 0.0f, 4.0f);
    }
}

void EulerFluid2D::addVelocity(f32 x, f32 y, f32 velocityX, f32 velocityY, f32 radius) {
    if (m_u.empty()) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dy;
            const f32 w = gaussianWeight(cx - x, cy - y, sigma);
            if (w <= 0.0f) continue;
            m_u[index(i, j)] += velocityX * w;
            m_v[index(i, j)] += velocityY * w;
        }
    }
}

void EulerFluid2D::step(f32 dt) {
    if (m_u.empty() || dt <= 0.0f) return;
    dt = std::min(dt, 0.05f);

    const int n = m_resolution;

    // External forces: wind, buoyancy and gravity on dense fluid.
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int k = index(i, j);
            const f32 buoyancy =
                m_material.temperatureBuoyancy * m_temperature[k] -
                m_material.densityWeight * m_density[k];
            m_v[k] += dt * buoyancy;
            m_u[k] += dt * m_wind.x;
            m_v[k] += dt * m_wind.y;
        }
    }

    // Advect with a frozen copy of the velocity field: field and velocity alias
    // the same arrays otherwise.
    m_scratchU = m_u;
    m_scratchV = m_v;
    advect(m_u, m_scratchU, m_scratchV, dt);
    advect(m_v, m_scratchU, m_scratchV, dt);

    if (m_material.viscosity > 0.0f) {
        // A couple of Jacobi iterations are enough for the visual viscosity range.
        const f32 alpha = m_material.viscosity * dt / std::max(m_dx * m_dy, 1.0e-6f);
        for (int iteration = 0; iteration < 2; ++iteration) {
            m_scratchU = m_u;
            m_scratchV = m_v;
            for (int j = 1; j + 1 < n; ++j) {
                for (int i = 1; i + 1 < n; ++i) {
                    const int k = index(i, j);
                    m_u[k] = (m_scratchU[k] +
                              alpha * (m_scratchU[k - 1] + m_scratchU[k + 1] +
                                       m_scratchU[k - n] + m_scratchU[k + n])) /
                             (1.0f + 4.0f * alpha);
                    m_v[k] = (m_scratchV[k] +
                              alpha * (m_scratchV[k - 1] + m_scratchV[k + 1] +
                                       m_scratchV[k - n] + m_scratchV[k + n])) /
                             (1.0f + 4.0f * alpha);
                }
            }
        }
    }

    applyVorticity(dt);
    project();

    advect(m_density, m_u, m_v, dt);
    advect(m_temperature, m_u, m_v, dt);

    const f32 densityFactor = 1.0f / (1.0f + dt * m_material.densityDissipation);
    const f32 temperatureFactor = 1.0f / (1.0f + dt * m_material.temperatureDissipation);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int k = index(i, j);
            m_density[k] = std::clamp(m_density[k] * densityFactor, 0.0f, 1.0f);
            m_temperature[k] = std::max(m_temperature[k] * temperatureFactor, 0.0f);
        }
    }

    // Open boundaries: copy the neighbouring velocity (zero-gradient). This
    // keeps a uniform wind field divergence-free instead of slowing it down
    // against no-slip walls; the flow is allowed to leave the slice.
    for (int j = 0; j < n; ++j) {
        m_u[index(0, j)] = m_u[index(1, j)];
        m_u[index(n - 1, j)] = m_u[index(n - 2, j)];
    }
    for (int i = 0; i < n; ++i) {
        m_v[index(i, 0)] = m_v[index(i, 1)];
        m_v[index(i, n - 1)] = m_v[index(i, n - 2)];
    }
}

f32 EulerFluid2D::sample(const std::vector<f32>& field, f32 x, f32 y) const {
    const int n = m_resolution;
    const f32 gx = std::clamp(x / m_dx - 0.5f, 0.0f, static_cast<f32>(n - 1));
    const f32 gy = std::clamp(y / m_dy - 0.5f, 0.0f, static_cast<f32>(n - 1));
    const int i0 = static_cast<int>(gx);
    const int j0 = static_cast<int>(gy);
    const int i1 = std::min(i0 + 1, n - 1);
    const int j1 = std::min(j0 + 1, n - 1);
    const f32 tx = gx - static_cast<f32>(i0);
    const f32 ty = gy - static_cast<f32>(j0);
    const f32 a = field[index(i0, j0)] * (1.0f - tx) + field[index(i1, j0)] * tx;
    const f32 b = field[index(i0, j1)] * (1.0f - tx) + field[index(i1, j1)] * tx;
    return a * (1.0f - ty) + b * ty;
}

void EulerFluid2D::advect(std::vector<f32>& field, const std::vector<f32>& velocityX,
                          const std::vector<f32>& velocityY, f32 dt) {
    const int n = m_resolution;
    m_scratch = field;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int k = index(i, j);
            const f32 x = (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 y = (static_cast<f32>(j) + 0.5f) * m_dy;
            const f32 backX = x - dt * velocityX[k];
            const f32 backY = y - dt * velocityY[k];
            field[k] = sample(m_scratch, backX, backY);
        }
    }
}

void EulerFluid2D::project() {
    const int n = m_resolution;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int k = index(i, j);
            const int iL = std::max(i - 1, 0);
            const int iR = std::min(i + 1, n - 1);
            const int jB = std::max(j - 1, 0);
            const int jT = std::min(j + 1, n - 1);
            const f32 du = (m_u[index(iR, j)] - m_u[index(iL, j)]) /
                           (m_dx * static_cast<f32>(iR - iL));
            const f32 dv = (m_v[index(i, jT)] - m_v[index(i, jB)]) /
                           (m_dy * static_cast<f32>(jT - jB));
            m_divergence[k] = -0.5f * (du + dv);
            m_pressure[k] = 0.0f;
        }
    }

    // The Poisson RHS must be scaled by h^2: with the discrete Laplacian
    // (neighbours - 4p) / h^2 = div, the Jacobi update is
    // p = (neighbours + h^2 * div) / 4. Without this factor the pressure (and
    // the velocity correction) grows as 1/h^2 and high-resolution grids explode.
    const f32 h2 = m_dx * m_dy;
    for (int iteration = 0; iteration < m_settings.pressureIterations; ++iteration) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const int k = index(i, j);
                const f32 pL = m_pressure[index(std::max(i - 1, 0), j)];
                const f32 pR = m_pressure[index(std::min(i + 1, n - 1), j)];
                const f32 pB = m_pressure[index(i, std::max(j - 1, 0))];
                const f32 pT = m_pressure[index(i, std::min(j + 1, n - 1))];
                m_pressure[k] = (m_divergence[k] * h2 + pL + pR + pB + pT) * 0.25f;
            }
        }
    }

    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int k = index(i, j);
            const int iL = std::max(i - 1, 0);
            const int iR = std::min(i + 1, n - 1);
            const int jB = std::max(j - 1, 0);
            const int jT = std::min(j + 1, n - 1);
            const f32 dpdx = (m_pressure[index(iR, j)] - m_pressure[index(iL, j)]) /
                             (m_dx * static_cast<f32>(iR - iL));
            const f32 dpdy = (m_pressure[index(i, jT)] - m_pressure[index(i, jB)]) /
                             (m_dy * static_cast<f32>(jT - jB));
            m_u[k] -= 0.5f * dpdx;
            m_v[k] -= 0.5f * dpdy;
        }
    }
}

void EulerFluid2D::applyVorticity(f32 dt) {
    if (m_material.vorticity <= 0.0f) return;
    const int n = m_resolution;
    std::vector<f32> curl(static_cast<size_t>(n) * n, 0.0f);
    for (int j = 1; j + 1 < n; ++j) {
        for (int i = 1; i + 1 < n; ++i) {
            const int k = index(i, j);
            const f32 dvdx = (m_v[k + 1] - m_v[k - 1]) / (2.0f * m_dx);
            const f32 dudy = (m_u[k + n] - m_u[k - n]) / (2.0f * m_dy);
            curl[k] = dvdx - dudy;
        }
    }
    for (int j = 1; j + 1 < n; ++j) {
        for (int i = 1; i + 1 < n; ++i) {
            const int k = index(i, j);
            const f32 nx = (std::abs(curl[k + 1]) - std::abs(curl[k - 1])) * 0.5f;
            const f32 ny = (std::abs(curl[k + n]) - std::abs(curl[k - n])) * 0.5f;
            const f32 length = std::sqrt(nx * nx + ny * ny);
            if (length < 1.0e-5f) continue;
            const f32 scale = m_material.vorticity * dt / length;
            m_u[k] += scale * ny * curl[k];
            m_v[k] -= scale * nx * curl[k];
        }
    }
}

f32 EulerFluid2D::densityAt(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_density[index(i, j)];
}

f32 EulerFluid2D::temperatureAt(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_temperature[index(i, j)];
}

f32 EulerFluid2D::velocityXAt(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_u[index(i, j)];
}

f32 EulerFluid2D::velocityYAt(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_v[index(i, j)];
}

f32 EulerFluid2D::totalDensity() const {
    f32 sum = 0.0f;
    for (f32 density : m_density) sum += density;
    return sum * m_dx * m_dy;
}

f32 EulerFluid2D::totalTemperature() const {
    f32 sum = 0.0f;
    for (f32 temperature : m_temperature) sum += temperature;
    return sum * m_dx * m_dy;
}

Vec2 EulerFluid2D::densityCenterOfMass() const {
    f32 total = 0.0f;
    f32 cx = 0.0f;
    f32 cy = 0.0f;
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 density = m_density[index(i, j)];
            total += density;
            cx += density * (static_cast<f32>(i) + 0.5f) * m_dx;
            cy += density * (static_cast<f32>(j) + 0.5f) * m_dy;
        }
    }
    if (total <= 1.0e-8f) return Vec2(0.0f, 0.0f);
    return Vec2(cx / total, cy / total);
}

Vec2 EulerFluid2D::temperatureCenterOfMass() const {
    f32 total = 0.0f;
    f32 cx = 0.0f;
    f32 cy = 0.0f;
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 temperature = m_temperature[index(i, j)];
            total += temperature;
            cx += temperature * (static_cast<f32>(i) + 0.5f) * m_dx;
            cy += temperature * (static_cast<f32>(j) + 0.5f) * m_dy;
        }
    }
    if (total <= 1.0e-8f) return Vec2(0.0f, 0.0f);
    return Vec2(cx / total, cy / total);
}

f32 EulerFluid2D::divergenceRms() const {
    const int n = m_resolution;
    f32 sum = 0.0f;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int iL = std::max(i - 1, 0);
            const int iR = std::min(i + 1, n - 1);
            const int jB = std::max(j - 1, 0);
            const int jT = std::min(j + 1, n - 1);
            const f32 du = (m_u[index(iR, j)] - m_u[index(iL, j)]) /
                           (m_dx * static_cast<f32>(iR - iL));
            const f32 dv = (m_v[index(i, jT)] - m_v[index(i, jB)]) /
                           (m_dy * static_cast<f32>(jT - jB));
            const f32 divergence = 0.5f * (du + dv);
            sum += divergence * divergence;
        }
    }
    return std::sqrt(sum / static_cast<f32>(n * n));
}

f32 EulerFluid2D::maxSpeed() const {
    f32 maximum = 0.0f;
    for (size_t k = 0; k < m_u.size(); ++k) {
        maximum = std::max(maximum, std::sqrt(m_u[k] * m_u[k] + m_v[k] * m_v[k]));
    }
    return maximum;
}

}  // namespace Caffeine::Fluids
