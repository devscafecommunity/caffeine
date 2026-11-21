#include "scatter/Scatter.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Scatter {
namespace {

struct Candidate {
    const ScatterClass* cls = nullptr;
    i32 cellX = 0;
    i32 cellZ = 0;
    f32 x = 0.0f;
    f32 z = 0.0f;
    /// Decides conflicts: priority first, then a hash rank that is the same for everybody.
    u64 key = 0;
};

constexpr u32 kSaltJitterX = 1;
constexpr u32 kSaltJitterZ = 2;
constexpr u32 kSaltAccept = 3;
constexpr u32 kSaltRank = 4;
constexpr u32 kSaltScale = 5;
constexpr u32 kSaltYaw = 6;
constexpr u32 kSaltVariant = 7;
constexpr u32 kSaltTint = 8;
constexpr u32 kSaltInstance = 9;

u64 classSeed(u64 worldSeed, const ScatterClass& cls) {
    return Core::hashCombine(worldSeed, cls.id);
}

/// Every accepted-by-density candidate of `cls` whose point lies in [minX,maxX) x [minZ,maxZ).
void collect(u64 worldSeed, const ScatterClass& cls, const ScatterField& field, f32 minX, f32 minZ,
             f32 maxX, f32 maxZ, std::vector<Candidate>& out) {
    const f32 cell = std::max(cls.cellSize, 0.05f);
    const i32 x0 = static_cast<i32>(std::floor(minX / cell)) - 1;
    const i32 x1 = static_cast<i32>(std::floor(maxX / cell)) + 1;
    const i32 z0 = static_cast<i32>(std::floor(minZ / cell)) - 1;
    const i32 z1 = static_cast<i32>(std::floor(maxZ / cell)) + 1;
    const u64 seed = classSeed(worldSeed, cls);
    for (i32 cz = z0; cz <= z1; ++cz) {
        for (i32 cx = x0; cx <= x1; ++cx) {
            const f32 x = (static_cast<f32>(cx) + static_cast<f32>(Core::toUnit(
                                                      Core::deriveSeed(seed, kSaltJitterX, cx, cz)))) * cell;
            const f32 z = (static_cast<f32>(cz) + static_cast<f32>(Core::toUnit(
                                                      Core::deriveSeed(seed, kSaltJitterZ, cx, cz)))) * cell;
            if (x < minX || x >= maxX || z < minZ || z >= maxZ) continue;
            const f32 mask = field.density ? std::clamp(field.density(cls.id, x, z), 0.0f, 1.0f) : 1.0f;
            const f32 chance = std::clamp(cls.density, 0.0f, 1.0f) * mask;
            if (static_cast<f32>(Core::toUnit(Core::deriveSeed(seed, kSaltAccept, cx, cz))) >= chance) {
                continue;
            }
            Candidate c;
            c.cls = &cls;
            c.cellX = cx;
            c.cellZ = cz;
            c.x = x;
            c.z = z;
            const u64 rank = Core::deriveSeed(seed, kSaltRank, cx, cz) >> 16;
            c.key = (static_cast<u64>(static_cast<u32>(cls.priority + 0x40000000)) << 32) | (rank >> 16);
            out.push_back(c);
        }
    }
}

}  // namespace

std::vector<Instance> scatterChunk(u64 worldSeed, i32 chunkX, i32 chunkZ, f32 chunkSize,
                                   const std::vector<ScatterClass>& classes,
                                   const ScatterField& field) {
    std::vector<Instance> result;
    if (classes.empty() || chunkSize <= 0.0f || !field.height) return result;

    const f32 minX = static_cast<f32>(chunkX) * chunkSize;
    const f32 minZ = static_cast<f32>(chunkZ) * chunkSize;
    const f32 maxX = minX + chunkSize;
    const f32 maxZ = minZ + chunkSize;

    // Conflicts reach as far as the two largest radii: read everybody inside that margin.
    f32 maxRadius = 0.0f;
    for (const ScatterClass& cls : classes) maxRadius = std::max(maxRadius, cls.radius);
    const f32 margin = maxRadius * 2.0f;

    std::vector<Candidate> near;
    std::vector<Candidate> mine;
    for (const ScatterClass& cls : classes) {
        collect(worldSeed, cls, field, minX - margin, minZ - margin, maxX + margin, maxZ + margin, near);
    }
    for (const Candidate& c : near) {
        if (c.x >= minX && c.x < maxX && c.z >= minZ && c.z < maxZ) mine.push_back(c);
    }

    for (const Candidate& c : mine) {
        bool blocked = false;
        for (const Candidate& other : near) {
            if (&other == &c) continue;
            const bool same = other.cls->id == c.cls->id && other.cellX == c.cellX && other.cellZ == c.cellZ;
            if (same) continue;
            const f32 dx = other.x - c.x;
            const f32 dz = other.z - c.z;
            const f32 reach = other.cls->radius + c.cls->radius;
            if (dx * dx + dz * dz >= reach * reach) continue;
            // The higher key wins; ties (identical keys) go to the smaller cell, never to both.
            if (other.key > c.key ||
                (other.key == c.key && (other.cellZ < c.cellZ ||
                                        (other.cellZ == c.cellZ && other.cellX < c.cellX)))) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        const ScatterClass& cls = *c.cls;
        const u64 seed = classSeed(worldSeed, cls);
        Instance instance;
        instance.classId = cls.id;
        instance.seed = Core::deriveSeed(seed, kSaltInstance, c.cellX, c.cellZ);
        instance.scale = static_cast<f32>(Core::toRange(
            Core::deriveSeed(seed, kSaltScale, c.cellX, c.cellZ), cls.minScale, cls.maxScale));
        instance.yaw = static_cast<f32>(
            Core::toRange(Core::deriveSeed(seed, kSaltYaw, c.cellX, c.cellZ), 0.0, 6.283185307179586));
        instance.variant = static_cast<u32>(Core::deriveSeed(seed, kSaltVariant, c.cellX, c.cellZ) %
                                            std::max(cls.variants, 1u));
        const f32 jitter = std::max(cls.tintJitter, 0.0f);
        auto channel = [&](i32 salt) {
            return 1.0f + static_cast<f32>(Core::toRange(
                              Core::deriveSeed(seed, kSaltTint + static_cast<u32>(salt) * 16u, c.cellX, c.cellZ),
                              -jitter, jitter));
        };
        instance.tint = Vec3(channel(0), channel(1), channel(2));

        Vec3 normal = field.normal ? field.normal(c.x, c.z) : Vec3(0.0f, 1.0f, 0.0f);
        if (normal.lengthSquared() < 1e-8f) normal = Vec3(0.0f, 1.0f, 0.0f);
        normal = normal.normalized();
        const f32 lean = std::clamp(cls.tiltToNormal, 0.0f, 1.0f);
        Vec3 up = Vec3(0.0f, 1.0f, 0.0f) * (1.0f - lean) + normal * lean;
        instance.up = up.normalized();
        const f32 ground = field.height(c.x, c.z);
        instance.position = Vec3(c.x, ground - cls.sink * instance.scale, c.z);
        result.push_back(instance);
    }

    // Canonical order, independent of how the candidates were collected.
    auto priorityOf = [&](u32 id) {
        for (const ScatterClass& cls : classes) {
            if (cls.id == id) return cls.priority;
        }
        return 0;
    };
    std::sort(result.begin(), result.end(), [&](const Instance& a, const Instance& b) {
        const i32 pa = priorityOf(a.classId);
        const i32 pb = priorityOf(b.classId);
        if (pa != pb) return pa > pb;
        if (a.classId != b.classId) return a.classId < b.classId;
        if (a.position.z != b.position.z) return a.position.z < b.position.z;
        return a.position.x < b.position.x;
    });
    return result;
}

}  // namespace Caffeine::Scatter
