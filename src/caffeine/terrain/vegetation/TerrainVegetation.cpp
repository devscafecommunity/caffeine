#include "terrain/vegetation/TerrainVegetation.hpp"

#include "core/Hash.hpp"
#include "core/io/Crc32.hpp"
#include "scatter/Scatter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Caffeine::Terrain {
namespace {

constexpr u32 kMagic = 0x47455654u;  // "TVEG"
constexpr u32 kFileVersion = 1;
constexpr u32 kSaltManualId = 1;
constexpr u32 kSaltProceduralId = 2;

i32 centimetres(f32 metres) { return static_cast<i32>(std::lround(static_cast<f64>(metres) * 100.0)); }

void put32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>((v >> (8 * i)) & 0xFF));
}
void put64(std::vector<u8>& out, u64 v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<u8>((v >> (8 * i)) & 0xFF));
}
void putF(std::vector<u8>& out, f32 v) {
    u32 bits;
    std::memcpy(&bits, &v, 4);
    put32(out, bits);
}
struct Reader {
    const u8* p;
    const u8* end;
    bool ok = true;
    bool need(usize n) {
        if (static_cast<usize>(end - p) < n) ok = false;
        return ok;
    }
    u32 u32v() {
        if (!need(4)) return 0;
        const u32 v = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
        p += 4;
        return v;
    }
    u64 u64v() {
        const u64 lo = u32v();
        const u64 hi = u32v();
        return lo | (hi << 32);
    }
    f32 f32v() {
        const u32 bits = u32v();
        f32 v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    u8 u8v() {
        if (!need(1)) return 0;
        return *p++;
    }
};

}  // namespace

bool VegetationInstance::operator==(const VegetationInstance& o) const {
    return id == o.id && x == o.x && z == o.z && species == o.species && variant == o.variant && flags == o.flags && scale == o.scale &&
           yaw == o.yaw && seed == o.seed && tint[0] == o.tint[0] && tint[1] == o.tint[1] && tint[2] == o.tint[2];
}

bool VegetationOverride::operator==(const VegetationOverride& o) const {
    return removed == o.removed && scaleMul == o.scaleMul && yawAdd == o.yawAdd && hasTint == o.hasTint && tint[0] == o.tint[0] &&
           tint[1] == o.tint[1] && tint[2] == o.tint[2] && dx == o.dx && dz == o.dz;
}

// ---------------------------------------------------------------------------------------------------------- species

i32 TerrainVegetation::addSpecies(VegetationSpecies species) {
    if (m_species.size() >= kMaxVegetationSpecies) return -1;
    if (species.uid == 0) species.uid = m_nextUid;
    // A uid in use is not reused: it would make two species share instance ids.
    while (indexOfUid(species.uid) >= 0) ++species.uid;
    m_nextUid = std::max(m_nextUid, species.uid + 1);
    m_species.push_back(std::move(species));
    touch();
    return static_cast<i32>(m_species.size() - 1);
}

bool TerrainVegetation::setSpecies(u32 index, const VegetationSpecies& species) {
    if (index >= m_species.size()) return false;
    VegetationSpecies copy = species;
    copy.uid = m_species[index].uid;
    if (copy == m_species[index]) return true;
    m_species[index] = std::move(copy);
    touch();
    return true;
}

bool TerrainVegetation::removeSpecies(u32 index) {
    if (index >= m_species.size()) return false;
    const u32 uid = m_species[index].uid;
    // Its manual instances go with it.
    for (auto& entry : m_chunks) {
        auto& list = entry.second;
        for (usize i = 0; i < list.size();) {
            if (list[i].species == uid) {
                m_where.erase(list[i].id);
                list[i] = list.back();
                list.pop_back();
                --m_manualCount;
            } else {
                ++i;
            }
        }
    }
    m_species.erase(m_species.begin() + index);
    touch();
    return true;
}

i32 TerrainVegetation::indexOfUid(u32 uid) const {
    for (usize i = 0; i < m_species.size(); ++i) {
        if (m_species[i].uid == uid) return static_cast<i32>(i);
    }
    return -1;
}

const VegetationSpecies* TerrainVegetation::speciesByUid(u32 uid) const {
    const i32 index = indexOfUid(uid);
    return index >= 0 ? &m_species[static_cast<usize>(index)] : nullptr;
}

void TerrainVegetation::setAllSpecies(std::vector<VegetationSpecies> species) {
    m_species = std::move(species);
    for (const VegetationSpecies& s : m_species) m_nextUid = std::max(m_nextUid, s.uid + 1);
    // Manual instances of species that are gone are dropped.
    for (auto& entry : m_chunks) {
        auto& list = entry.second;
        for (usize i = 0; i < list.size();) {
            if (indexOfUid(list[i].species) < 0) {
                m_where.erase(list[i].id);
                list[i] = list.back();
                list.pop_back();
                --m_manualCount;
            } else {
                ++i;
            }
        }
    }
    touch();
}

void TerrainVegetation::setSeed(u64 seed) {
    if (seed == m_seed) return;
    m_seed = seed;
    touch();
}

void TerrainVegetation::setProceduralEnabled(bool on) {
    if (on == m_procedural) return;
    m_procedural = on;
    touch();
}

// ----------------------------------------------------------------------------------------------------------- manual

i32 TerrainVegetation::chunkOf(f32 metres) { return static_cast<i32>(std::floor(metres / kChunkM)); }

const VegetationInstance* TerrainVegetation::findManual(u64 id) const {
    const auto where = m_where.find(id);
    if (where == m_where.end()) return nullptr;
    const auto chunk = m_chunks.find(where->second);
    if (chunk == m_chunks.end()) return nullptr;
    for (const VegetationInstance& i : chunk->second) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

bool TerrainVegetation::addManual(const VegetationInstance& instance) {
    if (m_where.count(instance.id)) return false;
    const u64 key = chunkKey(chunkOf(instance.x), chunkOf(instance.z));
    m_chunks[key].push_back(instance);
    m_where[instance.id] = key;
    ++m_manualCount;
    touch();
    return true;
}

bool TerrainVegetation::removeManual(u64 id, VegetationInstance* removed) {
    const auto where = m_where.find(id);
    if (where == m_where.end()) return false;
    auto& list = m_chunks[where->second];
    for (usize i = 0; i < list.size(); ++i) {
        if (list[i].id != id) continue;
        if (removed) *removed = list[i];
        list[i] = list.back();
        list.pop_back();
        break;
    }
    m_where.erase(where);
    --m_manualCount;
    touch();
    return true;
}

bool TerrainVegetation::replaceManual(const VegetationInstance& instance) {
    if (!removeManual(instance.id)) return false;
    return addManual(instance);
}

u64 TerrainVegetation::newManualId(f32 x, f32 z) {
    for (;;) {
        const u64 id = Core::deriveSeed(Core::hashCombine(kSaltManualId, m_idCounter++), kSaltManualId, centimetres(x), centimetres(z)) | 1ull;
        if (!m_where.count(id)) return id;
    }
}

std::vector<VegetationInstance> TerrainVegetation::allManual() const {
    std::vector<VegetationInstance> all;
    all.reserve(m_manualCount);
    for (const auto& entry : m_chunks) all.insert(all.end(), entry.second.begin(), entry.second.end());
    // A canonical order, so saves and tests do not depend on the hash map.
    std::sort(all.begin(), all.end(), [](const VegetationInstance& a, const VegetationInstance& b) { return a.id < b.id; });
    return all;
}

const VegetationOverride* TerrainVegetation::findOverride(u64 id) const {
    const auto it = m_overrides.find(id);
    return it != m_overrides.end() ? &it->second : nullptr;
}

void TerrainVegetation::setOverride(u64 id, const VegetationOverride& override) {
    if (override.isIdentity()) {
        m_overrides.erase(id);
    } else {
        m_overrides[id] = override;
    }
    touch();
}

std::vector<std::pair<u64, VegetationOverride>> TerrainVegetation::allOverrides() const {
    std::vector<std::pair<u64, VegetationOverride>> all(m_overrides.begin(), m_overrides.end());
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return all;
}

u64 TerrainVegetation::proceduralId(u32 uid, f32 x, f32 z) {
    return Core::deriveSeed(Core::hashCombine(kSaltProceduralId, uid), kSaltProceduralId, centimetres(x), centimetres(z)) & ~1ull;
}

// ---------------------------------------------------------------------------------------------------------- resolve

VegetationResolved TerrainVegetation::place(const VegetationSpecies& species, u32 index, u64 id, u8 variant, bool manual, f32 x, f32 z,
                                            f32 scale, f32 yaw, const f32* tint, const VegetationField& field) const {
    VegetationResolved r;
    r.id = id;
    r.species = index;
    r.variant = variant;
    r.manual = manual;
    r.x = x;
    r.z = z;
    r.y = (field.height ? field.height(x, z) : 0.0f) - species.sinkM * scale;
    r.scale = scale;
    r.yaw = yaw;
    Vec3 up(0.0f, 1.0f, 0.0f);
    if (field.normal && species.tilt > 0.0f) {
        const Vec3 n = field.normal(x, z);
        up = Vec3(n.x * species.tilt, 1.0f - species.tilt + n.y * species.tilt, n.z * species.tilt);
        const f32 length = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
        if (length > 1e-6f) up = Vec3(up.x / length, up.y / length, up.z / length);
    }
    r.up[0] = up.x;
    r.up[1] = up.y;
    r.up[2] = up.z;
    for (int c = 0; c < 3; ++c) r.tint[c] = tint[c] * species.tint[c];
    return r;
}

void TerrainVegetation::resolveChunk(i32 cx, i32 cz, const VegetationField& field, std::vector<VegetationResolved>& out) const {
    const usize firstNew = out.size();
    const f32 minX = static_cast<f32>(cx) * kChunkM, minZ = static_cast<f32>(cz) * kChunkM;

    // The manual instances near this chunk (a margin of the largest conflict distance), for conflicts with procedural ones.
    f32 widest = 0.0f;
    for (const VegetationSpecies& s : m_species) widest = std::max(widest, s.radius);
    const i32 margin = static_cast<i32>(std::ceil(widest * 2.0f / kChunkM));
    std::vector<VegetationResolved> nearManual;
    for (i32 dz = -margin; dz <= margin; ++dz) {
        for (i32 dx = -margin; dx <= margin; ++dx) {
            const auto chunk = m_chunks.find(chunkKey(cx + dx, cz + dz));
            if (chunk == m_chunks.end()) continue;
            for (const VegetationInstance& inst : chunk->second) {
                const i32 index = indexOfUid(inst.species);
                if (index < 0) continue;
                const VegetationSpecies& s = m_species[static_cast<usize>(index)];
                if (!s.visible || s.meshes.empty()) continue;
                f32 x = inst.x, z = inst.z, scale = inst.scale, yaw = inst.yaw;
                f32 tint[3] = {inst.tint[0] / 255.0f, inst.tint[1] / 255.0f, inst.tint[2] / 255.0f};
                if (const VegetationOverride* o = findOverride(inst.id)) {
                    if (o->removed) continue;
                    x += o->dx;
                    z += o->dz;
                    scale *= o->scaleMul;
                    yaw += o->yawAdd;
                    if (o->hasTint) {
                        for (int c = 0; c < 3; ++c) tint[c] = o->tint[c] / 255.0f;
                    }
                }
                // A moved or hidden-by-rules instance: the rules are read where it stands now, unless it was forced.
                if (!(inst.flags & kVegForced) && field.density && field.density(static_cast<u32>(index), x, z) <= 0.0f) continue;
                nearManual.push_back(place(s, static_cast<u32>(index), inst.id, inst.variant, true, x, z, scale, yaw, tint, field));
            }
        }
    }
    for (const VegetationResolved& m : nearManual) {
        if (chunkOf(m.x) == cx && chunkOf(m.z) == cz) out.push_back(m);
    }

    // The procedural source.
    if (m_procedural) {
        std::vector<Scatter::ScatterClass> classes;
        for (const VegetationSpecies& s : m_species) {
            if (!s.procedural || !s.visible || s.meshes.empty() || s.densityPerHa <= 0.0f) continue;
            Scatter::ScatterClass c;
            c.id = s.uid;
            c.priority = s.priority;
            c.cellSize = std::sqrt(10000.0f / s.densityPerHa);
            c.radius = s.radius;
            c.density = 1.0f;
            c.minScale = s.minScale;
            c.maxScale = s.maxScale;
            c.tiltToNormal = s.tilt;
            c.sink = s.sinkM;
            c.tintJitter = s.tintJitter;
            c.variants = s.variantCount();
            classes.push_back(c);
        }
        if (!classes.empty()) {
            Scatter::ScatterField scatterField;
            scatterField.height = field.height ? field.height : [](f32, f32) { return 0.0f; };
            scatterField.normal = field.normal ? field.normal : [](f32, f32) { return Vec3(0.0f, 1.0f, 0.0f); };
            scatterField.density = [&](u32 uid, f32 x, f32 z) {
                const i32 index = indexOfUid(uid);
                return index < 0 || !field.density ? 1.0f : field.density(static_cast<u32>(index), x, z);
            };
            for (const Scatter::Instance& inst : Scatter::scatterChunk(m_seed, cx, cz, kChunkM, classes, scatterField)) {
                const i32 index = indexOfUid(inst.classId);
                if (index < 0) continue;
                const VegetationSpecies& s = m_species[static_cast<usize>(index)];
                const u64 id = proceduralId(s.uid, inst.position.x, inst.position.z);
                f32 x = inst.position.x, z = inst.position.z, scale = inst.scale, yaw = inst.yaw;
                f32 tint[3] = {inst.tint.x, inst.tint.y, inst.tint.z};
                if (const VegetationOverride* o = findOverride(id)) {
                    if (o->removed) continue;
                    x += o->dx;
                    z += o->dz;
                    scale *= o->scaleMul;
                    yaw += o->yawAdd;
                    if (o->hasTint) {
                        for (int c = 0; c < 3; ++c) tint[c] = o->tint[c] / 255.0f;
                    }
                }
                // Nothing overlaps: a manual instance of at least this priority within the two radii wins the spot.
                bool beaten = false;
                for (const VegetationResolved& m : nearManual) {
                    const VegetationSpecies& other = m_species[m.species];
                    if (other.priority < s.priority) continue;
                    const f32 reach = other.radius * m.scale + s.radius * scale;
                    const f32 dx = m.x - x, dz = m.z - z;
                    if (dx * dx + dz * dz < reach * reach) {
                        beaten = true;
                        break;
                    }
                }
                if (beaten) continue;
                VegetationResolved r = place(s, static_cast<u32>(index), id, static_cast<u8>(std::min<u32>(inst.variant, 255u)), false, x, z, scale, yaw, tint, field);
                out.push_back(r);
            }
        }
    }

    // Nothing stands off the terrain (the procedural source does not know where the terrain ends).
    if (field.halfX > 0.0f || field.halfZ > 0.0f) {
        out.erase(std::remove_if(out.begin() + static_cast<std::ptrdiff_t>(firstNew), out.end(),
                                 [&](const VegetationResolved& r) {
                                     return (field.halfX > 0.0f && std::fabs(r.x) > field.halfX) || (field.halfZ > 0.0f && std::fabs(r.z) > field.halfZ);
                                 }),
                  out.end());
    }

    std::sort(out.begin() + static_cast<std::ptrdiff_t>(firstNew), out.end(), [](const VegetationResolved& a, const VegetationResolved& b) {
        if (a.species != b.species) return a.species < b.species;
        if (a.variant != b.variant) return a.variant < b.variant;
        return a.id < b.id;
    });
}

void TerrainVegetation::resolveArea(f32 x, f32 z, f32 radius, const VegetationField& field, std::vector<VegetationResolved>& out) const {
    const i32 x0 = chunkOf(x - radius), x1 = chunkOf(x + radius), z0 = chunkOf(z - radius), z1 = chunkOf(z + radius);
    std::vector<VegetationResolved> chunk;
    for (i32 cz = z0; cz <= z1; ++cz) {
        for (i32 cx = x0; cx <= x1; ++cx) {
            chunk.clear();
            resolveChunk(cx, cz, field, chunk);
            for (const VegetationResolved& r : chunk) {
                const f32 dx = r.x - x, dz = r.z - z;
                if (dx * dx + dz * dz <= radius * radius) out.push_back(r);
            }
        }
    }
}

// -------------------------------------------------------------------------------------------------------------- file

std::vector<u8> TerrainVegetation::toBytes() const {
    std::vector<u8> out;
    put32(out, kMagic);
    put32(out, kFileVersion);
    put64(out, m_seed);
    put32(out, m_procedural ? 1u : 0u);
    put32(out, m_nextUid);
    const std::string text = vegetationSpeciesToText(m_species);
    put32(out, static_cast<u32>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    const std::vector<VegetationInstance> manual = allManual();
    put32(out, static_cast<u32>(manual.size()));
    for (const VegetationInstance& i : manual) {
        put64(out, i.id);
        putF(out, i.x);
        putF(out, i.z);
        put32(out, i.species);
        out.push_back(i.variant);
        out.push_back(i.flags);
        putF(out, i.scale);
        putF(out, i.yaw);
        put32(out, i.seed);
        out.insert(out.end(), i.tint, i.tint + 3);
    }
    const auto overrides = allOverrides();
    put32(out, static_cast<u32>(overrides.size()));
    for (const auto& entry : overrides) {
        const VegetationOverride& o = entry.second;
        put64(out, entry.first);
        out.push_back(static_cast<u8>((o.removed ? 1 : 0) | (o.hasTint ? 2 : 0)));
        putF(out, o.scaleMul);
        putF(out, o.yawAdd);
        putF(out, o.dx);
        putF(out, o.dz);
        out.insert(out.end(), o.tint, o.tint + 3);
    }
    put32(out, IO::crc32(out.data(), out.size()));
    return out;
}

bool TerrainVegetation::fromBytes(const u8* data, usize size, TerrainVegetation& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (size < 12) return fail("the file is too short to be a vegetation file");
    Reader head{data, data + size};
    if (head.u32v() != kMagic) return fail("not a vegetation file (the signature is wrong)");
    if (head.u32v() > kFileVersion) return fail("the vegetation file is from a newer version");
    const u32 stored = static_cast<u32>(data[size - 4]) | (static_cast<u32>(data[size - 3]) << 8) | (static_cast<u32>(data[size - 2]) << 16) |
                       (static_cast<u32>(data[size - 1]) << 24);
    if (IO::crc32(data, size - 4) != stored) return fail("the vegetation file is damaged (the checksum does not match)");
    Reader r{data + 8, data + size - 4};
    TerrainVegetation veg;
    veg.m_seed = r.u64v();
    veg.m_procedural = r.u32v() != 0;
    veg.m_nextUid = std::max(r.u32v(), 1u);
    const u32 textSize = r.u32v();
    if (!r.need(textSize)) return fail("the vegetation file is truncated");
    const std::string text(reinterpret_cast<const char*>(r.p), textSize);
    r.p += textSize;
    std::string reason;
    if (!vegetationSpeciesFromText(text, veg.m_species, &reason)) {
        if (error) *error = "the species list: " + reason;
        return false;
    }
    for (const VegetationSpecies& s : veg.m_species) veg.m_nextUid = std::max(veg.m_nextUid, s.uid + 1);
    const u32 manualCount = r.u32v();
    if (!r.ok || static_cast<u64>(manualCount) * 37ull > static_cast<u64>(r.end - r.p)) return fail("the vegetation file is truncated");
    for (u32 n = 0; n < manualCount; ++n) {
        VegetationInstance i;
        i.id = r.u64v();
        i.x = r.f32v();
        i.z = r.f32v();
        i.species = r.u32v();
        i.variant = r.u8v();
        i.flags = r.u8v();
        i.scale = r.f32v();
        i.yaw = r.f32v();
        i.seed = r.u32v();
        for (u8& t : i.tint) t = r.u8v();
        if (!r.ok) return fail("the vegetation file is truncated");
        veg.addManual(i);
    }
    const u32 overrideCount = r.u32v();
    for (u32 n = 0; n < overrideCount && r.ok; ++n) {
        VegetationOverride o;
        const u64 id = r.u64v();
        const u8 flags = r.u8v();
        o.removed = (flags & 1) != 0;
        o.hasTint = (flags & 2) != 0;
        o.scaleMul = r.f32v();
        o.yawAdd = r.f32v();
        o.dx = r.f32v();
        o.dz = r.f32v();
        for (u8& t : o.tint) t = r.u8v();
        if (r.ok) veg.setOverride(id, o);
    }
    if (!r.ok || r.p != r.end) return fail("the vegetation file is truncated or has extra bytes");
    veg.m_revision = 1;
    out = std::move(veg);
    return true;
}

bool TerrainVegetation::save(const std::string& path, std::string* error) const {
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

bool TerrainVegetation::load(const std::string& path, TerrainVegetation& out, std::string* error) {
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

}  // namespace Caffeine::Terrain
