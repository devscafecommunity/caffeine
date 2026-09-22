#pragma once

#include "core/Types.hpp"

namespace Caffeine::Procedural {

f32 hash01(i32 x, i32 z, u32 seed, u32 salt = 0);
f32 valueNoise2D(f32 x, f32 z, u32 seed);
f32 fbm2D(f32 x, f32 z, u32 seed, u32 octaves = 4, f32 lacunarity = 2.0f,
          f32 persistence = 0.5f);

}  // namespace Caffeine::Procedural
