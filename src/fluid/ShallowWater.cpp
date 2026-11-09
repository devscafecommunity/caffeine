#include "ShallowWater.hpp"

#include "math/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Fluids {

void ShallowWater::configure(const ShallowWaterSettings& settings) {
    m_settings = settings;
    m_resolution = std::max(4, settings.resolution);
    m_dx = settings.sizeX / static_cast<f32>(m_resolution);
    m_dz = settings.sizeZ / static_cast<f32>(m_resolution);

    const size_t cells = static_cast<size_t>(m_resolution) * m_resolution;
    m_depth.assign(cells, settings.baseDepth);
    m_bed.assign(cells, 0.0f);
    m_u.assign(static_cast<size_t>(m_resolution + 1) * m_resolution, 0.0f);
    m_v.assign(static_cast<size_t>(m_resolution) * (m_resolution + 1), 0.0f);
}

void ShallowWater::reset() {
    std::fill(m_depth.begin(), m_depth.end(), m_settings.baseDepth);
    std::fill(m_u.begin(), m_u.end(), 0.0f);
    std::fill(m_v.begin(), m_v.end(), 0.0f);
}

void ShallowWater::setFlatBed(f32 height) {
    std::fill(m_bed.begin(), m_bed.end(), height);
}

void ShallowWater::setBed(f32 worldX, f32 worldZ, f32 height) {
    if (m_depth.empty()) return;
    const int i = static_cast<int>((worldX + m_settings.sizeX * 0.5f) / m_dx);
    const int j = static_cast<int>((worldZ + m_settings.sizeZ * 0.5f) / m_dz);
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return;
    m_bed[index(i, j)] = height;
}

void ShallowWater::addDrop(f32 worldX, f32 worldZ, f32 volume, f32 radius) {
    if (m_depth.empty() || volume <= 0.0f) return;
    radius = std::max(radius, m_dx);
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    const f32 invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

    std::vector<f32> weights;
    weights.reserve(m_depth.size());
    f32 weightSum = 0.0f;

    const f32 originX = -m_settings.sizeX * 0.5f;
    const f32 originZ = -m_settings.sizeZ * 0.5f;
    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 cx = originX + (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 cz = originZ + (static_cast<f32>(j) + 0.5f) * m_dz;
            const f32 dx = cx - worldX;
            const f32 dz = cz - worldZ;
            const f32 w = std::exp(-(dx * dx + dz * dz) * invTwoSigmaSq);
            weights.push_back(w);
            weightSum += w;
        }
    }
    if (weightSum <= 0.0f) return;

    const f32 cellArea = m_dx * m_dz;
    for (size_t k = 0; k < m_depth.size(); ++k) {
        m_depth[k] += volume * (weights[k] / weightSum) / cellArea;
    }
}

void ShallowWater::addImpulse(f32 worldX, f32 worldZ, f32 velocityX, f32 velocityZ,
                              f32 radius) {
    if (m_depth.empty() || radius <= 0.0f) return;
    const f32 sigma = std::max(radius * 0.5f, 1.0e-4f);
    const f32 invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);
    const f32 originX = -m_settings.sizeX * 0.5f;
    const f32 originZ = -m_settings.sizeZ * 0.5f;

    for (int j = 0; j < m_resolution; ++j) {
        for (int i = 0; i < m_resolution; ++i) {
            const f32 cx = originX + (static_cast<f32>(i) + 0.5f) * m_dx;
            const f32 cz = originZ + (static_cast<f32>(j) + 0.5f) * m_dz;
            const f32 dx = cx - worldX;
            const f32 dz = cz - worldZ;
            const f32 w = std::exp(-(dx * dx + dz * dz) * invTwoSigmaSq);
            if (w <= 0.0f) continue;
            m_u[uIndex(i, j)] += velocityX * w;
            m_u[uIndex(i + 1, j)] += velocityX * w;
            m_v[vIndex(i, j)] += velocityZ * w;
            m_v[vIndex(i, j + 1)] += velocityZ * w;
        }
    }
}

void ShallowWater::setWind(f32 speed, f32 directionDeg) {
    m_settings.windSpeed = std::max(speed, 0.0f);
    m_settings.windDirectionDeg = directionDeg;
}

void ShallowWater::step(f32 dt) {
    if (m_depth.empty() || dt <= 0.0f) return;

    const f32 c = std::sqrt(std::max(m_settings.gravity * (maxDepth() + 0.1f), 0.01f));
    const f32 dtMax = 0.35f * std::min(m_dx, m_dz) / std::max(c, 0.01f);
    int steps = static_cast<int>(std::ceil(dt / std::max(dtMax, 1.0e-5f)));
    steps = std::clamp(steps, 1, 64);
    const f32 sub = dt / static_cast<f32>(steps);
    for (int s = 0; s < steps; ++s) {
        substep(sub);
    }
}

void ShallowWater::substep(f32 dt) {
    const int n = m_resolution;
    const f32 g = m_settings.gravity;
    const f32 angle = Math::degToRad(m_settings.windDirectionDeg);
    const f32 wind = m_settings.windSpeed;
    const f32 accelX = m_settings.windStrength * wind * wind * std::cos(angle);
    const f32 accelZ = m_settings.windStrength * wind * wind * std::sin(angle);
    const f32 friction = 1.0f / (1.0f + dt * m_settings.friction);
    const f32 damping = std::clamp(1.0f - dt * m_settings.damping, 0.0f, 1.0f);

    // Momentum: surface gradient plus wind stress, divided by the local depth.
    for (int j = 0; j < n; ++j) {
        m_u[uIndex(0, j)] = 0.0f;
        m_u[uIndex(n, j)] = 0.0f;
        for (int i = 1; i < n; ++i) {
            const f32 hL = m_depth[index(i - 1, j)];
            const f32 hR = m_depth[index(i, j)];
            const f32 sL = m_bed[index(i - 1, j)] + hL;
            const f32 sR = m_bed[index(i, j)] + hR;
            const f32 depth = std::max(0.25f, 0.5f * (hL + hR));
            f32 vel = m_u[uIndex(i, j)];
            vel += -g * dt * (sR - sL) / m_dx;
            vel += dt * accelX / depth;
            vel *= friction;
            vel *= damping;
            m_u[uIndex(i, j)] = vel;
        }
    }
    for (int i = 0; i < n; ++i) {
        m_v[vIndex(i, 0)] = 0.0f;
        m_v[vIndex(i, n)] = 0.0f;
        for (int j = 1; j < n; ++j) {
            const f32 hB = m_depth[index(i, j - 1)];
            const f32 hT = m_depth[index(i, j)];
            const f32 sB = m_bed[index(i, j - 1)] + hB;
            const f32 sT = m_bed[index(i, j)] + hT;
            const f32 depth = std::max(0.25f, 0.5f * (hB + hT));
            f32 vel = m_v[vIndex(i, j)];
            vel += -g * dt * (sT - sB) / m_dz;
            vel += dt * accelZ / depth;
            vel *= friction;
            vel *= damping;
            m_v[vIndex(i, j)] = vel;
        }
    }

    // Continuity in flux form with upwind face depths.
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const f32 hL = (i > 0) ? m_depth[index(i - 1, j)] : m_depth[index(i, j)];
            const f32 hR = (i + 1 < n) ? m_depth[index(i + 1, j)] : m_depth[index(i, j)];
            const f32 hB = (j > 0) ? m_depth[index(i, j - 1)] : m_depth[index(i, j)];
            const f32 hT = (j + 1 < n) ? m_depth[index(i, j + 1)] : m_depth[index(i, j)];

            const f32 uL = m_u[uIndex(i, j)];
            const f32 uR = m_u[uIndex(i + 1, j)];
            const f32 vB = m_v[vIndex(i, j)];
            const f32 vT = m_v[vIndex(i, j + 1)];

            const f32 depthL = uL > 0.0f ? hL : m_depth[index(i, j)];
            const f32 depthR = uR > 0.0f ? m_depth[index(i, j)] : hR;
            const f32 depthB = vB > 0.0f ? hB : m_depth[index(i, j)];
            const f32 depthT = vT > 0.0f ? m_depth[index(i, j)] : hT;

            const f32 fluxL = uL * depthL;
            const f32 fluxR = uR * depthR;
            const f32 fluxB = vB * depthB;
            const f32 fluxT = vT * depthT;

            f32& h = m_depth[index(i, j)];
            h -= dt / m_dx * (fluxR - fluxL);
            h -= dt / m_dz * (fluxT - fluxB);
            if (h < 0.0f) h = 0.0f;
        }
    }
}

f32 ShallowWater::depth(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_depth[index(i, j)];
}

f32 ShallowWater::surface(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_bed[index(i, j)] + m_depth[index(i, j)];
}

f32 ShallowWater::bed(int i, int j) const {
    if (i < 0 || j < 0 || i >= m_resolution || j >= m_resolution) return 0.0f;
    return m_bed[index(i, j)];
}

f32 ShallowWater::depthAtWorld(f32 x, f32 z) const {
    const f32 gx = (x + m_settings.sizeX * 0.5f) / m_dx - 0.5f;
    const f32 gz = (z + m_settings.sizeZ * 0.5f) / m_dz - 0.5f;
    const int i0 = static_cast<int>(std::floor(gx));
    const int j0 = static_cast<int>(std::floor(gz));
    const f32 tx = gx - static_cast<f32>(i0);
    const f32 tz = gz - static_cast<f32>(j0);
    const int i1 = std::clamp(i0 + 1, 0, m_resolution - 1);
    const int j1 = std::clamp(j0 + 1, 0, m_resolution - 1);
    const int ic = std::clamp(i0, 0, m_resolution - 1);
    const int jc = std::clamp(j0, 0, m_resolution - 1);
    const f32 a = depth(ic, jc) * (1.0f - tx) + depth(i1, jc) * tx;
    const f32 b = depth(ic, j1) * (1.0f - tx) + depth(i1, j1) * tx;
    return a * (1.0f - tz) + b * tz;
}

f32 ShallowWater::heightAtWorld(f32 x, f32 z) const {
    const f32 gx = (x + m_settings.sizeX * 0.5f) / m_dx - 0.5f;
    const f32 gz = (z + m_settings.sizeZ * 0.5f) / m_dz - 0.5f;
    const int i0 = static_cast<int>(std::floor(gx));
    const int j0 = static_cast<int>(std::floor(gz));
    const f32 tx = gx - static_cast<f32>(i0);
    const f32 tz = gz - static_cast<f32>(j0);
    const int i1 = std::clamp(i0 + 1, 0, m_resolution - 1);
    const int j1 = std::clamp(j0 + 1, 0, m_resolution - 1);
    const int ic = std::clamp(i0, 0, m_resolution - 1);
    const int jc = std::clamp(j0, 0, m_resolution - 1);
    const f32 a = surface(ic, jc) * (1.0f - tx) + surface(i1, jc) * tx;
    const f32 b = surface(ic, j1) * (1.0f - tx) + surface(i1, j1) * tx;
    return a * (1.0f - tz) + b * tz;
}

Vec3 ShallowWater::normalAtWorld(f32 x, f32 z) const {
    const f32 e = std::max(m_dx, m_dz) * 0.5f;
    const f32 hL = heightAtWorld(x - e, z);
    const f32 hR = heightAtWorld(x + e, z);
    const f32 hB = heightAtWorld(x, z - e);
    const f32 hT = heightAtWorld(x, z + e);
    const Vec3 n(-(hR - hL) / (2.0f * e), 1.0f, -(hT - hB) / (2.0f * e));
    const f32 len = n.length();
    return len > 1.0e-6f ? n / len : Vec3(0.0f, 1.0f, 0.0f);
}

f32 ShallowWater::totalVolume() const {
    f32 sum = 0.0f;
    for (f32 h : m_depth) sum += h;
    return sum * m_dx * m_dz;
}

f32 ShallowWater::maxDepth() const {
    f32 maximum = 0.0f;
    for (f32 h : m_depth) maximum = std::max(maximum, h);
    return maximum;
}

f32 ShallowWater::totalKineticEnergy() const {
    f32 energy = 0.0f;
    const int n = m_resolution;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const f32 uc = 0.5f * (m_u[uIndex(i, j)] + m_u[uIndex(i + 1, j)]);
            const f32 vc = 0.5f * (m_v[vIndex(i, j)] + m_v[vIndex(i, j + 1)]);
            energy += 0.5f * m_depth[index(i, j)] * (uc * uc + vc * vc);
        }
    }
    return energy * m_dx * m_dz;
}

}  // namespace Caffeine::Fluids
