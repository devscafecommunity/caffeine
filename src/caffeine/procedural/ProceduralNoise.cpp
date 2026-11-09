#include "procedural/ProceduralNoise.hpp"

#include <cmath>

namespace Caffeine::Procedural {

f32 hash01(i32 x, i32 z, u32 seed, u32 salt) {
    u32 n = static_cast<u32>(x) * 374761393u + static_cast<u32>(z) * 668265263u +
            seed * 362437u + salt * 1013904223u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;
    return static_cast<f32>(n & 0xFFFFu) / 65535.0f;
}

f32 valueNoise2D(f32 x, f32 z, u32 seed) {
    const i32 x0 = static_cast<i32>(std::floor(x));
    const i32 z0 = static_cast<i32>(std::floor(z));
    const i32 x1 = x0 + 1;
    const i32 z1 = z0 + 1;
    const f32 tx = x - static_cast<f32>(x0);
    const f32 tz = z - static_cast<f32>(z0);
    const f32 sx = tx * tx * (3.0f - 2.0f * tx);
    const f32 sz = tz * tz * (3.0f - 2.0f * tz);

    const f32 n00 = hash01(x0, z0, seed);
    const f32 n10 = hash01(x1, z0, seed);
    const f32 n01 = hash01(x0, z1, seed);
    const f32 n11 = hash01(x1, z1, seed);
    const f32 nx0 = n00 + (n10 - n00) * sx;
    const f32 nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sz;
}

f32 fbm2D(f32 x, f32 z, u32 seed, u32 octaves, f32 lacunarity, f32 persistence) {
    f32 amplitude = 1.0f;
    f32 frequency = 1.0f;
    f32 sum = 0.0f;
    f32 norm = 0.0f;
    for (u32 i = 0; i < octaves; ++i) {
        sum += valueNoise2D(x * frequency, z * frequency, seed + i * 7919u) * amplitude;
        norm += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace Caffeine::Procedural
