#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include <vector>

namespace Caffeine::Terrain {

class TerrainHeightmap {
public:
    TerrainHeightmap() = default;
    TerrainHeightmap(u32 resolutionX, u32 resolutionZ);

    u32 resolutionX() const { return m_resolutionX; }
    u32 resolutionZ() const { return m_resolutionZ; }
    bool empty() const { return m_heights.empty(); }

    void resize(u32 resolutionX, u32 resolutionZ);
    void fill(f32 normalizedHeight);

    f32 sampleNormalized(u32 x, u32 z) const;
    void setNormalized(u32 x, u32 z, f32 value);

    f32 sampleBilinear(f32 u, f32 v) const;
    Vec3 sampleNormalBilinear(f32 u, f32 v, f32 worldSizeX, f32 worldSizeZ, f32 maxHeight) const;

    const std::vector<f32>& heights() const { return m_heights; }
    std::vector<f32>& heights() { return m_heights; }

    void applyProceduralNoise(f32 amplitude, u32 seed);

private:
    u32 index(u32 x, u32 z) const;

    u32 m_resolutionX = 0;
    u32 m_resolutionZ = 0;
    std::vector<f32> m_heights;
};

}  // namespace Caffeine::Terrain
