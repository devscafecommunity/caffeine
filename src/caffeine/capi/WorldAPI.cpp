// World Composer C ABI (v32): a session-free wrapper over src/caffeine/world. See EngineAPI.h.

#include "caffeine/capi/EngineAPI.h"

#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "world/WorldBuild.hpp"
#include "world/WorldComposition.hpp"
#include "world/WorldInstanceFile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct CaffeineWorld {
    Caffeine::World::WorldPart part;
    /// Placement-order instances flattened, with the index of their class.
    std::vector<std::pair<uint32_t, uint32_t>> flat;  // (class index, index in class)
    Caffeine::u64 hash = 0;
};

namespace {

using namespace Caffeine;

void copyText(char* out, size_t size, const std::string& text) {
    if (!out || size == 0) return;
    std::snprintf(out, size, "%s", text.c_str());
}

bool kindFromC(uint32_t value, World::RecipeKind& out) {
    if (value > CAFFEINE_RECIPE_TEXTURE) return false;
    out = static_cast<World::RecipeKind>(value);
    return true;
}

/// Everything a build reads, owned (the request points into it, so it never moves).
struct Inputs {
    Inputs() = default;
    Inputs(const Inputs&) = delete;
    Inputs& operator=(const Inputs&) = delete;

    World::WorldComposition composition;
    std::map<std::string, World::RecipeInfo> recipes;
    Terrain::TerrainHeightmap heights;
    Terrain::TerrainMasks masks;
    World::WorldBuildRequest request;
    /// Problems found before the build (a graph that does not parse, a mask that does not store).
    std::vector<std::string> errors;
};

CaffeineResult prepare(const CaffeineWorldBuildDesc* desc, Inputs& in) {
    if (!desc || !desc->graph_text) return CAFFEINE_INVALID_ARGUMENT;
    if (desc->recipe_count > 0 && !desc->recipes) return CAFFEINE_INVALID_ARGUMENT;
    if (desc->mask_count > 0 && !desc->masks) return CAFFEINE_INVALID_ARGUMENT;
    const uint64_t cells = static_cast<uint64_t>(desc->resolution_x) * desc->resolution_z;
    if (cells > 0 && !desc->heights) return CAFFEINE_INVALID_ARGUMENT;
    if (cells > 0 && (desc->resolution_x < 2 || desc->resolution_z < 2)) return CAFFEINE_INVALID_ARGUMENT;
    if (cells > 0 && !(desc->size_x > 0.0f && desc->size_z > 0.0f)) return CAFFEINE_INVALID_ARGUMENT;

    std::string parseError;
    if (!World::WorldComposition::fromText(desc->graph_text, in.composition, &parseError)) {
        in.errors.push_back(parseError);
        return CAFFEINE_OK;
    }
    for (uint32_t i = 0; i < desc->recipe_count; ++i) {
        const CaffeineWorldRecipe& r = desc->recipes[i];
        if (!r.ref) continue;
        World::RecipeInfo info;
        info.found = kindFromC(r.kind, info.kind);
        info.generatorVersion = r.generator_version;
        info.contentHash = r.content_hash;
        in.recipes[r.ref] = info;
    }
    if (cells > 0) {
        in.heights.resize(desc->resolution_x, desc->resolution_z);
        std::copy(desc->heights, desc->heights + cells, in.heights.heights().begin());
    }
    for (uint32_t i = 0; i < desc->mask_count; ++i) {
        const CaffeineWorldMask& m = desc->masks[i];
        if (!m.name || !m.data || !in.masks.set(m.name, m.resolution_x, m.resolution_z, m.data)) {
            in.errors.push_back(std::string("the mask `") + (m.name ? m.name : "") + "` could not be stored");
        }
    }
    World::WorldBuildRequest& request = in.request;
    request.composition = &in.composition;
    request.generatorVersion = desc->generator_version;
    if (desc->check_refs) {
        const auto* recipes = &in.recipes;
        request.resolve = [recipes](const std::string& ref) {
            const auto it = recipes->find(ref);
            return it == recipes->end() ? World::RecipeInfo{} : it->second;
        };
    }
    request.frame.heightmap = cells > 0 ? &in.heights : nullptr;
    request.frame.sizeX = desc->size_x;
    request.frame.sizeZ = desc->size_z;
    request.frame.maxHeight = desc->max_height;
    request.masks = &in.masks;
    request.x0 = desc->region[0];
    request.z0 = desc->region[1];
    request.x1 = desc->region[2];
    request.z1 = desc->region[3];
    request.maxInstances = desc->max_instances;
    return CAFFEINE_OK;
}

void adopt(CaffeineWorld& world, World::WorldPart part) {
    world.part = std::move(part);
    world.flat.clear();
    for (uint32_t c = 0; c < world.part.classes.size(); ++c) {
        for (uint32_t i = 0; i < world.part.classes[c].instances.size(); ++i) world.flat.emplace_back(c, i);
    }
    world.hash = World::instanceHash(world.part);
}

}  // namespace

extern "C" {

CaffeineResult caffeine_world_build(const CaffeineWorldBuildDesc* desc, CaffeineWorld** out_world) {
    if (!out_world) return CAFFEINE_INVALID_ARGUMENT;
    *out_world = nullptr;
    Inputs inputs;
    if (const CaffeineResult r = prepare(desc, inputs); r != CAFFEINE_OK) return r;
    auto world = new CaffeineWorld();
    if (!inputs.errors.empty()) {
        world->part.errors = std::move(inputs.errors);
    } else {
        adopt(*world, World::build(inputs.request));
    }
    *out_world = world;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_cache_key(const CaffeineWorldBuildDesc* desc, uint64_t* out_key) {
    if (!out_key) return CAFFEINE_INVALID_ARGUMENT;
    *out_key = 0;
    Inputs inputs;
    if (const CaffeineResult r = prepare(desc, inputs); r != CAFFEINE_OK) return r;
    if (inputs.errors.empty()) *out_key = World::cacheKeyOf(inputs.request);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_build_cached(const CaffeineWorldBuildDesc* desc, const char* cache_dir,
                                           CaffeineWorld** out_world, int32_t* out_hit) {
    if (!out_world || !cache_dir) return CAFFEINE_INVALID_ARGUMENT;
    *out_world = nullptr;
    if (out_hit) *out_hit = 0;
    Inputs inputs;
    if (const CaffeineResult r = prepare(desc, inputs); r != CAFFEINE_OK) return r;
    auto world = new CaffeineWorld();
    if (!inputs.errors.empty()) {
        world->part.errors = std::move(inputs.errors);
    } else {
        const World::WorldCache cache(cache_dir);
        World::WorldPart built;
        bool hit = false;
        const std::optional<World::InstanceFile> file = cache.buildOrLoad(inputs.request, &built, &hit);
        if (hit && file) {
            adopt(*world, World::partFromInstances(*file));
        } else {
            adopt(*world, std::move(built));
        }
        if (out_hit) *out_hit = hit ? 1 : 0;
    }
    *out_world = world;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_write_cinst(const CaffeineWorld* world, const char* path, float chunk_size) {
    if (!world || !path) return CAFFEINE_INVALID_ARGUMENT;
    if (!world->part.ok) return CAFFEINE_ERROR;
    const World::InstanceFile file =
        World::bakeInstances(world->part, chunk_size > 0.0f ? chunk_size : World::kDefaultInstanceChunk);
    return World::writeInstanceFile(path, file) ? CAFFEINE_OK : CAFFEINE_ERROR;
}

CaffeineResult caffeine_world_read_cinst(const char* path, CaffeineWorld** out_world, char* error,
                                         uint32_t error_size) {
    if (!path || !out_world) return CAFFEINE_INVALID_ARGUMENT;
    *out_world = nullptr;
    World::InstanceFile file;
    std::string why;
    if (!World::readInstanceFile(path, file, &why)) {
        copyText(error, error_size, why);
        return CAFFEINE_INVALID_ARGUMENT;
    }
    copyText(error, error_size, "");
    auto world = new CaffeineWorld();
    adopt(*world, World::partFromInstances(file));
    *out_world = world;
    return CAFFEINE_OK;
}

void caffeine_world_free(CaffeineWorld* world) { delete world; }

void caffeine_world_info(const CaffeineWorld* world, CaffeineWorldInfo* out) {
    if (!out) return;
    *out = CaffeineWorldInfo{};
    if (!world) return;
    const World::WorldPart& part = world->part;
    out->ok = part.ok ? 1 : 0;
    out->error_count = static_cast<uint32_t>(part.errors.size());
    out->class_count = static_cast<uint32_t>(part.classes.size());
    out->instance_count = static_cast<uint32_t>(world->flat.size());
    out->total = part.total;
    out->truncated = part.truncated ? 1 : 0;
    out->cache_key = part.cacheKey;
    out->instance_hash = world->hash;
    out->water_body_count = static_cast<uint32_t>(part.water.bodies.size());
    out->carved = part.water.carved ? 1 : 0;
    out->resolution_x = part.water.resolutionX;
    out->resolution_z = part.water.resolutionZ;
}

CaffeineResult caffeine_world_error(const CaffeineWorld* world, uint32_t index, char* out, uint32_t size) {
    if (!world || !out || size == 0) return CAFFEINE_INVALID_ARGUMENT;
    if (index >= world->part.errors.size()) return CAFFEINE_NOT_FOUND;
    copyText(out, size, world->part.errors[index]);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_class(const CaffeineWorld* world, uint32_t index, CaffeineWorldClass* out) {
    if (!world || !out) return CAFFEINE_INVALID_ARGUMENT;
    if (index >= world->part.classes.size()) return CAFFEINE_NOT_FOUND;
    *out = CaffeineWorldClass{};
    uint32_t first = 0;
    for (uint32_t c = 0; c < index; ++c) first += static_cast<uint32_t>(world->part.classes[c].instances.size());
    const World::WorldClassInstances& cls = world->part.classes[index];
    copyText(out->id, sizeof(out->id), cls.id);
    copyText(out->ref, sizeof(out->ref), cls.ref);
    out->salt = cls.salt;
    out->first_instance = first;
    out->instance_count = static_cast<uint32_t>(cls.instances.size());
    out->unique = cls.unique ? 1 : 0;
    out->param_count = static_cast<uint32_t>(std::min<size_t>(cls.params.size(), 4));
    for (uint32_t k = 0; k < out->param_count; ++k) copyText(out->params[k], sizeof(out->params[k]), cls.params[k]);
    return CAFFEINE_OK;
}

uint32_t caffeine_world_instances(const CaffeineWorld* world, uint32_t first, CaffeineWorldInstance* out,
                                  uint32_t capacity) {
    if (!world || !out || first >= world->flat.size()) return 0;
    const uint32_t count = std::min<uint32_t>(capacity, static_cast<uint32_t>(world->flat.size()) - first);
    for (uint32_t k = 0; k < count; ++k) {
        const auto [c, i] = world->flat[first + k];
        const World::WorldClassInstances& cls = world->part.classes[c];
        const Scatter::Instance& in = cls.instances[i];
        const World::InstanceContext& ctx = cls.context[i];
        CaffeineWorldInstance& o = out[k];
        o = CaffeineWorldInstance{};
        o.class_index = c;
        o.variant = in.variant;
        o.cell[0] = in.cellX;
        o.cell[1] = in.cellZ;
        o.position[0] = in.position.x;
        o.position[1] = in.position.y;
        o.position[2] = in.position.z;
        o.up[0] = in.up.x;
        o.up[1] = in.up.y;
        o.up[2] = in.up.z;
        o.yaw = in.yaw;
        o.scale = in.scale;
        o.tint[0] = in.tint.x;
        o.tint[1] = in.tint.y;
        o.tint[2] = in.tint.z;
        o.seed = in.seed;
        o.slope = ctx.slope;
        o.height = ctx.height;
        o.wetness = ctx.wetness;
        o.water_distance = ctx.waterDistance;
        o.neighbours = ctx.neighbours;
        o.wetness_modifier = ctx.wetnessModifier;
        o.snow_modifier = ctx.snowModifier;
        for (int k = 0; k < 4; ++k) o.params[k] = ctx.params[k];
    }
    return count;
}

CaffeineResult caffeine_world_water_body(const CaffeineWorld* world, uint32_t index, CaffeineWorldWaterBody* out,
                                         uint8_t* coverage, float* surface) {
    if (!world || !out) return CAFFEINE_INVALID_ARGUMENT;
    if (index >= world->part.water.bodies.size()) return CAFFEINE_NOT_FOUND;
    const World::WaterBody& body = world->part.water.bodies[index];
    *out = CaffeineWorldWaterBody{};
    copyText(out->ref, sizeof(out->ref), body.ref);
    out->lake = body.lake ? 1 : 0;
    out->cells = body.cells;
    if (coverage) std::memcpy(coverage, body.coverage.data(), body.coverage.size());
    if (surface) std::memcpy(surface, body.surface.data(), body.surface.size() * sizeof(float));
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_water_distance(const CaffeineWorld* world, uint8_t* out) {
    if (!world || !out) return CAFFEINE_INVALID_ARGUMENT;
    if (world->part.water.distance.empty()) return CAFFEINE_NOT_FOUND;
    std::memcpy(out, world->part.water.distance.data(), world->part.water.distance.size());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_heights(const CaffeineWorld* world, float* out) {
    if (!world || !out) return CAFFEINE_INVALID_ARGUMENT;
    const std::vector<float>& h = world->part.terrain.heights();
    if (h.empty()) return CAFFEINE_NOT_FOUND;
    std::memcpy(out, h.data(), h.size() * sizeof(float));
    return CAFFEINE_OK;
}

CaffeineResult caffeine_world_check(const char* graph_text, char* error, uint32_t error_size) {
    World::WorldComposition composition;
    std::string why;
    std::vector<std::string> problems;
    if (!World::WorldComposition::fromText(graph_text ? graph_text : "", composition, &why)) {
        problems.push_back(why);
    } else {
        problems = composition.validate();
    }
    std::string joined;
    for (const std::string& p : problems) joined += (joined.empty() ? "" : "\n") + p;
    copyText(error, error_size, joined);
    return problems.empty() ? CAFFEINE_OK : CAFFEINE_INVALID_ARGUMENT;
}

}  // extern "C"
