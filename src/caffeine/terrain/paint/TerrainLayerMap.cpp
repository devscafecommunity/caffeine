#include "terrain/paint/TerrainLayerMap.hpp"

#include "core/io/Crc32.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Caffeine::Terrain {

bool LayerTexel::operator==(const LayerTexel& other) const {
    return std::memcmp(index, other.index, sizeof(index)) == 0 && std::memcmp(weight, other.weight, sizeof(weight)) == 0;
}

namespace {

constexpr u32 kMagic = 0x504D4C54u;  // "TLMP" little endian
constexpr u32 kFileVersion = 1;
constexpr usize kHeaderBytes = 32;

// An integer hash in 0..1: the dither of a rounding. Arithmetic only, so the same on every machine.
f32 hash01(u32 x, u32 z, u32 dither, u32 slot) {
    u64 h = 0x9E3779B97F4A7C15ull;
    h ^= static_cast<u64>(x) * 0xBF58476D1CE4E5B9ull;
    h ^= static_cast<u64>(z) * 0x94D049BB133111EBull + 0x632BE59BD9B4E019ull;
    h ^= static_cast<u64>(dither) * 0xD6E8FEB86659FD93ull;
    h ^= static_cast<u64>(slot) * 0xA0761D6478BD642Full;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return static_cast<f32>(h & 0xFFFFFFu) / 16777216.0f;
}

void put32(std::vector<u8>& out, u32 value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>((value >> (8 * i)) & 0xFF));
}
void put64(std::vector<u8>& out, u64 value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<u8>((value >> (8 * i)) & 0xFF));
}
u32 get32(const u8* p) { return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24); }
u64 get64(const u8* p) { return static_cast<u64>(get32(p)) | (static_cast<u64>(get32(p + 4)) << 32); }

}  // namespace

// The layers of one texel as floats. A texel names at most four, but an edit may add a fifth before the smallest goes.
struct TerrainLayerMap::Mix {
    static constexpr int kCap = 12;
    u32 layer[kCap];
    f32 weight[kCap];
    int count = 0;

    f32* find(u32 id) {
        for (int i = 0; i < count; ++i) {
            if (layer[i] == id) return &weight[i];
        }
        return nullptr;
    }
    f32 get(u32 id) const {
        for (int i = 0; i < count; ++i) {
            if (layer[i] == id) return weight[i];
        }
        return 0.0f;
    }
    void add(u32 id, f32 amount) {
        if (f32* w = find(id)) {
            *w += amount;
        } else if (count < kCap) {
            layer[count] = id;
            weight[count] = amount;
            ++count;
        }
    }
    static Mix of(const LayerTexel& texel) {
        Mix mix;
        for (u32 s = 0; s < kSlots; ++s) {
            if (texel.weight[s] > 0) mix.add(texel.index[s], static_cast<f32>(texel.weight[s]) / 255.0f);
        }
        if (mix.count == 0) mix.add(0, 1.0f);
        return mix;
    }
    f32 total() const {
        f32 sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += weight[i];
        return sum;
    }
};

TerrainLayerMap::TerrainLayerMap(u32 resolutionX, u32 resolutionZ) { resize(resolutionX, resolutionZ, false); }

void TerrainLayerMap::resize(u32 resolutionX, u32 resolutionZ, bool keep) {
    resolutionX = std::max(resolutionX, 1u);
    resolutionZ = std::max(resolutionZ, 1u);
    std::vector<LayerTexel> next(static_cast<usize>(resolutionX) * resolutionZ);
    if (keep && !m_texels.empty()) {
        for (u32 z = 0; z < resolutionZ; ++z) {
            const u32 sz = resolutionZ > 1 ? static_cast<u32>(static_cast<u64>(z) * (m_resolutionZ - 1) / std::max(resolutionZ - 1, 1u)) : 0;
            for (u32 x = 0; x < resolutionX; ++x) {
                const u32 sx = resolutionX > 1 ? static_cast<u32>(static_cast<u64>(x) * (m_resolutionX - 1) / std::max(resolutionX - 1, 1u)) : 0;
                next[static_cast<usize>(z) * resolutionX + x] = at(sx, sz);
            }
        }
    }
    m_texels = std::move(next);
    m_resolutionX = resolutionX;
    m_resolutionZ = resolutionZ;
}

void TerrainLayerMap::fill(u32 layer) {
    LayerTexel texel;
    texel.index[0] = static_cast<u8>(std::min(layer, kMaxLayers - 1));
    std::fill(m_texels.begin(), m_texels.end(), texel);
}

f32 TerrainLayerMap::weightOf(u32 x, u32 z, u32 layer) const {
    const LayerTexel& texel = at(x, z);
    f32 total = 0.0f;
    for (u32 s = 0; s < kSlots; ++s) {
        if (texel.weight[s] > 0 && texel.index[s] == layer) total += static_cast<f32>(texel.weight[s]) / 255.0f;
    }
    return total;
}

u32 TerrainLayerMap::dominant(u32 x, u32 z) const {
    const LayerTexel& texel = at(x, z);
    u32 best = 0;
    for (u32 s = 1; s < kSlots; ++s) {
        if (texel.weight[s] > texel.weight[best]) best = s;
    }
    return texel.index[best];
}

void TerrainLayerMap::setLocked(u32 layer, bool on) {
    if (layer >= kMaxLayers) return;
    if (on) {
        m_lockMask |= (1ull << layer);
    } else {
        m_lockMask &= ~(1ull << layer);
    }
}

// Keeps the four heaviest layers, rescales them to add up to one and writes them as bytes that add up to 255.
LayerTexel TerrainLayerMap::pack(u32 x, u32 z, Mix& mix, u32 dither) const {
    // Heaviest first; a tie goes to the lower layer so the result does not depend on the order edits arrived in.
    int order[Mix::kCap];
    for (int i = 0; i < mix.count; ++i) order[i] = i;
    std::sort(order, order + mix.count, [&](int a, int b) {
        if (mix.weight[a] != mix.weight[b]) return mix.weight[a] > mix.weight[b];
        return mix.layer[a] < mix.layer[b];
    });
    const int kept = std::min<int>(mix.count, kSlots);
    f32 sum = 0.0f;
    for (int i = 0; i < kept; ++i) sum += std::max(mix.weight[order[i]], 0.0f);
    LayerTexel texel;
    std::memset(texel.weight, 0, sizeof(texel.weight));
    if (sum <= 1e-9f) {
        texel.index[0] = 0;
        texel.weight[0] = 255;
        return texel;
    }
    int total = 0;
    int heaviest = 0;
    for (int i = 0; i < kept; ++i) {
        const int src = order[i];
        const f32 share = std::max(mix.weight[src], 0.0f) / sum * 255.0f;
        // Stochastic rounding: the fraction decides, by a hash, whether the byte rounds up.
        int bytes = static_cast<int>(std::floor(share + hash01(x, z, dither, static_cast<u32>(mix.layer[src]))));
        bytes = std::clamp(bytes, 0, 255);
        texel.index[i] = static_cast<u8>(mix.layer[src]);
        texel.weight[i] = static_cast<u8>(bytes);
        total += bytes;
        if (bytes > texel.weight[heaviest]) heaviest = i;
    }
    // The bytes must add up to 255: the difference goes to the heaviest layer.
    texel.weight[heaviest] = static_cast<u8>(std::clamp(static_cast<int>(texel.weight[heaviest]) + (255 - total), 0, 255));
    // A slot with no weight keeps the index of a layer that is there, so the texel never names a layer for nothing.
    for (u32 s = 0; s < kSlots; ++s) {
        if (texel.weight[s] == 0) texel.index[s] = texel.index[0];
    }
    return texel;
}

void TerrainLayerMap::blendToward(u32 x, u32 z, u32 layer, f32 amount, u32 dither) {
    if (layer >= kMaxLayers || locked(layer)) return;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return;
    Mix mix = Mix::of(at(x, z));
    const f32 target = mix.get(layer);
    f32 unlocked = 0.0f;  // what can give way: the weight of the other, unlocked layers
    for (int i = 0; i < mix.count; ++i) {
        if (mix.layer[i] != layer && !locked(mix.layer[i])) unlocked += mix.weight[i];
    }
    if (unlocked <= 0.0f) return;
    const f32 delta = std::min(amount * (1.0f - target), unlocked);
    const f32 keep = (unlocked - delta) / unlocked;
    for (int i = 0; i < mix.count; ++i) {
        if (mix.layer[i] != layer && !locked(mix.layer[i])) mix.weight[i] *= keep;
    }
    mix.add(layer, delta);
    store(x, z, mix, dither);
}

void TerrainLayerMap::eraseLayer(u32 x, u32 z, u32 layer, f32 amount, u32 dither) {
    if (layer >= kMaxLayers || locked(layer)) return;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return;
    Mix mix = Mix::of(at(x, z));
    const f32 own = mix.get(layer);
    if (own <= 0.0f) return;
    f32 others = 0.0f;
    for (int i = 0; i < mix.count; ++i) {
        if (mix.layer[i] != layer && !locked(mix.layer[i])) others += mix.weight[i];
    }
    if (others <= 0.0f) return;  // all `layer` (or the rest is locked): there is nothing to give the weight to
    const f32 freed = own * amount;
    for (int i = 0; i < mix.count; ++i) {
        if (mix.layer[i] == layer) {
            mix.weight[i] -= freed;
        } else if (!locked(mix.layer[i])) {
            mix.weight[i] += freed * mix.weight[i] / others;
        }
    }
    store(x, z, mix, dither);
}

void TerrainLayerMap::replaceLayer(u32 x, u32 z, u32 from, u32 to, f32 amount, u32 dither) {
    if (from >= kMaxLayers || to >= kMaxLayers || from == to || locked(from) || locked(to)) return;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return;
    Mix mix = Mix::of(at(x, z));
    const f32 own = mix.get(from);
    if (own <= 0.0f) return;
    const f32 moved = own * amount;
    mix.add(from, -moved);
    mix.add(to, moved);
    store(x, z, mix, dither);
}

void TerrainLayerMap::smoothTexel(u32 x, u32 z, f32 amount, u32 dither) { setRaw(x, z, smoothed(x, z, amount, dither)); }

LayerTexel TerrainLayerMap::smoothed(u32 x, u32 z, f32 amount, u32 dither) const {
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return at(x, z);
    Mix own = Mix::of(at(x, z));
    Mix average;
    int cells = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const i64 nx = static_cast<i64>(x) + dx, nz = static_cast<i64>(z) + dz;
            if (nx < 0 || nz < 0 || nx >= static_cast<i64>(m_resolutionX) || nz >= static_cast<i64>(m_resolutionZ)) continue;
            const Mix neighbour = Mix::of(at(static_cast<u32>(nx), static_cast<u32>(nz)));
            for (int i = 0; i < neighbour.count; ++i) average.add(neighbour.layer[i], neighbour.weight[i]);
            ++cells;
        }
    }
    // The average and the texel's own layers, mixed. A locked layer keeps its own weight exactly.
    Mix result;
    for (int i = 0; i < average.count; ++i) {
        const u32 id = average.layer[i];
        const f32 mean = average.weight[i] / static_cast<f32>(cells);
        result.add(id, locked(id) ? own.get(id) : own.get(id) + (mean - own.get(id)) * amount);
    }
    for (int i = 0; i < own.count; ++i) {
        if (result.find(own.layer[i]) == nullptr) {
            result.add(own.layer[i], locked(own.layer[i]) ? own.weight[i] : own.weight[i] * (1.0f - amount));
        }
    }
    // Locked layers did not move, so the rest must give or take what keeps the total at one.
    f32 lockedSum = 0.0f, free = 0.0f;
    for (int i = 0; i < result.count; ++i) (locked(result.layer[i]) ? lockedSum : free) += result.weight[i];
    if (free > 0.0f) {
        const f32 scale = std::max(1.0f - lockedSum, 0.0f) / free;
        for (int i = 0; i < result.count; ++i) {
            if (!locked(result.layer[i])) result.weight[i] *= scale;
        }
    }
    return pack(x, z, result, dither);
}

void TerrainLayerMap::assign(u32 x, u32 z, const std::vector<std::pair<u32, f32>>& weights, u32 dither) {
    Mix own = Mix::of(at(x, z));
    Mix result;
    f32 lockedSum = 0.0f;
    for (int i = 0; i < own.count; ++i) {
        if (locked(own.layer[i])) {
            result.add(own.layer[i], own.weight[i]);
            lockedSum += own.weight[i];
        }
    }
    f32 given = 0.0f;
    for (const auto& entry : weights) {
        if (entry.first < kMaxLayers && !locked(entry.first) && entry.second > 0.0f) given += entry.second;
    }
    if (given <= 0.0f) return;  // nothing to give the free share to: the texel keeps what it has
    const f32 share = std::max(1.0f - lockedSum, 0.0f) / given;
    for (const auto& entry : weights) {
        if (entry.first < kMaxLayers && !locked(entry.first) && entry.second > 0.0f) result.add(entry.first, entry.second * share);
    }
    store(x, z, result, dither);
}

void TerrainLayerMap::removeLayer(u32 index) {
    if (index >= kMaxLayers) return;
    for (u32 z = 0; z < m_resolutionZ; ++z) {
        for (u32 x = 0; x < m_resolutionX; ++x) {
            Mix mix = Mix::of(at(x, z));
            f32 gone = mix.get(index);
            Mix next;
            for (int i = 0; i < mix.count; ++i) {
                if (mix.layer[i] == index) continue;
                next.add(mix.layer[i] > index ? mix.layer[i] - 1 : mix.layer[i], mix.weight[i]);
            }
            if (next.count == 0) {
                next.add(0, 1.0f);  // it stood alone: the ground falls back to the first layer
            } else if (gone > 0.0f) {
                const f32 rest = next.total();
                for (int i = 0; i < next.count; ++i) next.weight[i] += gone * next.weight[i] / std::max(rest, 1e-9f);
            }
            store(x, z, next, 0);
        }
    }
    const u64 below = index == 0 ? 0ull : (m_lockMask & ((1ull << index) - 1ull));
    const u64 above = index + 1 >= 64 ? 0ull : (m_lockMask >> (index + 1)) << index;
    m_lockMask = below | above;
}

void TerrainLayerMap::moveLayer(u32 from, u32 to) {
    if (from >= kMaxLayers || to >= kMaxLayers || from == to) return;
    auto renumber = [&](u32 id) -> u32 {
        if (id == from) return to;
        if (from < to && id > from && id <= to) return id - 1;
        if (from > to && id >= to && id < from) return id + 1;
        return id;
    };
    for (LayerTexel& texel : m_texels) {
        for (u32 s = 0; s < kSlots; ++s) texel.index[s] = static_cast<u8>(renumber(texel.index[s]));
    }
    u64 mask = 0;
    for (u32 layer = 0; layer < kMaxLayers; ++layer) {
        if ((m_lockMask >> layer) & 1ull) mask |= 1ull << renumber(layer);
    }
    m_lockMask = mask;
}

TerrainLayerMap TerrainLayerMap::fromSplatmap(const TerrainSplatmap& splat) {
    TerrainLayerMap map(splat.resolutionX(), splat.resolutionZ());
    if (splat.empty()) return map;
    for (u32 z = 0; z < map.m_resolutionZ; ++z) {
        for (u32 x = 0; x < map.m_resolutionX; ++x) {
            const Vec4 w = splat.sample(x, z);
            Mix mix;
            const f32 values[4] = {w.x, w.y, w.z, w.w};
            for (u32 i = 0; i < 4; ++i) {
                if (values[i] > 0.0f) mix.add(i, values[i]);
            }
            if (mix.count == 0) mix.add(0, 1.0f);
            map.store(x, z, mix, 0);
        }
    }
    return map;
}

TerrainSplatmap TerrainLayerMap::toSplatmap() const {
    TerrainSplatmap splat(m_resolutionX, m_resolutionZ);
    for (u32 z = 0; z < m_resolutionZ; ++z) {
        for (u32 x = 0; x < m_resolutionX; ++x) {
            const LayerTexel& texel = at(x, z);
            f32 channel[4] = {0, 0, 0, 0};
            f32 sum = 0.0f;
            for (u32 s = 0; s < kSlots; ++s) {
                if (texel.weight[s] > 0 && texel.index[s] < 4) {
                    channel[texel.index[s]] += static_cast<f32>(texel.weight[s]) / 255.0f;
                    sum += static_cast<f32>(texel.weight[s]) / 255.0f;
                }
            }
            if (sum <= 0.0f) {
                channel[0] = 1.0f;  // only layers the old renderer cannot show: draw the first
            } else {
                for (f32& c : channel) c /= sum;
            }
            splat.set(x, z, Vec4{channel[0], channel[1], channel[2], channel[3]});
        }
    }
    return splat;
}

std::vector<u32> TerrainLayerMap::usage(u32 layerCount) const {
    std::vector<u32> counts(layerCount, 0);
    for (const LayerTexel& texel : m_texels) {
        for (u32 s = 0; s < kSlots; ++s) {
            if (texel.weight[s] > 0 && texel.index[s] < layerCount) ++counts[texel.index[s]];
        }
    }
    return counts;
}

std::vector<u8> TerrainLayerMap::toBytes() const {
    std::vector<u8> out;
    out.reserve(kHeaderBytes + bytes() + 4);
    put32(out, kMagic);
    put32(out, kFileVersion);
    put32(out, m_resolutionX);
    put32(out, m_resolutionZ);
    put64(out, m_lockMask);
    put32(out, 0);
    put32(out, 0);
    for (const LayerTexel& texel : m_texels) {
        out.insert(out.end(), texel.index, texel.index + 4);
        out.insert(out.end(), texel.weight, texel.weight + 4);
    }
    put32(out, IO::crc32(out.data(), out.size()));
    return out;
}

bool TerrainLayerMap::fromBytes(const u8* data, usize size, TerrainLayerMap& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (size < kHeaderBytes + 4) return fail("the file is too short to be a layer map");
    if (get32(data) != kMagic) return fail("not a layer map (the signature is wrong)");
    if (get32(data + 4) > kFileVersion) return fail("the layer map is from a newer version");
    const u32 rx = get32(data + 8), rz = get32(data + 12);
    if (rx == 0 || rz == 0 || static_cast<u64>(rx) * rz > (1ull << 30)) return fail("the layer map has an impossible size");
    const u64 expected = kHeaderBytes + static_cast<u64>(rx) * rz * sizeof(LayerTexel) + 4;
    if (size != expected) return fail("the layer map is truncated or has extra bytes");
    if (IO::crc32(data, size - 4) != get32(data + size - 4)) return fail("the layer map is damaged (the checksum does not match)");
    TerrainLayerMap map;
    map.m_resolutionX = rx;
    map.m_resolutionZ = rz;
    map.m_lockMask = get64(data + 16);
    map.m_texels.resize(static_cast<usize>(rx) * rz);
    const u8* cursor = data + kHeaderBytes;
    for (LayerTexel& texel : map.m_texels) {
        std::memcpy(texel.index, cursor, 4);
        std::memcpy(texel.weight, cursor + 4, 4);
        cursor += 8;
    }
    out = std::move(map);
    return true;
}

bool TerrainLayerMap::save(const std::string& path, std::string* error) const {
    const std::vector<u8> bytes = toBytes();
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) *error = "could not open " + path + " for writing";
        return false;
    }
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) {
        if (error) *error = "could not write " + path;
        return false;
    }
    return true;
}

bool TerrainLayerMap::load(const std::string& path, TerrainLayerMap& out, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (error) *error = "could not open " + path;
        return false;
    }
    std::vector<u8> bytes;
    u8 buffer[65536];
    usize read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.insert(bytes.end(), buffer, buffer + read);
    std::fclose(file);
    return fromBytes(bytes.data(), bytes.size(), out, error);
}

bool TerrainLayerMap::operator==(const TerrainLayerMap& other) const {
    return m_resolutionX == other.m_resolutionX && m_resolutionZ == other.m_resolutionZ && m_lockMask == other.m_lockMask &&
           m_texels == other.m_texels;
}

}  // namespace Caffeine::Terrain
