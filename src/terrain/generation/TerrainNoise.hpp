#pragma once

#include "core/Types.hpp"

namespace Caffeine::Terrain {

enum class TerrainNoiseAlgorithm : u8 {
    Perlin = 0,
    Simplex = 1,
    Value = 2,
    Worley = 3,
};

// Returns noise in [0, 1].
f32 sampleNoise2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed);

// Fractal Brownian motion — combines octaves into [0, 1].
f32 sampleFbm2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                f32 persistence, f32 lacunarity);

// Domain warping — returns warped (x, z) for more complex shapes.
void warpDomain2D(f32 x, f32 z, u32 seed, f32 strength, f32& outX, f32& outZ);

}  // namespace Caffeine::Terrain
