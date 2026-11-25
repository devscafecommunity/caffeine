#include "world/WorldBuild.hpp"

#include "core/Hash.hpp"
#include "scatter/TerrainScatter.hpp"
#include "terrain/TerrainBiome.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <tuple>

namespace Caffeine::World {
namespace {

u64 bytesHash(std::string_view bytes) {
    u64 hash = 0xcbf29ce484222325ull;
    for (const char c : bytes) {
        hash ^= static_cast<u64>(static_cast<unsigned char>(c));
        hash *= 0x00000100000001b3ull;
    }
    return Core::splitMix64(hash);
}

u64 floatBits(f32 value) { return std::bit_cast<u32>(value); }

f32 heightAt(const Terrain::TerrainFrame& frame, f32 x, f32 z) {
    const f32 sizeX = std::max(frame.sizeX, 1e-3f), sizeZ = std::max(frame.sizeZ, 1e-3f);
    const f32 u = std::clamp(x / sizeX, 0.0f, 1.0f), v = std::clamp(z / sizeZ, 0.0f, 1.0f);
    return frame.heightmap->sampleBilinear(u, v) * frame.maxHeight;
}

Vec3 upAt(const Terrain::TerrainFrame& frame, f32 tilt, f32 x, f32 z) {
    Vec3 normal = Terrain::TerrainMasks::normalAt(frame, x, z);
    if (normal.lengthSquared() < 1e-8f) normal = Vec3(0.0f, 1.0f, 0.0f);
    const f32 lean = std::clamp(tilt, 0.0f, 1.0f);
    return (Vec3(0.0f, 1.0f, 0.0f) * (1.0f - lean) + normal.normalized() * lean).normalized();
}

/// Step 1: every reference, checked against the project. Appends one error per problem.
void resolveRefs(const WorldBuildRequest& request, std::vector<std::string>& errors, u64& key) {
    const WorldComposition& c = *request.composition;
    std::vector<std::pair<std::string, RecipeKind>> refs;
    if (!c.terrain.ref.empty()) refs.emplace_back(c.terrain.ref, RecipeKind::Terrain);
    for (const WorldWater& w : c.water) refs.emplace_back(w.ref, RecipeKind::Water);
    for (const WorldMaterial& m : c.materials) refs.emplace_back(m.ref, RecipeKind::Material);
    for (const WorldClass& cls : c.classes) refs.emplace_back(cls.ref, RecipeKind::Nature);

    for (const auto& [ref, wanted] : refs) {
        if (!request.resolve) continue;
        const RecipeInfo info = request.resolve(ref);
        if (!info.found) {
            errors.push_back("`" + ref + "` was not found");
            continue;
        }
        if (info.kind != wanted) {
            errors.push_back("`" + ref + "` is a " + recipeKindName(info.kind) + " recipe, expected " +
                             recipeKindName(wanted));
            continue;
        }
        if (info.generatorVersion > Core::GENERATOR_VERSION) {
            errors.push_back("`" + ref + "` was made by generator_version " + std::to_string(info.generatorVersion) +
                             "; this engine builds up to " + std::to_string(Core::GENERATOR_VERSION));
            continue;
        }
        key = Core::hashCombine(key, info.contentHash);
        key = Core::hashCombine(key, info.generatorVersion);
    }
}

}  // namespace

WorldPart build(const WorldBuildRequest& request) {
    WorldPart part;
    if (!request.composition) {
        part.errors.push_back("no composition");
        return part;
    }
    const WorldComposition& composition = *request.composition;
    const Terrain::TerrainFrame& frame = request.frame;

    part.errors = composition.validate();
    if (request.generatorVersion != Core::GENERATOR_VERSION) {
        part.errors.push_back("the world was made by generator_version " + std::to_string(request.generatorVersion) +
                              "; this engine builds version " + std::to_string(Core::GENERATOR_VERSION));
    }

    u64 key = Core::hashCombine(composition.seed, request.generatorVersion);
    key = Core::hashCombine(key, bytesHash(composition.toText()));
    resolveRefs(request, part.errors, key);

    const bool baked = frame.heightmap && !frame.heightmap->empty();
    if (!baked) {
        part.errors.push_back("the terrain `" + composition.terrain.ref + "` is not baked");
    } else {
        const auto differs = [](f32 wanted, f32 actual) { return wanted > 0.0f && std::fabs(wanted - actual) > 1e-3f; };
        if (differs(composition.terrain.sizeX, frame.sizeX) || differs(composition.terrain.sizeZ, frame.sizeZ)) {
            part.errors.push_back("the baked terrain is not the size the composition asks for");
        }
    }

    static const Terrain::TerrainMasks kNoMasks;
    const Terrain::TerrainMasks& masks = request.masks ? *request.masks : kNoMasks;
    for (const WorldClass& cls : composition.classes) {
        for (const Terrain::BiomeTerm& term : cls.where) {
            if (!masks.has(term.mask)) {
                part.errors.push_back("class `" + cls.id + "`: the terrain has no `" + term.mask + "` mask");
            }
        }
    }
    if (!part.errors.empty()) return part;

    // Region, normalised the way `scatterTerrain` does it, so the cache key names what is really built.
    f32 x0 = request.x0, z0 = request.z0, x1 = request.x1, z1 = request.z1;
    if (x1 <= x0 || z1 <= z0) {
        x0 = 0.0f;
        z0 = 0.0f;
        x1 = frame.sizeX;
        z1 = frame.sizeZ;
    }
    x0 = std::max(x0, 0.0f);
    z0 = std::max(z0, 0.0f);
    x1 = std::min(x1, frame.sizeX);
    z1 = std::min(z1, frame.sizeZ);
    for (const f32 edge : {x0, z0, x1, z1}) key = Core::hashCombine(key, floatBits(edge));
    part.cacheKey = key;

    // Step 5: every class in one scatter, so priority and exclusion work across classes.
    Terrain::BiomeTable biome;
    Scatter::TerrainScatterRequest scatter;
    scatter.seed = composition.seed;
    scatter.frame = frame;
    scatter.masks = request.masks;
    scatter.biome = &biome;
    scatter.x0 = x0;
    scatter.z0 = z0;
    scatter.x1 = x1;
    scatter.z1 = z1;
    u32 removals = 0;
    for (const WorldOverride& o : composition.overrides) removals += o.op == OverrideOp::Remove ? 1u : 0u;
    scatter.maxInstances = request.maxInstances == 0 ? 0 : request.maxInstances + removals;

    std::map<u32, const WorldClass*> bySalt;
    for (const WorldClass& cls : composition.classes) {
        Scatter::ScatterClass sc;
        sc.id = classSalt(cls.id);
        sc.priority = cls.priority;
        sc.cellSize = cls.cell;
        sc.radius = cls.radius;
        sc.density = cls.density;
        sc.minScale = cls.minScale;
        sc.maxScale = cls.maxScale;
        sc.tiltToNormal = cls.tilt;
        sc.sink = cls.sink;
        sc.tintJitter = cls.tint;
        sc.variants = cls.variants;
        scatter.classes.push_back(sc);
        biome.set(Terrain::BiomeRule{sc.id, cls.base, cls.where});
        bySalt[sc.id] = &cls;
    }
    const Scatter::TerrainScatterResult scattered = Scatter::scatterTerrain(scatter);
    part.truncated = scattered.truncated;

    // Step 6: the edit layer, keyed by (class, cell); a later line wins over an earlier one.
    std::map<std::tuple<u32, i32, i32>, std::vector<const WorldOverride*>> edits;
    for (const WorldOverride& o : composition.overrides) {
        edits[{classSalt(o.classId), o.cellX, o.cellZ}].push_back(&o);
    }

    std::map<u32, std::vector<Scatter::Instance>> grouped;
    u32 removedCount = 0;
    for (Scatter::Instance instance : scattered.instances) {
        const WorldClass& cls = *bySalt.at(instance.classId);
        bool removed = false;
        const auto it = edits.find({instance.classId, instance.cellX, instance.cellZ});
        if (it != edits.end()) {
            for (const WorldOverride* o : it->second) {
                switch (o->op) {
                    case OverrideOp::Remove: removed = true; break;
                    case OverrideOp::Pin: instance.variant = o->variant; break;
                    case OverrideOp::Move: {
                        const f32 x = std::clamp(o->x, 0.0f, frame.sizeX);
                        const f32 z = std::clamp(o->z, 0.0f, frame.sizeZ);
                        instance.position = Vec3(x, heightAt(frame, x, z) - cls.sink * instance.scale, z);
                        instance.up = upAt(frame, cls.tilt, x, z);
                        break;
                    }
                }
            }
        }
        if (removed) {
            ++removedCount;
        } else {
            grouped[instance.classId].push_back(instance);
        }
    }
    part.total = scattered.total > removedCount ? scattered.total - removedCount : 0u;

    for (const WorldClass& cls : composition.classes) {
        WorldClassInstances out;
        out.id = cls.id;
        out.salt = classSalt(cls.id);
        out.ref = cls.ref;
        auto found = grouped.find(out.salt);
        if (found != grouped.end()) out.instances = std::move(found->second);
        // Canonical order inside a class: by cell, whatever the region or chunking.
        std::sort(out.instances.begin(), out.instances.end(), [](const Scatter::Instance& a, const Scatter::Instance& b) {
            return a.cellZ != b.cellZ ? a.cellZ < b.cellZ : a.cellX < b.cellX;
        });
        part.classes.push_back(std::move(out));
    }
    std::sort(part.classes.begin(), part.classes.end(), [&](const WorldClassInstances& a, const WorldClassInstances& b) {
        const i32 pa = bySalt.at(a.salt)->priority, pb = bySalt.at(b.salt)->priority;
        return pa != pb ? pa > pb : a.salt < b.salt;
    });

    // The budget: `scatterTerrain` kept room for the removals; trim what removals that matched nothing left over.
    u32 kept = 0;
    for (WorldClassInstances& cls : part.classes) {
        if (request.maxInstances != 0) {
            const u32 room = request.maxInstances > kept ? request.maxInstances - kept : 0u;
            if (cls.instances.size() > room) {
                cls.instances.resize(room);
                part.truncated = true;
            }
        }
        kept += static_cast<u32>(cls.instances.size());
    }

    part.water = composition.water;
    part.materials = composition.materials;
    part.ok = true;
    return part;
}

u64 instanceHash(const WorldPart& part) {
    u64 h = 0x5745524C44ull;  // "WORLD"
    for (const WorldClassInstances& cls : part.classes) {
        h = Core::hashCombine(h, cls.salt);
        h = Core::hashCombine(h, cls.instances.size());
        for (const Scatter::Instance& i : cls.instances) {
            h = Core::hashCombine(h, static_cast<u32>(i.cellX));
            h = Core::hashCombine(h, static_cast<u32>(i.cellZ));
            h = Core::hashCombine(h, i.variant);
            h = Core::hashCombine(h, static_cast<u64>(std::llround(i.position.x * 1000.0f)));
            h = Core::hashCombine(h, static_cast<u64>(std::llround(i.position.y * 1000.0f)));
            h = Core::hashCombine(h, static_cast<u64>(std::llround(i.position.z * 1000.0f)));
            h = Core::hashCombine(h, i.seed);
        }
    }
    return h;
}

}  // namespace Caffeine::World
