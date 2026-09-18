#pragma once

#include "core/Types.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/generation/TerrainNoise.hpp"

namespace Caffeine::Terrain {

// Diamond-Square on a (2^N + 1) grid. Output is normalized to [0, 1].
// roughness maps to the H parameter (higher = smoother, typical 0.45–0.75).
void generateDiamondSquare(TerrainHeightmap& heightmap, u32 seed, f32 roughness);

// FFT spectral synthesis with 1/f^beta filtering. Smooth, rolling terrain; tileable.
void generateSpectralFft(TerrainHeightmap& heightmap, u32 seed, f32 spectralExponent);

bool isGridHeightModel(TerrainHeightModel model);
bool isPointSampleHeightModel(TerrainHeightModel model);

}  // namespace Caffeine::Terrain
