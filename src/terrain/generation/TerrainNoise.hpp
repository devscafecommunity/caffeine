#pragma once

#include "core/Types.hpp"

namespace Caffeine::Terrain {

enum class TerrainNoiseAlgorithm : u8 {
    Perlin = 0,
    Simplex = 1,
    Value = 2,
    Worley = 3,
};

enum class TerrainHeightModel : u8 {
    RollingHills = 0,
    FractalFBM = 1,
    RidgedMountains = 2,
    Multiplicative = 3,
    Hybrid = 4,
    Combined = 5,
    DiamondSquare = 6,
    SpectralFFT = 7,
    HeightfieldMultiply = 8,
};

// Returns noise in [0, 1].
f32 sampleNoise2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed);

// Fractal Brownian motion — combines octaves into [0, 1].
f32 sampleFbm2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                f32 persistence, f32 lacunarity);

// Ridged multifractal — sharp connected ridges in [0, 1].
f32 sampleRidgedMultifractal2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                               f32 persistence, f32 lacunarity, f32 ridgeOffset = 1.0f,
                               f32 ridgeGain = 1.5f);

f32 sampleMultiplicativeNoise2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                                f32 lacunarity, f32 contrast);

f32 sampleHeightModel2D(TerrainHeightModel model, TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed,
                        u32 octaves, f32 persistence, f32 lacunarity, f32 ridgedBlend,
                        f32 ridgeOffset, f32 ridgeGain, f32 multiplicativeContrast);

void warpDomain2D(f32 x, f32 z, u32 seed, f32 strength, f32& outX, f32& outZ);

void warpDomainFractal2D(f32 x, f32 z, u32 seed, f32 warpScale, f32 strength, u32 octaves,
                         f32& outX, f32& outZ);

void warpDomain2DMultiPass(f32 x, f32 z, u32 seed, f32 strength, u32 passes, f32& outX,
                           f32& outZ);

}  // namespace Caffeine::Terrain
