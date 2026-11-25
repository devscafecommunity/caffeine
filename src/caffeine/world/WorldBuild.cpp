#include "world/WorldBuild.hpp"

#include "core/Hash.hpp"
#include "scatter/TerrainScatter.hpp"
#include "terrain/TerrainBiome.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

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

constexpr f32 kCrowdingRadii = 3.0f;

/// Instances bucketed on a grid, to count how crowded a point is.
class NeighbourGrid {
public:
    NeighbourGrid(const std::vector<Scatter::Instance>& instances, f32 cell) : m_instances(instances), m_cell(cell) {
        for (size_t i = 0; i < instances.size(); ++i) {
            m_cells[keyOf(instances[i].position.x, instances[i].position.z)].push_back(static_cast<u32>(i));
        }
    }

    /// Other instances within `radius` of (x, z), over 6, at most 1. `self` is not counted.
    f32 crowding(size_t self, f32 x, f32 z, f32 radius) const {
        const i64 cx = cellOf(x), cz = cellOf(z);
        const i64 span = static_cast<i64>(std::ceil(radius / m_cell));
        u32 count = 0;
        for (i64 dz = -span; dz <= span; ++dz) {
            for (i64 dx = -span; dx <= span; ++dx) {
                const auto it = m_cells.find(pack(cx + dx, cz + dz));
                if (it == m_cells.end()) continue;
                for (const u32 i : it->second) {
                    if (i == self) continue;
                    const f32 ox = m_instances[i].position.x - x, oz = m_instances[i].position.z - z;
                    if (ox * ox + oz * oz < radius * radius) ++count;
                }
            }
        }
        return std::min(static_cast<f32>(count) / 6.0f, 1.0f);
    }

private:
    i64 cellOf(f32 v) const { return static_cast<i64>(std::floor(v / m_cell)); }
    static u64 pack(i64 x, i64 z) { return (static_cast<u64>(static_cast<u32>(x)) << 32) | static_cast<u32>(z); }
    u64 keyOf(f32 x, f32 z) const { return pack(cellOf(x), cellOf(z)); }

    const std::vector<Scatter::Instance>& m_instances;
    f32 m_cell;
    std::unordered_map<u64, std::vector<u32>> m_cells;
};

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

    part.errors = composition.validate();
    if (request.generatorVersion != Core::GENERATOR_VERSION) {
        part.errors.push_back("the world was made by generator_version " + std::to_string(request.generatorVersion) +
                              "; this engine builds version " + std::to_string(Core::GENERATOR_VERSION));
    }

    u64 key = Core::hashCombine(composition.seed, request.generatorVersion);
    key = Core::hashCombine(key, bytesHash(composition.toText()));
    resolveRefs(request, part.errors, key);

    const bool baked = request.frame.heightmap && !request.frame.heightmap->empty();
    if (!baked) {
        part.errors.push_back("the terrain `" + composition.terrain.ref + "` is not baked");
    } else {
        const auto differs = [](f32 wanted, f32 actual) { return wanted > 0.0f && std::fabs(wanted - actual) > 1e-3f; };
        if (differs(composition.terrain.sizeX, request.frame.sizeX) ||
            differs(composition.terrain.sizeZ, request.frame.sizeZ)) {
            part.errors.push_back("the baked terrain is not the size the composition asks for");
        }
    }
    if (!part.errors.empty()) return part;

    // Step 3: water, on our own copy of the heightfield (carving changes it) and of the masks (+ water_distance).
    Terrain::TerrainHeightmap heights = *request.frame.heightmap;
    Terrain::TerrainFrame frame = request.frame;
    frame.heightmap = &heights;
    Terrain::TerrainMasks masks = request.masks ? *request.masks : Terrain::TerrainMasks{};
    WaterLayer water = buildWater(composition, heights, frame, masks, part.errors);
    if (!masks.set("water_distance", water.resolutionX, water.resolutionZ, water.distance.data())) {
        part.errors.push_back("the terrain already stores 16 masks: no room for `water_distance`");
    }

    bool needNeighbours = false;
    for (const WorldClass& cls : composition.classes) {
        for (const Terrain::BiomeTerm& term : cls.where) {
            if (!masks.has(term.mask)) {
                part.errors.push_back("class `" + cls.id + "`: the terrain has no `" + term.mask + "` mask");
            }
        }
        for (const WorldAdapt& a : cls.adapt) {
            if (a.mask == "neighbours") {
                needNeighbours = true;
            } else if (!masks.has(a.mask)) {
                part.errors.push_back("class `" + cls.id + "`: adapt reads `" + a.mask + "`, which the terrain lacks");
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

    // Step 5: every class in one scatter, so priority and exclusion work across classes. Crowding needs the
    // neighbours just outside the region too, so the scatter then reads a margin of three radii around it.
    Terrain::BiomeTable biome;
    Scatter::TerrainScatterRequest scatter;
    scatter.seed = composition.seed;
    scatter.frame = frame;
    scatter.masks = &masks;
    scatter.biome = &biome;
    std::map<u32, const WorldClass*> bySalt;
    f32 reach = 0.0f;
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
        reach = std::max(reach, kCrowdingRadii * cls.radius);
    }
    const f32 margin = needNeighbours ? reach : 0.0f;
    scatter.x0 = std::max(x0 - margin, 0.0f);
    scatter.z0 = std::max(z0 - margin, 0.0f);
    scatter.x1 = std::min(x1 + margin, frame.sizeX);
    scatter.z1 = std::min(z1 + margin, frame.sizeZ);
    u32 removals = 0;
    for (const WorldOverride& o : composition.overrides) removals += o.op == OverrideOp::Remove ? 1u : 0u;
    if (request.maxInstances != 0) {
        const f64 grow = static_cast<f64>((scatter.x1 - scatter.x0) * (scatter.z1 - scatter.z0)) /
                         std::max(static_cast<f64>((x1 - x0) * (z1 - z0)), 1e-6);
        scatter.maxInstances = static_cast<u32>(std::min<f64>(std::ceil(request.maxInstances * grow) + removals,
                                                              4294967295.0));
    }
    const Scatter::TerrainScatterResult scattered = Scatter::scatterTerrain(scatter);
    part.truncated = scattered.truncated;

    // Crowding is read from the generated instances, before the edit layer.
    NeighbourGrid crowd(scattered.instances, std::max(reach, 1e-3f));

    // Step 6: the edit layer, keyed by (class, cell); a later line wins over an earlier one.
    std::map<std::tuple<u32, i32, i32>, std::vector<const WorldOverride*>> edits;
    for (const WorldOverride& o : composition.overrides) {
        edits[{classSalt(o.classId), o.cellX, o.cellZ}].push_back(&o);
    }

    std::map<u32, std::vector<std::pair<Scatter::Instance, InstanceContext>>> grouped;
    u32 removedInRegion = 0;
    for (size_t index = 0; index < scattered.instances.size(); ++index) {
        Scatter::Instance instance = scattered.instances[index];
        const Vec3 born = instance.position;
        if (born.x < x0 || born.x >= x1 || born.z < z0 || born.z >= z1) continue;  // context only
        const WorldClass& cls = *bySalt.at(instance.classId);
        bool removed = false;
        const auto it = edits.find({instance.classId, instance.cellX, instance.cellZ});
        if (it != edits.end()) {
            for (const WorldOverride* o : it->second) {
                switch (o->op) {
                    case OverrideOp::Remove: removed = true; break;
                    case OverrideOp::Pin: instance.variant = o->variant; break;
                    case OverrideOp::Move:
                        instance.position.x = std::clamp(o->x, 0.0f, frame.sizeX);
                        instance.position.z = std::clamp(o->z, 0.0f, frame.sizeZ);
                        break;
                }
            }
        }
        if (removed) {
            ++removedInRegion;
            continue;
        }

        // Ground context at the final position, then the class's adapt mappings.
        const f32 x = instance.position.x, z = instance.position.z;
        InstanceContext ctx;
        masks.sample(frame, "slope", x, z, ctx.slope);
        masks.sample(frame, "height", x, z, ctx.height);
        if (masks.has("wetness")) masks.sample(frame, "wetness", x, z, ctx.wetness);
        masks.sample(frame, "water_distance", x, z, ctx.waterDistance);
        ctx.neighbours = needNeighbours ? crowd.crowding(index, x, z, kCrowdingRadii * cls.radius) : 0.0f;

        f32 tilt = cls.tilt;
        for (const WorldAdapt& a : cls.adapt) {
            f32 v = 0.0f;
            if (a.mask == "neighbours") {
                v = ctx.neighbours;
            } else {
                masks.sample(frame, a.mask, x, z, v);
            }
            v = std::clamp(v, 0.0f, 1.0f);
            const f32 amount = a.effectiveAmount();
            if (a.target == "size") {
                instance.scale *= std::max(1.0f + amount * (2.0f * v - 1.0f), 0.05f);
            } else if (a.target == "lean") {
                tilt += amount * v;
            } else if (a.target == "tint") {
                instance.tint = instance.tint * std::clamp(1.0f - amount * v, 0.0f, 2.0f);
            } else if (a.target == "wetness") {
                ctx.wetnessModifier = std::clamp(amount * v, 0.0f, 1.0f);
            } else if (a.target == "snow") {
                ctx.snowModifier = std::clamp(amount * v, 0.0f, 1.0f);
            }
        }
        const bool conform = tilt != cls.tilt || x != born.x || z != born.z;
        if (conform) instance.up = upAt(frame, tilt, x, z);
        instance.position.y = heightAt(frame, x, z) - cls.sink * instance.scale;
        grouped[instance.classId].push_back({instance, ctx});
    }

    u32 inRegion = 0;
    for (const WorldClass& cls : composition.classes) {
        WorldClassInstances out;
        out.id = cls.id;
        out.salt = classSalt(cls.id);
        out.ref = cls.ref;
        out.unique = cls.unique;
        auto found = grouped.find(out.salt);
        if (found != grouped.end()) {
            auto& list = found->second;
            // Canonical order inside a class: by cell, whatever the region or chunking.
            std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
                return a.first.cellZ != b.first.cellZ ? a.first.cellZ < b.first.cellZ : a.first.cellX < b.first.cellX;
            });
            for (auto& [instance, ctx] : list) {
                out.instances.push_back(instance);
                out.context.push_back(ctx);
            }
        }
        inRegion += static_cast<u32>(out.instances.size());
        part.classes.push_back(std::move(out));
    }
    std::sort(part.classes.begin(), part.classes.end(), [&](const WorldClassInstances& a, const WorldClassInstances& b) {
        const i32 pa = bySalt.at(a.salt)->priority, pb = bySalt.at(b.salt)->priority;
        return pa != pb ? pa > pb : a.salt < b.salt;
    });
    // Without a margin the scatter counts the region exactly, also past its cap; with one, what we kept is the count
    // (a lower bound when the scatter stopped, which `truncated` says).
    if (margin > 0.0f) {
        part.total = inRegion;
    } else {
        part.total = scattered.total > removedInRegion ? scattered.total - removedInRegion : 0u;
    }

    // The budget, in placement order, after the edits.
    u32 kept = 0;
    for (WorldClassInstances& cls : part.classes) {
        if (request.maxInstances != 0) {
            const u32 room = request.maxInstances > kept ? request.maxInstances - kept : 0u;
            if (cls.instances.size() > room) {
                cls.instances.resize(room);
                cls.context.resize(room);
                part.truncated = true;
            }
        }
        kept += static_cast<u32>(cls.instances.size());
    }

    part.terrain = std::move(heights);
    part.water = std::move(water);
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
            h = Core::hashCombine(h, static_cast<u64>(std::llround(i.scale * 10000.0f)));
            h = Core::hashCombine(h, i.seed);
        }
    }
    return h;
}

}  // namespace Caffeine::World
