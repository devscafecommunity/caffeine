#include "terrain/generation/TerrainNoise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Caffeine::Terrain {
namespace {

constexpr f32 kInv255 = 1.0f / 255.0f;

u32 hash2D(i32 x, i32 z, u32 seed) {
    u32 n = static_cast<u32>(x) * 374761393u + static_cast<u32>(z) * 668265263u + seed * 362437u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;
    return n;
}

f32 fade(f32 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

f32 grad2D(u32 hash, f32 x, f32 z) {
    const u32 h = hash & 7u;
    const f32 u = h < 4 ? x : z;
    const f32 v = h < 4 ? z : x;
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -2.0f * v : 2.0f * v);
}

f32 valueNoise2D(f32 x, f32 z, u32 seed) {
    const i32 x0 = static_cast<i32>(std::floor(x));
    const i32 z0 = static_cast<i32>(std::floor(z));
    const i32 x1 = x0 + 1;
    const i32 z1 = z0 + 1;

    const f32 tx = x - static_cast<f32>(x0);
    const f32 tz = z - static_cast<f32>(z0);

    const f32 v00 = static_cast<f32>(hash2D(x0, z0, seed) & 0xFFFFu) / 65535.0f;
    const f32 v10 = static_cast<f32>(hash2D(x1, z0, seed) & 0xFFFFu) / 65535.0f;
    const f32 v01 = static_cast<f32>(hash2D(x0, z1, seed) & 0xFFFFu) / 65535.0f;
    const f32 v11 = static_cast<f32>(hash2D(x1, z1, seed) & 0xFFFFu) / 65535.0f;

    const f32 sx = fade(tx);
    const f32 sz = fade(tz);
    const f32 ix0 = lerp(v00, v10, sx);
    const f32 ix1 = lerp(v01, v11, sx);
    return lerp(ix0, ix1, sz);
}

f32 perlinNoise2D(f32 x, f32 z, u32 seed) {
    const i32 x0 = static_cast<i32>(std::floor(x));
    const i32 z0 = static_cast<i32>(std::floor(z));
    const i32 x1 = x0 + 1;
    const i32 z1 = z0 + 1;

    const f32 tx = x - static_cast<f32>(x0);
    const f32 tz = z - static_cast<f32>(z0);

    const f32 g00 = grad2D(hash2D(x0, z0, seed), tx, tz);
    const f32 g10 = grad2D(hash2D(x1, z0, seed), tx - 1.0f, tz);
    const f32 g01 = grad2D(hash2D(x0, z1, seed), tx, tz - 1.0f);
    const f32 g11 = grad2D(hash2D(x1, z1, seed), tx - 1.0f, tz - 1.0f);

    const f32 sx = fade(tx);
    const f32 sz = fade(tz);
    const f32 ix0 = lerp(g00, g10, sx);
    const f32 ix1 = lerp(g01, g11, sx);
    const f32 n = lerp(ix0, ix1, sz);
    return std::clamp((n + 1.0f) * 0.5f, 0.0f, 1.0f);
}

f32 simplexNoise2D(f32 xin, f32 zin, u32 seed) {
    constexpr f32 F2 = 0.366025403f;
    constexpr f32 G2 = 0.211324865f;

    const f32 s = (xin + zin) * F2;
    const i32 i = static_cast<i32>(std::floor(xin + s));
    const i32 j = static_cast<i32>(std::floor(zin + s));

    const f32 t = static_cast<f32>(i + j) * G2;
    const f32 x0 = xin - (static_cast<f32>(i) - t);
    const f32 z0 = zin - (static_cast<f32>(j) - t);

    i32 i1 = 0;
    i32 j1 = 0;
    if (x0 > z0) {
        i1 = 1;
        j1 = 0;
    } else {
        i1 = 0;
        j1 = 1;
    }

    const f32 x1 = x0 - static_cast<f32>(i1) + G2;
    const f32 z1 = z0 - static_cast<f32>(j1) + G2;
    const f32 x2 = x0 - 1.0f + 2.0f * G2;
    const f32 z2 = z0 - 1.0f + 2.0f * G2;

    auto contrib = [&](f32 x, f32 z, i32 cx, i32 cz) {
        f32 t = 0.5f - x * x - z * z;
        if (t < 0.0f) return 0.0f;
        t *= t;
        return t * t * grad2D(hash2D(cx, cz, seed), x, z);
    };

    f32 n = contrib(x0, z0, i, j);
    n += contrib(x1, z1, i + i1, j + j1);
    n += contrib(x2, z2, i + 1, j + 1);
    return std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f);
}

f32 worleyNoise2D(f32 x, f32 z, u32 seed) {
    const i32 cellX = static_cast<i32>(std::floor(x));
    const i32 cellZ = static_cast<i32>(std::floor(z));
    f32 minDist = 1.0f;

    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const i32 cx = cellX + dx;
            const i32 cz = cellZ + dz;
            const u32 h = hash2D(cx, cz, seed);
            const f32 px = static_cast<f32>(cx) + static_cast<f32>(h & 0xFFu) * kInv255;
            const f32 pz = static_cast<f32>(cz) + static_cast<f32>((h >> 8) & 0xFFu) * kInv255;
            const f32 distX = x - px;
            const f32 distZ = z - pz;
            const f32 dist = std::sqrt(distX * distX + distZ * distZ);
            minDist = std::min(minDist, dist);
        }
    }

    return std::clamp(minDist, 0.0f, 1.0f);
}

}  // namespace

f32 sampleNoise2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed) {
    switch (type) {
        case TerrainNoiseAlgorithm::Perlin:
            return perlinNoise2D(x, z, seed);
        case TerrainNoiseAlgorithm::Simplex:
            return simplexNoise2D(x, z, seed);
        case TerrainNoiseAlgorithm::Value:
            return valueNoise2D(x, z, seed);
        case TerrainNoiseAlgorithm::Worley:
            return worleyNoise2D(x, z, seed);
    }
    return 0.0f;
}

f32 sampleFbm2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                f32 persistence, f32 lacunarity) {
    octaves = std::max(1u, octaves);
    persistence = std::clamp(persistence, 0.01f, 1.0f);
    lacunarity = std::max(1.01f, lacunarity);

    f32 sum = 0.0f;
    f32 amp = 1.0f;
    f32 freq = 1.0f;
    f32 maxAmp = 0.0f;

    for (u32 i = 0; i < octaves; ++i) {
        sum += sampleNoise2D(type, x * freq, z * freq, seed + i * 131u) * amp;
        maxAmp += amp;
        amp *= persistence;
        freq *= lacunarity;
    }

    return maxAmp > 0.0f ? std::clamp(sum / maxAmp, 0.0f, 1.0f) : 0.0f;
}

f32 sampleRidgedMultifractal2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                               f32 persistence, f32 lacunarity, f32 ridgeOffset, f32 ridgeGain) {
    octaves = std::max(1u, octaves);
    persistence = std::clamp(persistence, 0.01f, 1.0f);
    lacunarity = std::max(1.01f, lacunarity);
    ridgeOffset = std::clamp(ridgeOffset, 0.1f, 2.0f);
    ridgeGain = std::clamp(ridgeGain, 0.5f, 3.0f);

    f32 result = 0.0f;
    f32 amplitude = 1.0f;
    f32 frequency = 1.0f;
    f32 weight = 1.0f;
    f32 maxAmp = 0.0f;

    for (u32 i = 0; i < octaves; ++i) {
        const f32 centered = sampleNoise2D(type, x * frequency, z * frequency, seed + i * 131u) * 2.0f -
                             1.0f;
        const f32 ridgeDelta = ridgeOffset - std::abs(centered);
        f32 signal = std::max(0.0f, 1.0f - ridgeDelta * ridgeDelta);
        signal *= weight;
        result += signal * amplitude;
        weight = std::pow(std::clamp(signal * ridgeGain, 0.0f, 1.0f), 0.8f);
        maxAmp += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return maxAmp > 0.0f ? std::clamp(result / maxAmp, 0.0f, 1.0f) : 0.0f;
}

f32 sampleMultiplicativeNoise2D(TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed, u32 octaves,
                                f32 lacunarity, f32 contrast) {
    octaves = std::max(1u, octaves);
    lacunarity = std::max(1.01f, lacunarity);
    contrast = std::clamp(contrast, 0.25f, 4.0f);

    f32 product = 1.0f;
    f32 frequency = 1.0f;
    for (u32 i = 0; i < octaves; ++i) {
        const f32 layer =
            std::pow(std::clamp(sampleNoise2D(type, x * frequency, z * frequency, seed + i * 97u),
                                0.001f, 1.0f),
                     contrast);
        product *= layer;
        frequency *= lacunarity;
    }

    return std::clamp(std::pow(product, 1.0f / static_cast<f32>(octaves)), 0.0f, 1.0f);
}

f32 sampleHeightModel2D(TerrainHeightModel model, TerrainNoiseAlgorithm type, f32 x, f32 z, u32 seed,
                        u32 octaves, f32 persistence, f32 lacunarity, f32 ridgedBlend,
                        f32 ridgeOffset, f32 ridgeGain, f32 multiplicativeContrast) {
    const f32 fbm = sampleFbm2D(type, x, z, seed, octaves, persistence, lacunarity);
    switch (model) {
        case TerrainHeightModel::RollingHills:
        case TerrainHeightModel::FractalFBM:
            return fbm;
        case TerrainHeightModel::RidgedMountains:
            return sampleRidgedMultifractal2D(type, x, z, seed + 53u, octaves, persistence,
                                              lacunarity, ridgeOffset, ridgeGain);
        case TerrainHeightModel::Multiplicative:
            return sampleMultiplicativeNoise2D(type, x, z, seed + 211u, octaves, lacunarity,
                                               multiplicativeContrast);
        case TerrainHeightModel::Hybrid: {
            const f32 ridged = sampleRidgedMultifractal2D(type, x, z, seed + 53u, octaves,
                                                          persistence, lacunarity, ridgeOffset,
                                                          ridgeGain);
            const f32 blend = std::clamp(ridgedBlend, 0.0f, 1.0f);
            return ridged * blend + fbm * (1.0f - blend);
        }
        case TerrainHeightModel::Combined: {
            const f32 mult = sampleMultiplicativeNoise2D(type, x, z, seed + 211u, octaves, lacunarity,
                                                         multiplicativeContrast);
            const f32 blend = std::clamp(ridgedBlend, 0.0f, 1.0f);
            return fbm * (1.0f - blend) + mult * blend;
        }
        case TerrainHeightModel::DiamondSquare:
        case TerrainHeightModel::SpectralFFT:
        case TerrainHeightModel::HeightfieldMultiply:
            return 0.5f;
    }
    return fbm;
}

void warpDomain2D(f32 x, f32 z, u32 seed, f32 strength, f32& outX, f32& outZ) {
    strength = std::max(0.0f, strength);
    const f32 warpX = (sampleNoise2D(TerrainNoiseAlgorithm::Perlin, x + 17.0f, z + 3.0f, seed + 91u) -
                       0.5f) *
                      2.0f;
    const f32 warpZ = (sampleNoise2D(TerrainNoiseAlgorithm::Perlin, x + 5.0f, z + 29.0f, seed + 173u) -
                       0.5f) *
                      2.0f;
    outX = x + warpX * strength;
    outZ = z + warpZ * strength;
}

void warpDomainFractal2D(f32 x, f32 z, u32 seed, f32 warpScale, f32 strength, u32 octaves,
                         f32& outX, f32& outZ) {
    octaves = std::max(1u, octaves);
    warpScale = std::max(warpScale, 0.0001f);
    strength = std::max(0.0f, strength);

    f32 warpX = 0.0f;
    f32 warpZ = 0.0f;
    f32 amplitude = 1.0f;
    f32 frequency = 1.0f;
    f32 maxAmp = 0.0f;

    for (u32 i = 0; i < octaves; ++i) {
        const f32 sx = x * frequency / warpScale;
        const f32 sz = z * frequency / warpScale;
        warpX += (sampleNoise2D(TerrainNoiseAlgorithm::Perlin, sx + 17.0f, sz + 3.0f, seed + 1000u + i) -
                  0.5f) *
                 2.0f * amplitude;
        warpZ += (sampleNoise2D(TerrainNoiseAlgorithm::Perlin, sx + 5.0f, sz + 29.0f, seed + 2000u + i) -
                  0.5f) *
                 2.0f * amplitude;
        maxAmp += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    const f32 invMax = maxAmp > 0.0f ? 1.0f / maxAmp : 1.0f;
    outX = x + warpX * invMax * strength;
    outZ = z + warpZ * invMax * strength;
}

void warpDomain2DMultiPass(f32 x, f32 z, u32 seed, f32 strength, u32 passes, f32& outX,
                           f32& outZ) {
    passes = std::max(1u, passes);
    outX = x;
    outZ = z;
    for (u32 pass = 0; pass < passes; ++pass) {
        const f32 passStrength = strength * (pass == 0 ? 1.0f : 0.5f);
        warpDomain2D(outX, outZ, seed + pass * 997u, passStrength, outX, outZ);
    }
}

}  // namespace Caffeine::Terrain
