#include "terrain/edit/BrushStamps.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

// ------------------------------------------------------------------------------------------------ arithmetic helpers

f32 clamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
f32 smooth(f32 t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}
f32 absf(f32 v) { return v < 0.0f ? -v : v; }
f32 frac(f32 v) { return v - std::floor(v); }

/// A number in 0..1 from an integer lattice point and the seed (SplitMix-style finaliser).
f32 hash01(i32 x, i32 y, u64 seed, u32 salt = 0) {
    u64 h = seed ^ (static_cast<u64>(static_cast<u32>(x)) * 0x9E3779B97F4A7C15ull) ^
            (static_cast<u64>(static_cast<u32>(y)) * 0xC2B2AE3D27D4EB4Full) ^ (static_cast<u64>(salt) * 0x165667B19E3779F9ull);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return static_cast<f32>(h & 0xFFFFFFu) / 16777216.0f;
}

/// Smooth value noise, 0..1.
f32 vnoise(f32 x, f32 y, u64 seed) {
    const f32 fx = std::floor(x), fy = std::floor(y);
    const i32 ix = static_cast<i32>(fx), iy = static_cast<i32>(fy);
    const f32 tx = smooth(x - fx), ty = smooth(y - fy);
    const f32 a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed);
    const f32 c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
    const f32 top = a + (b - a) * tx;
    const f32 bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

f32 fbm(f32 x, f32 y, u64 seed, int octaves) {
    f32 sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += vnoise(x, y, seed + static_cast<u64>(i) * 7919u) * amplitude;
        norm += amplitude;
        x *= 2.0f;
        y *= 2.0f;
        amplitude *= 0.5f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

/// Cellular noise: distances to the nearest (f1) and second nearest (f2) feature point, in cells, and the nearest
/// cell's key (0..1) for per-cell variation.
void worley(f32 x, f32 y, u64 seed, f32& f1, f32& f2, f32& cellKey) {
    const i32 ix = static_cast<i32>(std::floor(x)), iy = static_cast<i32>(std::floor(y));
    f1 = f2 = 8.0f;
    cellKey = 0.0f;
    for (i32 oy = -1; oy <= 1; ++oy) {
        for (i32 ox = -1; ox <= 1; ++ox) {
            const i32 cx = ix + ox, cy = iy + oy;
            const f32 px = static_cast<f32>(cx) + hash01(cx, cy, seed, 1);
            const f32 py = static_cast<f32>(cy) + hash01(cx, cy, seed, 2);
            const f32 d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (d < f1) {
                f2 = f1;
                f1 = d;
                cellKey = hash01(cx, cy, seed, 3);
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
}

/// A stamp point: the position across the diameter (-1..1), its distance from the centre, the seed.
struct P {
    f32 u, v, r;
    u64 seed;
};
using StampFn = f32 (*)(const P&);

// ------------------------------------------------------------------------------------------------------------ Basic
f32 roundSoft(const P& p) { return smooth(1.0f - p.r); }
f32 roundHard(const P& p) { return p.r < 0.92f ? 1.0f : clamp01((1.0f - p.r) / 0.08f); }
f32 square(const P& p) { return clamp01((0.95f - std::max(absf(p.u), absf(p.v))) / 0.1f); }
f32 diamond(const P& p) { return clamp01((1.0f - (absf(p.u) + absf(p.v))) * 5.0f); }
f32 ring(const P& p) { return smooth(1.0f - absf(p.r - 0.7f) / 0.22f); }
f32 cone(const P& p) { return clamp01(1.0f - p.r); }
f32 dome(const P& p) { return std::sqrt(clamp01(1.0f - p.r * p.r)); }
f32 plateau(const P& p) { return clamp01((0.9f - p.r) / 0.3f); }

// ------------------------------------------------------------------------------------------------------------ Noise
f32 perlinFine(const P& p) { return vnoise(p.u * 8.0f + 20.0f, p.v * 8.0f + 20.0f, p.seed); }
f32 perlinCoarse(const P& p) { return vnoise(p.u * 2.5f + 20.0f, p.v * 2.5f + 20.0f, p.seed); }
f32 ridged(const P& p) {
    f32 sum = 0.0f, amplitude = 0.55f, x = p.u * 3.0f + 20.0f, y = p.v * 3.0f + 20.0f;
    for (int i = 0; i < 4; ++i) {
        const f32 n = 1.0f - absf(2.0f * vnoise(x, y, p.seed + static_cast<u64>(i) * 131u) - 1.0f);
        sum += n * n * amplitude;
        amplitude *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
    }
    return clamp01(sum * 1.1f);
}
f32 billow(const P& p) {
    f32 sum = 0.0f, amplitude = 0.55f, x = p.u * 3.0f + 20.0f, y = p.v * 3.0f + 20.0f;
    for (int i = 0; i < 4; ++i) {
        sum += absf(2.0f * vnoise(x, y, p.seed + static_cast<u64>(i) * 173u) - 1.0f) * amplitude;
        amplitude *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
    }
    return clamp01(sum * 1.2f);
}
f32 fbmRough(const P& p) { return fbm(p.u * 4.0f + 20.0f, p.v * 4.0f + 20.0f, p.seed, 6); }
f32 worleyCells(const P& p) {
    f32 f1, f2, key;
    worley(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed, f1, f2, key);
    return clamp01(1.0f - f1 * 1.5f);
}
f32 worleyRidges(const P& p) {
    f32 f1, f2, key;
    worley(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed, f1, f2, key);
    return clamp01(1.0f - (f2 - f1) * 3.0f);
}
f32 grain(const P& p) { return vnoise(p.u * 32.0f + 20.0f, p.v * 32.0f + 20.0f, p.seed); }

// ------------------------------------------------------------------------------------------------------------- Rock
f32 strata(const P& p) {
    const f32 t = fbm(p.u * 1.4f + 20.0f, p.v * 1.4f + 20.0f, p.seed, 3) * 6.0f;
    const f32 k = std::floor(t), f = t - k;
    return clamp01((k + smooth((f - 0.65f) / 0.35f)) / 6.0f);
}
f32 cliffFace(const P& p) {
    const f32 edge = p.u * 0.9f + (vnoise(p.v * 4.0f + 20.0f, p.u * 2.0f, p.seed) - 0.5f) * 0.7f;
    return smooth(edge * 3.0f + 0.5f);
}
f32 boulders(const P& p) {
    f32 f1, f2, key;
    worley(p.u * 2.2f + 20.0f, p.v * 2.2f + 20.0f, p.seed, f1, f2, key);
    const f32 radius = 0.35f + key * 0.25f;
    const f32 d = f1 / radius;
    return d >= 1.0f ? 0.0f : std::sqrt(1.0f - d * d) * (0.7f + key * 0.3f);
}
f32 fractured(const P& p) {
    f32 f1, f2, key;
    worley(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed, f1, f2, key);
    return clamp01((f2 - f1) * 5.0f) * (0.75f + key * 0.25f);
}
f32 scree(const P& p) {
    f32 f1, f2, key;
    worley(p.u * 9.0f + 20.0f, p.v * 9.0f + 20.0f, p.seed, f1, f2, key);
    if (key > 0.4f) return 0.0f;
    const f32 d = f1 / 0.4f;
    return d >= 1.0f ? 0.0f : std::sqrt(1.0f - d * d) * (0.5f + key);
}
f32 rockRidge(const P& p) {
    const f32 spine = clamp01(1.0f - absf(p.v) * 1.3f);
    return clamp01(spine * std::sqrt(spine) * (0.55f + 0.45f * fbm(p.u * 3.0f + 20.0f, p.v + 20.0f, p.seed, 3)));
}
f32 erodedRock(const P& p) { return dome(p) * (0.45f + 0.55f * fbm(p.u * 5.0f + 20.0f, p.v * 5.0f + 20.0f, p.seed, 4)); }

// ------------------------------------------------------------------------------------------------------------ Dunes
f32 ripples(const P& p) {
    const f32 x = p.u * 7.0f + (vnoise(p.u * 2.0f + 20.0f, p.v * 2.0f + 20.0f, p.seed) - 0.5f) * 2.0f;
    const f32 t = frac(x);
    return (1.0f - absf(2.0f * t - 1.0f)) * clamp01(1.4f - p.r);
}
f32 duneCrescent(const P& p) {
    const f32 x = p.u * 0.9f + 0.55f * p.v * p.v - 0.25f;
    return x > 0.0f ? clamp01(1.0f - x * 3.2f) : clamp01(1.0f + x * 1.1f);
}
f32 sandWaves(const P& p) {
    const f32 x = frac(p.u * 3.0f + vnoise(p.u * 1.5f + 20.0f, p.v * 3.0f + 20.0f, p.seed) * 0.6f);
    return (x < 0.8f ? x / 0.8f : (1.0f - x) / 0.2f) * clamp01(1.4f - p.r);
}
f32 windStreaks(const P& p) { return vnoise(p.u * 1.6f + 20.0f, p.v * 14.0f + 20.0f, p.seed) * clamp01(1.3f - p.r); }

// ---------------------------------------------------------------------------------------------------------- Craters
f32 craterProfile(f32 r) {
    // A bowl inside a raised rim, -1 .. +0.4 (0 outside).
    if (r < 0.62f) {
        const f32 t = r / 0.62f;
        return -(1.0f - t * t);
    }
    if (r < 1.0f) {
        const f32 t = (r - 0.62f) / 0.38f;
        return 0.4f * (1.0f - t) * (1.0f - t) * (t < 0.15f ? t / 0.15f : 1.0f);
    }
    return 0.0f;
}
f32 craterSimple(const P& p) { return clamp01(0.5f + 0.5f * craterProfile(p.r)); }
f32 craterComplex(const P& p) {
    const f32 peak = p.r < 0.2f ? (1.0f - p.r / 0.2f) * 0.55f : 0.0f;
    return clamp01(0.5f + 0.5f * (craterProfile(p.r) + peak));
}
f32 craterOld(const P& p) {
    const f32 worn = 0.45f + 0.55f * fbm(p.u * 4.0f + 20.0f, p.v * 4.0f + 20.0f, p.seed, 4);
    return clamp01(0.5f + 0.5f * craterProfile(p.r) * 0.6f * worn);
}
f32 impactSplash(const P& p) {
    // A pseudo-angle (no trigonometry) turns the radial streaks into noise along one axis.
    const f32 s = absf(p.u) + absf(p.v) + 1e-4f;
    const f32 angle = (p.u >= 0.0f ? p.v / s : 2.0f - p.v / s);
    const f32 streak = vnoise(angle * 9.0f + 20.0f, p.r * 1.5f, p.seed);
    return clamp01(0.5f + 0.5f * (streak - 0.35f) * clamp01(1.0f - p.r) * 1.6f + 0.5f * craterProfile(p.r) * 0.5f);
}
f32 pit(const P& p) { return dome(p); }

// -------------------------------------------------------------------------------------------------------- Terraces
f32 steppedValue(f32 value, int steps, f32 soft) {
    const f32 t = clamp01(value) * static_cast<f32>(steps);
    const f32 k = std::floor(t);
    return clamp01((k + smooth((t - k - (1.0f - soft)) / soft)) / static_cast<f32>(steps));
}
f32 terracesSoft(const P& p) { return steppedValue(dome(p), 6, 0.6f); }
f32 terracesHard(const P& p) { return steppedValue(cone(p), 5, 0.12f); }
f32 ricePaddies(const P& p) {
    return steppedValue(fbm(p.u * 2.2f + 20.0f, p.v * 2.2f + 20.0f, p.seed, 3), 7, 0.35f) * clamp01(1.5f - p.r);
}

// ------------------------------------------------------------------------------------------------------------ Shapes
f32 mound(const P& p) { return dome(p) * (0.85f + 0.15f * fbm(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed, 3)); }
f32 hillCluster(const P& p) {
    auto hill = [&](f32 cx, f32 cz, f32 radius, f32 height) {
        const f32 d = std::sqrt((p.u - cx) * (p.u - cx) + (p.v - cz) * (p.v - cz)) / radius;
        return d >= 1.0f ? 0.0f : std::sqrt(1.0f - d * d) * height;
    };
    return clamp01(std::max({hill(-0.3f, -0.15f, 0.55f, 0.85f), hill(0.35f, 0.1f, 0.5f, 1.0f), hill(0.0f, 0.5f, 0.4f, 0.6f)}));
}
f32 ridgeLine(const P& p) {
    const f32 d = absf(p.v - p.u * 0.35f) * 1.5f;
    return clamp01(1.0f - d) * clamp01(1.25f - p.r) * (0.7f + 0.3f * fbm(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed, 3));
}
f32 valley(const P& p) {
    const f32 depth = clamp01(1.0f - absf(p.v) * 1.25f);
    return clamp01(0.5f - 0.5f * depth * depth * clamp01(1.3f - absf(p.u)));
}
f32 volcano(const P& p) {
    const f32 slope = clamp01(1.0f - p.r);
    return p.r < 0.18f ? clamp01(0.72f + p.r * 0.9f) : slope * 0.95f + 0.05f * (1.0f - p.r) * (1.0f - p.r);
}
f32 mesa(const P& p) {
    const f32 edge = 0.78f + (vnoise(p.u * 3.0f + 20.0f, p.v * 3.0f + 20.0f, p.seed) - 0.5f) * 0.3f;
    return clamp01((edge - p.r) / 0.07f);
}
f32 riverBed(const P& p) {
    const f32 centre = (vnoise(p.u * 1.8f + 20.0f, 3.0f, p.seed) - 0.5f) * 0.9f;
    const f32 d = absf(p.v - centre) * 3.2f;
    const f32 channel = d < 1.0f ? -(1.0f - d * d) : 0.0f;
    return clamp01(0.5f + 0.5f * channel * clamp01(1.4f - absf(p.u)));
}

struct Entry {
    StampInfo info;
    StampFn fn;
};

using M = TerrainBrushMode;

const std::vector<Entry>& table() {
    static const std::vector<Entry> entries = {
        // id, name, category, signed, mode, radius, strength, spacing ratio
        {{"round_soft", "Round soft", "Basic", false, M::Raise, 8.0f, 0.5f, 0.2f}, roundSoft},
        {{"round_hard", "Round hard", "Basic", false, M::Raise, 6.0f, 0.5f, 0.2f}, roundHard},
        {{"square", "Square", "Basic", false, M::Raise, 6.0f, 0.5f, 0.2f}, square},
        {{"diamond", "Diamond", "Basic", false, M::Raise, 6.0f, 0.5f, 0.2f}, diamond},
        {{"ring", "Ring", "Basic", false, M::Raise, 8.0f, 0.5f, 0.2f}, ring},
        {{"cone", "Cone", "Basic", false, M::Raise, 10.0f, 0.6f, 0.2f}, cone},
        {{"dome", "Dome", "Basic", false, M::Raise, 10.0f, 0.6f, 0.2f}, dome},
        {{"plateau", "Plateau", "Basic", false, M::Raise, 10.0f, 0.5f, 0.2f}, plateau},

        {{"perlin_fine", "Perlin fine", "Noise", false, M::Raise, 8.0f, 0.4f, 0.2f}, perlinFine},
        {{"perlin_coarse", "Perlin coarse", "Noise", false, M::Raise, 12.0f, 0.8f, 0.2f}, perlinCoarse},
        {{"ridged", "Ridged", "Noise", false, M::Raise, 12.0f, 0.9f, 0.2f}, ridged},
        {{"billow", "Billow", "Noise", false, M::Raise, 12.0f, 0.7f, 0.2f}, billow},
        {{"fbm_rough", "Rough fBm", "Noise", false, M::Raise, 12.0f, 0.8f, 0.2f}, fbmRough},
        {{"worley_cells", "Worley cells", "Noise", false, M::Raise, 12.0f, 0.8f, 0.2f}, worleyCells},
        {{"worley_ridges", "Worley ridges", "Noise", false, M::Raise, 12.0f, 0.7f, 0.2f}, worleyRidges},
        {{"grain", "Grain", "Noise", false, M::Raise, 6.0f, 0.15f, 0.15f}, grain},

        {{"strata", "Strata", "Rock", false, M::Raise, 14.0f, 1.2f, 0.2f}, strata},
        {{"cliff_face", "Cliff face", "Rock", false, M::Raise, 12.0f, 2.0f, 0.2f}, cliffFace},
        {{"boulders", "Boulders", "Rock", false, M::Raise, 12.0f, 1.2f, 0.25f}, boulders},
        {{"fractured", "Fractured rock", "Rock", false, M::Raise, 12.0f, 0.8f, 0.2f}, fractured},
        {{"scree", "Scree", "Rock", false, M::Raise, 10.0f, 0.4f, 0.2f}, scree},
        {{"rock_ridge", "Rock ridge", "Rock", false, M::Raise, 14.0f, 1.5f, 0.2f}, rockRidge},
        {{"eroded_rock", "Eroded rock", "Rock", false, M::Raise, 12.0f, 1.2f, 0.2f}, erodedRock},

        {{"ripples", "Ripples", "Dunes", false, M::Raise, 10.0f, 0.25f, 0.15f}, ripples},
        {{"dune_crescent", "Crescent dune", "Dunes", false, M::Raise, 14.0f, 1.5f, 0.2f}, duneCrescent},
        {{"sand_waves", "Sand waves", "Dunes", false, M::Raise, 12.0f, 0.5f, 0.2f}, sandWaves},
        {{"wind_streaks", "Wind streaks", "Dunes", false, M::Raise, 12.0f, 0.3f, 0.15f}, windStreaks},

        {{"crater_simple", "Crater", "Craters", true, M::Raise, 12.0f, 2.0f, 0.5f}, craterSimple},
        {{"crater_complex", "Complex crater", "Craters", true, M::Raise, 16.0f, 3.0f, 0.5f}, craterComplex},
        {{"crater_old", "Weathered crater", "Craters", true, M::Raise, 14.0f, 2.0f, 0.5f}, craterOld},
        {{"impact_splash", "Impact splash", "Craters", true, M::Raise, 16.0f, 2.5f, 0.5f}, impactSplash},
        {{"pit", "Pit", "Craters", false, M::Lower, 6.0f, 1.0f, 0.2f}, pit},

        {{"terraces_soft", "Soft terraces", "Terraces", false, M::Raise, 14.0f, 2.0f, 0.25f}, terracesSoft},
        {{"terraces_hard", "Hard terraces", "Terraces", false, M::Raise, 14.0f, 2.0f, 0.25f}, terracesHard},
        {{"rice_paddies", "Rice paddies", "Terraces", false, M::Raise, 16.0f, 1.2f, 0.25f}, ricePaddies},

        {{"mound", "Mound", "Shapes", false, M::Raise, 10.0f, 1.0f, 0.2f}, mound},
        {{"hill_cluster", "Hill cluster", "Shapes", false, M::Raise, 24.0f, 6.0f, 0.4f}, hillCluster},
        {{"ridge_line", "Ridge line", "Shapes", false, M::Raise, 20.0f, 4.0f, 0.3f}, ridgeLine},
        {{"valley", "Valley", "Shapes", true, M::Raise, 20.0f, 4.0f, 0.3f}, valley},
        {{"volcano", "Volcano", "Shapes", false, M::Raise, 30.0f, 10.0f, 0.5f}, volcano},
        {{"mesa", "Mesa", "Shapes", false, M::Raise, 20.0f, 6.0f, 0.4f}, mesa},
        {{"river_bed", "River bed", "Shapes", true, M::Raise, 20.0f, 2.0f, 0.2f}, riverBed},
    };
    return entries;
}

const Entry* findEntry(const std::string& id) {
    for (const Entry& entry : table()) {
        if (id == entry.info.id) return &entry;
    }
    return nullptr;
}

}  // namespace

const std::vector<StampInfo>& proceduralStamps() {
    static const std::vector<StampInfo> infos = [] {
        std::vector<StampInfo> out;
        for (const Entry& entry : table()) out.push_back(entry.info);
        return out;
    }();
    return infos;
}

const StampInfo* findProceduralStamp(const std::string& id) {
    const Entry* entry = findEntry(id);
    return entry ? &entry->info : nullptr;
}

bool generateProceduralStamp(const std::string& id, u32 size, u64 seed, BrushStamp& out) {
    const Entry* entry = findEntry(id);
    if (!entry || size < 4 || size > 1024) return false;
    out.size = size;
    out.alpha.resize(static_cast<size_t>(size) * size);
    const f32 last = static_cast<f32>(size - 1);
    for (u32 z = 0; z < size; ++z) {
        for (u32 x = 0; x < size; ++x) {
            P p;
            p.u = static_cast<f32>(x) / last * 2.0f - 1.0f;
            p.v = static_cast<f32>(z) / last * 2.0f - 1.0f;
            p.r = std::sqrt(p.u * p.u + p.v * p.v);
            p.seed = seed;
            out.alpha[static_cast<size_t>(z) * size + x] = clamp01(entry->fn(p));
        }
    }
    return true;
}

}  // namespace Caffeine::Terrain
