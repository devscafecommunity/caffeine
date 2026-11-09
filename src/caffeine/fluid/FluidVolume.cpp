#include "FluidVolume.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Fluids {
namespace {

f32 gaussianWeight(f32 dx, f32 dy, f32 dz, f32 sigma) {
    const f32 inv = 1.0f / (2.0f * sigma * sigma);
    return std::exp(-(dx * dx + dy * dy + dz * dz) * inv);
}

}  // namespace

void FluidVolume::configure(const FluidVolumeSettings& settings) {
    m_settings = settings;
    m_nx = std::max(4, settings.resolutionX);
    m_ny = std::max(4, settings.resolutionY);
    m_nz = std::max(4, settings.resolutionZ);
    m_dx = std::min({settings.sizeX / static_cast<f32>(m_nx),
                     settings.sizeY / static_cast<f32>(m_ny),
                     settings.sizeZ / static_cast<f32>(m_nz)});
    const size_t cells =
        static_cast<size_t>(m_nx) * static_cast<size_t>(m_ny) * static_cast<size_t>(m_nz);
    m_u.assign(cells, 0.0f);
    m_v.assign(cells, 0.0f);
    m_w.assign(cells, 0.0f);
    m_density.assign(cells, 0.0f);
    m_temperature.assign(cells, 0.0f);
    m_pressure.assign(cells, 0.0f);
    m_divergence.assign(cells, 0.0f);
    m_scratch.assign(cells, 0.0f);
}

void FluidVolume::reset() {
    std::fill(m_u.begin(), m_u.end(), 0.0f);
    std::fill(m_v.begin(), m_v.end(), 0.0f);
    std::fill(m_w.begin(), m_w.end(), 0.0f);
    std::fill(m_density.begin(), m_density.end(), 0.0f);
    std::fill(m_temperature.begin(), m_temperature.end(), 0.0f);
    std::fill(m_pressure.begin(), m_pressure.end(), 0.0f);
    std::fill(m_divergence.begin(), m_divergence.end(), 0.0f);
}

void FluidVolume::setMaterial(FluidKind kind) {
    setMaterial(fluidMaterial(kind));
}

void FluidVolume::setMaterial(const FluidMaterial& material) {
    m_material = material;
}

void FluidVolume::addDensity(f32 x, f32 y, f32 z, f32 amount, f32 radius) {
    if (m_density.empty() || amount == 0.0f) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    f32 peak = 0.0f;
    m_scratch.assign(m_density.size(), 0.0f);
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
                const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dx;
                const f32 cz = (static_cast<f32>(k) + 0.5f) * m_dx;
                const f32 w = gaussianWeight(cx - x, cy - y, cz - z, sigma);
                m_scratch[index(i, j, k)] = w;
                peak = std::max(peak, w);
            }
        }
    }
    if (peak <= 0.0f) return;
    // `amount` is the peak density of the source.
    for (size_t cell = 0; cell < m_density.size(); ++cell) {
        m_density[cell] = std::clamp(m_density[cell] + amount * m_scratch[cell] / peak, 0.0f, 1.0f);
    }
}

void FluidVolume::addTemperature(f32 x, f32 y, f32 z, f32 amount, f32 radius) {
    if (m_temperature.empty() || amount == 0.0f) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    f32 peak = 0.0f;
    m_scratch.assign(m_temperature.size(), 0.0f);
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
                const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dx;
                const f32 cz = (static_cast<f32>(k) + 0.5f) * m_dx;
                const f32 w = gaussianWeight(cx - x, cy - y, cz - z, sigma);
                m_scratch[index(i, j, k)] = w;
                peak = std::max(peak, w);
            }
        }
    }
    if (peak <= 0.0f) return;
    // `amount` is the peak temperature of the source.
    for (size_t cell = 0; cell < m_temperature.size(); ++cell) {
        m_temperature[cell] =
            std::clamp(m_temperature[cell] + amount * m_scratch[cell] / peak, 0.0f, 4.0f);
    }
}

void FluidVolume::addVelocity(f32 x, f32 y, f32 z, f32 velocityX, f32 velocityY,
                              f32 velocityZ, f32 radius) {
    if (m_u.empty()) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const f32 cx = (static_cast<f32>(i) + 0.5f) * m_dx;
                const f32 cy = (static_cast<f32>(j) + 0.5f) * m_dx;
                const f32 cz = (static_cast<f32>(k) + 0.5f) * m_dx;
                const f32 w = gaussianWeight(cx - x, cy - y, cz - z, sigma);
                if (w <= 0.0f) continue;
                const int cell = index(i, j, k);
                m_u[cell] += velocityX * w;
                m_v[cell] += velocityY * w;
                m_w[cell] += velocityZ * w;
            }
        }
    }
}

void FluidVolume::step(f32 dt) {
    if (m_u.empty() || dt <= 0.0f) return;
    dt = std::min(dt, 0.05f);

    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const int cell = index(i, j, k);
                const f32 buoyancy =
                    m_material.temperatureBuoyancy * m_temperature[cell] -
                    m_material.densityWeight * m_density[cell];
                m_u[cell] += dt * m_wind.x;
                m_v[cell] += dt * m_wind.y;
                m_w[cell] += dt * (m_wind.z + buoyancy);
            }
        }
    }

    // Freeze the velocity field while advecting it (avoid self-aliasing).
    const std::vector<f32> velocityX = m_u;
    const std::vector<f32> velocityY = m_v;
    const std::vector<f32> velocityZ = m_w;
    advect(m_u, velocityX, velocityY, velocityZ, dt);
    advect(m_v, velocityX, velocityY, velocityZ, dt);
    advect(m_w, velocityX, velocityY, velocityZ, dt);
    project();

    advect(m_density, m_u, m_v, m_w, dt);
    advect(m_temperature, m_u, m_v, m_w, dt);

    const f32 densityFactor = 1.0f / (1.0f + dt * m_material.densityDissipation);
    const f32 temperatureFactor = 1.0f / (1.0f + dt * m_material.temperatureDissipation);
    for (size_t cell = 0; cell < m_density.size(); ++cell) {
        m_density[cell] = std::clamp(m_density[cell] * densityFactor, 0.0f, 1.0f);
        m_temperature[cell] = std::max(m_temperature[cell] * temperatureFactor, 0.0f);
    }

    // Open boundaries (zero-gradient velocity) so a forced plume can exhaust.
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            m_u[index(0, j, k)] = m_u[index(1, j, k)];
            m_u[index(m_nx - 1, j, k)] = m_u[index(m_nx - 2, j, k)];
        }
    }
    for (int k = 0; k < m_nz; ++k) {
        for (int i = 0; i < m_nx; ++i) {
            m_v[index(i, 0, k)] = m_v[index(i, 1, k)];
            m_v[index(i, m_ny - 1, k)] = m_v[index(i, m_ny - 2, k)];
        }
    }
    for (int j = 0; j < m_ny; ++j) {
        for (int i = 0; i < m_nx; ++i) {
            m_w[index(i, j, 0)] = m_w[index(i, j, 1)];
            m_w[index(i, j, m_nz - 1)] = m_w[index(i, j, m_nz - 2)];
        }
    }
}

f32 FluidVolume::sample(const std::vector<f32>& field, f32 x, f32 y, f32 z) const {
    const f32 gx = std::clamp(x / m_dx - 0.5f, 0.0f, static_cast<f32>(m_nx - 1));
    const f32 gy = std::clamp(y / m_dx - 0.5f, 0.0f, static_cast<f32>(m_ny - 1));
    const f32 gz = std::clamp(z / m_dx - 0.5f, 0.0f, static_cast<f32>(m_nz - 1));
    const int i0 = static_cast<int>(gx);
    const int j0 = static_cast<int>(gy);
    const int k0 = static_cast<int>(gz);
    const int i1 = std::min(i0 + 1, m_nx - 1);
    const int j1 = std::min(j0 + 1, m_ny - 1);
    const int k1 = std::min(k0 + 1, m_nz - 1);
    const f32 tx = gx - static_cast<f32>(i0);
    const f32 ty = gy - static_cast<f32>(j0);
    const f32 tz = gz - static_cast<f32>(k0);

    auto lerp = [](f32 a, f32 b, f32 t) { return a + (b - a) * t; };
    const f32 c00 = lerp(field[index(i0, j0, k0)], field[index(i1, j0, k0)], tx);
    const f32 c10 = lerp(field[index(i0, j1, k0)], field[index(i1, j1, k0)], tx);
    const f32 c01 = lerp(field[index(i0, j0, k1)], field[index(i1, j0, k1)], tx);
    const f32 c11 = lerp(field[index(i0, j1, k1)], field[index(i1, j1, k1)], tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

void FluidVolume::advect(std::vector<f32>& field, const std::vector<f32>& vx,
                         const std::vector<f32>& vy, const std::vector<f32>& vz, f32 dt) {
    m_scratch = field;
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const int cell = index(i, j, k);
                const f32 x = (static_cast<f32>(i) + 0.5f) * m_dx;
                const f32 y = (static_cast<f32>(j) + 0.5f) * m_dx;
                const f32 z = (static_cast<f32>(k) + 0.5f) * m_dx;
                field[cell] = sample(m_scratch, x - dt * vx[cell], y - dt * vy[cell],
                                     z - dt * vz[cell]);
            }
        }
    }
}

void FluidVolume::project() {
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const int iL = std::max(i - 1, 0);
                const int iR = std::min(i + 1, m_nx - 1);
                const int jB = std::max(j - 1, 0);
                const int jT = std::min(j + 1, m_ny - 1);
                const int kB = std::max(k - 1, 0);
                const int kF = std::min(k + 1, m_nz - 1);
                const f32 du = (m_u[index(iR, j, k)] - m_u[index(iL, j, k)]) /
                               (m_dx * static_cast<f32>(iR - iL));
                const f32 dv = (m_v[index(i, jT, k)] - m_v[index(i, jB, k)]) /
                               (m_dx * static_cast<f32>(jT - jB));
                const f32 dw = (m_w[index(i, j, kF)] - m_w[index(i, j, kB)]) /
                               (m_dx * static_cast<f32>(kF - kB));
                m_divergence[index(i, j, k)] = -0.5f * (du + dv + dw);
                m_pressure[index(i, j, k)] = 0.0f;
            }
        }
    }

    // Same h^2 scaling as the 2D solver (see EulerFluid.cpp).
    const f32 h2 = m_dx * m_dx;
    for (int iteration = 0; iteration < m_settings.pressureIterations; ++iteration) {
        for (int k = 0; k < m_nz; ++k) {
            for (int j = 0; j < m_ny; ++j) {
                for (int i = 0; i < m_nx; ++i) {
                    const f32 pL = m_pressure[index(std::max(i - 1, 0), j, k)];
                    const f32 pR = m_pressure[index(std::min(i + 1, m_nx - 1), j, k)];
                    const f32 pB = m_pressure[index(i, std::max(j - 1, 0), k)];
                    const f32 pT = m_pressure[index(i, std::min(j + 1, m_ny - 1), k)];
                    const f32 pD = m_pressure[index(i, j, std::max(k - 1, 0))];
                    const f32 pU = m_pressure[index(i, j, std::min(k + 1, m_nz - 1))];
                    m_pressure[index(i, j, k)] =
                        (m_divergence[index(i, j, k)] * h2 + pL + pR + pB + pT + pD + pU) /
                        6.0f;
                }
            }
        }
    }

    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const int cell = index(i, j, k);
                const int iL = std::max(i - 1, 0);
                const int iR = std::min(i + 1, m_nx - 1);
                const int jB = std::max(j - 1, 0);
                const int jT = std::min(j + 1, m_ny - 1);
                const int kB = std::max(k - 1, 0);
                const int kF = std::min(k + 1, m_nz - 1);
                const f32 dpdx =
                    (m_pressure[index(iR, j, k)] - m_pressure[index(iL, j, k)]) /
                    (m_dx * static_cast<f32>(iR - iL));
                const f32 dpdy =
                    (m_pressure[index(i, jT, k)] - m_pressure[index(i, jB, k)]) /
                    (m_dx * static_cast<f32>(jT - jB));
                const f32 dpdz =
                    (m_pressure[index(i, j, kF)] - m_pressure[index(i, j, kB)]) /
                    (m_dx * static_cast<f32>(kF - kB));
                m_u[cell] -= 0.5f * dpdx;
                m_v[cell] -= 0.5f * dpdy;
                m_w[cell] -= 0.5f * dpdz;
            }
        }
    }
}

f32 FluidVolume::densityAt(int i, int j, int k) const {
    if (i < 0 || j < 0 || k < 0 || i >= m_nx || j >= m_ny || k >= m_nz) return 0.0f;
    return m_density[index(i, j, k)];
}

f32 FluidVolume::temperatureAt(int i, int j, int k) const {
    if (i < 0 || j < 0 || k < 0 || i >= m_nx || j >= m_ny || k >= m_nz) return 0.0f;
    return m_temperature[index(i, j, k)];
}

f32 FluidVolume::velocityZAt(int i, int j, int k) const {
    if (i < 0 || j < 0 || k < 0 || i >= m_nx || j >= m_ny || k >= m_nz) return 0.0f;
    return m_w[index(i, j, k)];
}

f32 FluidVolume::totalDensity() const {
    f32 sum = 0.0f;
    for (f32 density : m_density) sum += density;
    return sum * m_dx * m_dx * m_dx;
}

Vec3 FluidVolume::densityCenterOfMass() const {
    f32 total = 0.0f;
    Vec3 center(0.0f, 0.0f, 0.0f);
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const f32 density = m_density[index(i, j, k)];
                total += density;
                center.x += density * (static_cast<f32>(i) + 0.5f) * m_dx;
                center.y += density * (static_cast<f32>(j) + 0.5f) * m_dx;
                center.z += density * (static_cast<f32>(k) + 0.5f) * m_dx;
            }
        }
    }
    if (total <= 1.0e-8f) return Vec3(0.0f, 0.0f, 0.0f);
    return center / total;
}

Vec3 FluidVolume::temperatureCenterOfMass() const {
    f32 total = 0.0f;
    Vec3 center(0.0f, 0.0f, 0.0f);
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const f32 temperature = m_temperature[index(i, j, k)];
                total += temperature;
                center.x += temperature * (static_cast<f32>(i) + 0.5f) * m_dx;
                center.y += temperature * (static_cast<f32>(j) + 0.5f) * m_dx;
                center.z += temperature * (static_cast<f32>(k) + 0.5f) * m_dx;
            }
        }
    }
    if (total <= 1.0e-8f) return Vec3(0.0f, 0.0f, 0.0f);
    return center / total;
}

f32 FluidVolume::divergenceRms() const {
    f32 sum = 0.0f;
    for (int k = 0; k < m_nz; ++k) {
        for (int j = 0; j < m_ny; ++j) {
            for (int i = 0; i < m_nx; ++i) {
                const int iL = std::max(i - 1, 0);
                const int iR = std::min(i + 1, m_nx - 1);
                const int jB = std::max(j - 1, 0);
                const int jT = std::min(j + 1, m_ny - 1);
                const int kB = std::max(k - 1, 0);
                const int kF = std::min(k + 1, m_nz - 1);
                const f32 du = (m_u[index(iR, j, k)] - m_u[index(iL, j, k)]) /
                               (m_dx * static_cast<f32>(iR - iL));
                const f32 dv = (m_v[index(i, jT, k)] - m_v[index(i, jB, k)]) /
                               (m_dx * static_cast<f32>(jT - jB));
                const f32 dw = (m_w[index(i, j, kF)] - m_w[index(i, j, kB)]) /
                               (m_dx * static_cast<f32>(kF - kB));
                const f32 divergence = 0.5f * (du + dv + dw);
                sum += divergence * divergence;
            }
        }
    }
    return std::sqrt(sum / static_cast<f32>(m_nx * m_ny * m_nz));
}

}  // namespace Caffeine::Fluids
