#pragma once

#include "core/Types.hpp"
#include "math/Vec4.hpp"
#include <vector>

namespace Caffeine::Terrain {

class TerrainSplatmap {
public:
    static constexpr u32 kLayerCount = 4;

    TerrainSplatmap() = default;
    TerrainSplatmap(u32 resolutionX, u32 resolutionZ);

    u32 resolutionX() const { return m_resolutionX; }
    u32 resolutionZ() const { return m_resolutionZ; }
    bool empty() const { return m_weights.empty(); }

    void resize(u32 resolutionX, u32 resolutionZ, bool resampleExisting = false);
    void fillLayer(u32 layer, f32 weight);

    Vec4 sample(u32 x, u32 z) const;
    void set(u32 x, u32 z, const Vec4& weights);
    Vec4 sampleBilinear(f32 u, f32 v) const;
    Vec4 sampleWorldXZ(f32 localX, f32 localZ, f32 worldSizeX, f32 worldSizeZ) const;

    const std::vector<Vec4>& weights() const { return m_weights; }
    std::vector<Vec4>& weights() { return m_weights; }

private:
    u32 index(u32 x, u32 z) const;

    u32 m_resolutionX = 0;
    u32 m_resolutionZ = 0;
    std::vector<Vec4> m_weights;
};

}  // namespace Caffeine::Terrain
