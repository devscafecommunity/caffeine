#include "terrain/generation/TerrainFractal.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace Caffeine::Terrain {
namespace {

struct Complex {
    f32 r = 0.0f;
    f32 i = 0.0f;
};

u32 nextDiamondSquareSide(u32 target) {
    if (target < 3) return 3;
    u32 n = 1;
    while ((1u << n) + 1u < target) {
        ++n;
    }
    return (1u << n) + 1u;
}

u32 nextPowerOfTwo(u32 value) {
    u32 p = 1;
    while (p < value) {
        p <<= 1u;
    }
    return p;
}

f32 gaussianRandom(std::mt19937& rng) {
    std::uniform_real_distribution<f32> dist(0.0001f, 1.0f);
    const f32 u1 = dist(rng);
    const f32 u2 = dist(rng);
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
}

void normalizeHeightmap(std::vector<f32>& heights) {
    if (heights.empty()) return;
    f32 minH = heights[0];
    f32 maxH = heights[0];
    for (f32 h : heights) {
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    const f32 range = std::max(maxH - minH, 0.0001f);
    for (f32& h : heights) {
        h = std::clamp((h - minH) / range, 0.0f, 1.0f);
    }
}

void copyGridToHeightmap(TerrainHeightmap& heightmap, const std::vector<f32>& grid, u32 gridSize) {
    const u32 targetX = heightmap.resolutionX();
    const u32 targetZ = heightmap.resolutionZ();
    const f32 spanX = static_cast<f32>(std::max(1u, targetX - 1));
    const f32 spanZ = static_cast<f32>(std::max(1u, targetZ - 1));
    const f32 gridSpan = static_cast<f32>(std::max(1u, gridSize - 1));

    for (u32 z = 0; z < targetZ; ++z) {
        for (u32 x = 0; x < targetX; ++x) {
            const f32 u = static_cast<f32>(x) / spanX;
            const f32 v = static_cast<f32>(z) / spanZ;
            const f32 gx = u * gridSpan;
            const f32 gz = v * gridSpan;

            const u32 x0 = static_cast<u32>(gx);
            const u32 z0 = static_cast<u32>(gz);
            const u32 x1 = std::min(x0 + 1, gridSize - 1);
            const u32 z1 = std::min(z0 + 1, gridSize - 1);
            const f32 tx = gx - static_cast<f32>(x0);
            const f32 tz = gz - static_cast<f32>(z0);

            const f32 h00 = grid[z0 * gridSize + x0];
            const f32 h10 = grid[z0 * gridSize + x1];
            const f32 h01 = grid[z1 * gridSize + x0];
            const f32 h11 = grid[z1 * gridSize + x1];
            const f32 hx0 = h00 + (h10 - h00) * tx;
            const f32 hx1 = h01 + (h11 - h01) * tx;
            const f32 h = hx0 + (hx1 - hx0) * tz;
            heightmap.setNormalized(x, z, h);
        }
    }
}

void diamondSquareCore(std::vector<f32>& grid, u32 size, u32 seed, f32 roughness) {
    roughness = std::clamp(roughness, 0.05f, 0.95f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);

    auto at = [&](u32 x, u32 z) -> f32& { return grid[z * size + x]; };

    at(0, 0) = dist(rng);
    at(size - 1, 0) = dist(rng);
    at(0, size - 1) = dist(rng);
    at(size - 1, size - 1) = dist(rng);

    f32 scale = 1.0f;
    u32 step = size - 1;
    while (step > 1) {
        const u32 half = step / 2;
        const f32 displacement = scale * std::pow(2.0f, -roughness);

        for (u32 z = half; z < size; z += step) {
            for (u32 x = half; x < size; x += step) {
                const f32 avg = (at(x - half, z - half) + at(x + half, z - half) +
                                 at(x - half, z + half) + at(x + half, z + half)) *
                                0.25f;
                at(x, z) = avg + dist(rng) * displacement;
            }
        }

        for (u32 z = 0; z < size; z += half) {
            const u32 shift = ((z / half) % 2 == 0) ? half : 0;
            for (u32 x = shift; x < size; x += step) {
                f32 sum = 0.0f;
                u32 count = 0;
                auto accumulate = [&](u32 nx, u32 nz) {
                    if (nx < size && nz < size) {
                        sum += at(nx, nz);
                        ++count;
                    }
                };
                accumulate(x - half, z);
                accumulate(x + half, z);
                accumulate(x, z - half);
                accumulate(x, z + half);
                at(x, z) = sum / static_cast<f32>(std::max(1u, count)) + dist(rng) * displacement;
            }
        }

        step = half;
        scale *= 0.5f;
    }

    normalizeHeightmap(grid);
}

void fftInPlace(std::vector<Complex>& data, bool inverse) {
    const size_t n = data.size();
    if (n < 2) return;

    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    const f32 sign = inverse ? 1.0f : -1.0f;
    for (size_t len = 2; len <= n; len <<= 1) {
        const f32 angle = sign * 6.2831853f / static_cast<f32>(len);
        const f32 wlenR = std::cos(angle);
        const f32 wlenI = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            f32 wR = 1.0f;
            f32 wI = 0.0f;
            for (size_t k = 0; k < len / 2; ++k) {
                const Complex u = data[i + k];
                const Complex v{data[i + k + len / 2].r * wR - data[i + k + len / 2].i * wI,
                                data[i + k + len / 2].r * wI + data[i + k + len / 2].i * wR};
                data[i + k].r = u.r + v.r;
                data[i + k].i = u.i + v.i;
                data[i + k + len / 2].r = u.r - v.r;
                data[i + k + len / 2].i = u.i - v.i;
                const f32 nextWR = wR * wlenR - wI * wlenI;
                wI = wR * wlenI + wI * wlenR;
                wR = nextWR;
            }
        }
    }

    if (inverse) {
        const f32 invN = 1.0f / static_cast<f32>(n);
        for (Complex& c : data) {
            c.r *= invN;
            c.i *= invN;
        }
    }
}

void fft2d(std::vector<Complex>& grid, u32 width, u32 height, bool inverse) {
    std::vector<Complex> row(width);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            row[x] = grid[y * width + x];
        }
        fftInPlace(row, inverse);
        for (u32 x = 0; x < width; ++x) {
            grid[y * width + x] = row[x];
        }
    }

    std::vector<Complex> col(height);
    for (u32 x = 0; x < width; ++x) {
        for (u32 y = 0; y < height; ++y) {
            col[y] = grid[y * width + x];
        }
        fftInPlace(col, inverse);
        for (u32 y = 0; y < height; ++y) {
            grid[y * width + x] = col[y];
        }
    }
}

}  // namespace

bool isGridHeightModel(TerrainHeightModel model) {
    return model == TerrainHeightModel::DiamondSquare || model == TerrainHeightModel::SpectralFFT;
}

bool isPointSampleHeightModel(TerrainHeightModel model) {
    return !isGridHeightModel(model) && model != TerrainHeightModel::HeightfieldMultiply;
}

void generateDiamondSquare(TerrainHeightmap& heightmap, u32 seed, f32 roughness) {
    const u32 targetX = heightmap.resolutionX();
    const u32 targetZ = heightmap.resolutionZ();
    const u32 gridSize = nextDiamondSquareSide(std::max(targetX, targetZ));

    std::vector<f32> grid(static_cast<size_t>(gridSize) * static_cast<size_t>(gridSize), 0.0f);
    diamondSquareCore(grid, gridSize, seed, roughness);
    copyGridToHeightmap(heightmap, grid, gridSize);
}

void generateSpectralFft(TerrainHeightmap& heightmap, u32 seed, f32 spectralExponent) {
    spectralExponent = std::clamp(spectralExponent, 0.5f, 4.0f);
    const u32 targetX = heightmap.resolutionX();
    const u32 targetZ = heightmap.resolutionZ();
    const u32 fftW = nextPowerOfTwo(std::max(2u, targetX));
    const u32 fftH = nextPowerOfTwo(std::max(2u, targetZ));

    std::mt19937 rng(seed);
    std::vector<Complex> spectrum(static_cast<size_t>(fftW) * static_cast<size_t>(fftH));
    for (u32 y = 0; y < fftH; ++y) {
        for (u32 x = 0; x < fftW; ++x) {
            spectrum[y * fftW + x] = {gaussianRandom(rng), 0.0f};
        }
    }

    fft2d(spectrum, fftW, fftH, false);

    const f32 centerX = static_cast<f32>(fftW) * 0.5f;
    const f32 centerY = static_cast<f32>(fftH) * 0.5f;
    for (u32 y = 0; y < fftH; ++y) {
        for (u32 x = 0; x < fftW; ++x) {
            f32 dx = static_cast<f32>(x) - centerX;
            f32 dy = static_cast<f32>(y) - centerY;
            if (dx < 0.0f) dx = -dx;
            if (dy < 0.0f) dy = -dy;
            const f32 freq = std::sqrt(dx * dx + dy * dy) + 1.0f;
            const f32 filter = 1.0f / std::pow(freq, spectralExponent);
            spectrum[y * fftW + x].r *= filter;
            spectrum[y * fftW + x].i *= filter;
        }
    }

    fft2d(spectrum, fftW, fftH, true);

    std::vector<f32> grid(static_cast<size_t>(fftW) * static_cast<size_t>(fftH));
    for (u32 y = 0; y < fftH; ++y) {
        for (u32 x = 0; x < fftW; ++x) {
            grid[y * fftW + x] = spectrum[y * fftW + x].r;
        }
    }
    normalizeHeightmap(grid);
    copyGridToHeightmap(heightmap, grid, fftW);
}

}  // namespace Caffeine::Terrain
