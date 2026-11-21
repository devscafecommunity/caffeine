#pragma once

#include "core/Types.hpp"

namespace Caffeine::Procedural {

f32 hash01(i32 x, i32 z, u32 seed, u32 salt = 0);
/// Hash in [0, 1) that decides **placement**. `version` 1 is the legacy 32-bit `hash01`; 2 and up
/// is the platform-independent SplitMix64 of `core/Hash.hpp`. Worlds store the version they were
/// generated with, so changing the algorithm never moves the props of an existing world.
f32 placementHash(u32 version, i32 x, i32 z, u32 seed, u32 salt = 0);
f32 valueNoise2D(f32 x, f32 z, u32 seed);
f32 fbm2D(f32 x, f32 z, u32 seed, u32 octaves = 4, f32 lacunarity = 2.0f,
          f32 persistence = 0.5f);

}  // namespace Caffeine::Procedural
