#include "terrain/TerrainMasks.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

constexpr const char* kBuiltins[TerrainMasks::kBuiltinCount] = {"height", "slope", "curvature"};
constexpr f32 kHalfPi = 1.57079632679f;
/// Laplacian (1/m) that maps to the end of the curvature mask's range.
constexpr f32 kCurvatureRange = 0.125f;

f32 clamp01(f32 v) { return std::clamp(v, 0.0f, 1.0f); }

/// World height in metres at (x, z) metres from the corner.
f32 heightMetres(const TerrainFrame& frame, f32 x, f32 z) {
    const f32 u = clamp01(x / std::max(frame.sizeX, 1e-4f));
    const f32 v = clamp01(z / std::max(frame.sizeZ, 1e-4f));
    return frame.heightmap->sampleBilinear(u, v) * frame.maxHeight;
}

}  // namespace

const char* TerrainMasks::builtinName(u32 index) { return index < kBuiltinCount ? kBuiltins[index] : ""; }

bool TerrainMasks::isBuiltin(const std::string& name) {
    return std::find(std::begin(kBuiltins), std::end(kBuiltins), name) != std::end(kBuiltins);
}

bool TerrainMasks::validName(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

bool TerrainMasks::set(const std::string& name, u32 resolutionX, u32 resolutionZ, const u8* data) {
    if (!validName(name) || isBuiltin(name) || resolutionX < 2 || resolutionZ < 2 || !data) return false;
    const size_t count = static_cast<size_t>(resolutionX) * resolutionZ;
    if (count > (1u << 26)) return false;  // 64 M texels: far beyond any terrain
    auto it = std::find_if(m_stored.begin(), m_stored.end(), [&](const Stored& s) { return s.name == name; });
    if (it == m_stored.end()) {
        if (m_stored.size() >= kMaxStoredMasks) return false;
        m_stored.push_back({name, 0, 0, {}});
        it = m_stored.end() - 1;
    }
    it->resolutionX = resolutionX;
    it->resolutionZ = resolutionZ;
    it->data.assign(data, data + count);
    return true;
}

bool TerrainMasks::remove(const std::string& name) {
    const auto it = std::find_if(m_stored.begin(), m_stored.end(), [&](const Stored& s) { return s.name == name; });
    if (it == m_stored.end()) return false;
    m_stored.erase(it);
    return true;
}

void TerrainMasks::clear() { m_stored.clear(); }

std::string TerrainMasks::nameAt(u32 index) const {
    if (index < kBuiltinCount) return kBuiltins[index];
    index -= kBuiltinCount;
    return index < m_stored.size() ? m_stored[index].name : std::string();
}

bool TerrainMasks::has(const std::string& name) const {
    if (isBuiltin(name)) return true;
    return std::any_of(m_stored.begin(), m_stored.end(), [&](const Stored& s) { return s.name == name; });
}

u32 TerrainMasks::resolutionX(const std::string& name) const {
    for (const Stored& s : m_stored) {
        if (s.name == name) return s.resolutionX;
    }
    return 0;
}

u32 TerrainMasks::resolutionZ(const std::string& name) const {
    for (const Stored& s : m_stored) {
        if (s.name == name) return s.resolutionZ;
    }
    return 0;
}

f32 TerrainMasks::sampleStored(const Stored& mask, f32 u, f32 v) const {
    u = clamp01(u);
    v = clamp01(v);
    const f32 fx = u * static_cast<f32>(mask.resolutionX - 1);
    const f32 fz = v * static_cast<f32>(mask.resolutionZ - 1);
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = std::min(x0 + 1, mask.resolutionX - 1), z1 = std::min(z0 + 1, mask.resolutionZ - 1);
    const f32 tx = fx - static_cast<f32>(x0), tz = fz - static_cast<f32>(z0);
    auto at = [&](u32 x, u32 z) { return static_cast<f32>(mask.data[static_cast<size_t>(z) * mask.resolutionX + x]); };
    const f32 a = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
    const f32 b = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
    return (a + (b - a) * tz) / 255.0f;
}

Vec3 TerrainMasks::normalAt(const TerrainFrame& frame, f32 x, f32 z) {
    if (!frame.heightmap || frame.heightmap->empty()) return Vec3(0.0f, 1.0f, 0.0f);
    const f32 u = clamp01(x / std::max(frame.sizeX, 1e-4f));
    const f32 v = clamp01(z / std::max(frame.sizeZ, 1e-4f));
    return frame.heightmap->sampleNormalBilinear(u, v, frame.sizeX, frame.sizeZ, frame.maxHeight);
}

bool TerrainMasks::sample(const TerrainFrame& frame, const std::string& name, f32 x, f32 z, f32& out) const {
    if (name == "height" || name == "slope" || name == "curvature") {
        if (!frame.heightmap || frame.heightmap->empty()) return false;
        const f32 u = clamp01(x / std::max(frame.sizeX, 1e-4f));
        const f32 v = clamp01(z / std::max(frame.sizeZ, 1e-4f));
        if (name == "height") {
            out = frame.heightmap->sampleBilinear(u, v);
        } else if (name == "slope") {
            const Vec3 n = normalAt(frame, x, z);
            out = clamp01(std::acos(std::clamp(n.y, -1.0f, 1.0f)) / kHalfPi);
        } else {
            // The five-point Laplacian one grid cell away, in metres: 1/m.
            const f32 cellX = frame.sizeX / static_cast<f32>(std::max(1u, frame.heightmap->resolutionX() - 1));
            const f32 cellZ = frame.sizeZ / static_cast<f32>(std::max(1u, frame.heightmap->resolutionZ() - 1));
            const f32 centre = heightMetres(frame, x, z);
            const f32 lap = (heightMetres(frame, x - cellX, z) + heightMetres(frame, x + cellX, z) - 2.0f * centre) /
                                (cellX * cellX) +
                            (heightMetres(frame, x, z - cellZ) + heightMetres(frame, x, z + cellZ) - 2.0f * centre) /
                                (cellZ * cellZ);
            out = clamp01(0.5f + 0.5f * lap / kCurvatureRange);
        }
        return true;
    }
    for (const Stored& s : m_stored) {
        if (s.name != name) continue;
        out = sampleStored(s, x / std::max(frame.sizeX, 1e-4f), z / std::max(frame.sizeZ, 1e-4f));
        return true;
    }
    return false;
}

bool TerrainMasks::rect(const TerrainFrame& frame, const std::string& name, f32 x0, f32 z0, f32 x1, f32 z1,
                        u32 width, u32 height, u8* out) const {
    if (!out || width == 0 || height == 0 || !has(name)) return false;
    for (u32 j = 0; j < height; ++j) {
        const f32 z = z0 + (z1 - z0) * (static_cast<f32>(j) + 0.5f) / static_cast<f32>(height);
        for (u32 i = 0; i < width; ++i) {
            const f32 x = x0 + (x1 - x0) * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(width);
            f32 value = 0.0f;
            if (!sample(frame, name, x, z, value)) return false;
            out[static_cast<size_t>(j) * width + i] = static_cast<u8>(std::lround(clamp01(value) * 255.0f));
        }
    }
    return true;
}

}  // namespace Caffeine::Terrain
