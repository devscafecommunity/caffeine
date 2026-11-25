#include "render/TerrainVegetationGpu.hpp"

#ifdef CF_HAS_SDL3

#include "terrain/TerrainCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Caffeine::Render {

TerrainVegetationGpu& TerrainVegetationGpu::instance() {
    static TerrainVegetationGpu gpu;
    return gpu;
}

Mat4 TerrainVegetationGpu::instanceMatrix(f32 x, f32 y, f32 z, const f32* up, f32 yaw, f32 scale) {
    // The heading is the yaw on the ground plane, pulled onto the plane the up axis stands on.
    Vec3 u(up[0], up[1], up[2]);
    Vec3 heading(std::cos(yaw), 0.0f, std::sin(yaw));
    const f32 along = heading.x * u.x + heading.y * u.y + heading.z * u.z;
    Vec3 forward(heading.x - u.x * along, heading.y - u.y * along, heading.z - u.z * along);
    f32 length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (length < 1e-5f) {
        forward = Vec3(1.0f, 0.0f, 0.0f);
        length = 1.0f;
    }
    forward = Vec3(forward.x / length, forward.y / length, forward.z / length);
    const Vec3 right(u.y * forward.z - u.z * forward.y, u.z * forward.x - u.x * forward.z, u.x * forward.y - u.y * forward.x);
    Mat4 m = Mat4::identity();
    // Columns: right (X), up (Y), forward (Z), position; scaled uniformly.
    m(0, 0) = right.x * scale;  m(1, 0) = right.y * scale;  m(2, 0) = right.z * scale;
    m(0, 1) = u.x * scale;      m(1, 1) = u.y * scale;      m(2, 1) = u.z * scale;
    m(0, 2) = forward.x * scale; m(1, 2) = forward.y * scale; m(2, 2) = forward.z * scale;
    m(0, 3) = x;  m(1, 3) = y;  m(2, 3) = z;
    return m;
}

void TerrainVegetationGpu::releaseChunk(RHI::RenderDevice* device, Chunk& chunk) {
    if (device) {
        for (VegetationBatch& batch : chunk.batches) {
            if (batch.buffer) device->destroyBuffer(batch.buffer);
        }
    }
    chunk.batches.clear();
    chunk.built = false;
}

u64 TerrainVegetationGpu::bytesFor(ECS::Entity entity) const {
    const auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return 0;
    u64 bytes = 0;
    for (const auto& chunk : it->second.chunks) {
        for (const VegetationBatch& batch : chunk.second.batches) bytes += static_cast<u64>(batch.count) * 16ull * sizeof(f32);
    }
    return bytes;
}

void TerrainVegetationGpu::removeEntity(ECS::Entity entity, RHI::RenderDevice* device) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    for (auto& chunk : it->second.chunks) releaseChunk(device, chunk.second);
    m_entries.erase(it);
}

void TerrainVegetationGpu::releaseAll(RHI::RenderDevice* device) {
    for (auto& entry : m_entries) {
        for (auto& chunk : entry.second.chunks) releaseChunk(device, chunk.second);
    }
    m_entries.clear();
}

void TerrainVegetationGpu::collect(RHI::RenderDevice* device, ECS::Entity entity, const ECS::TerrainComponent& terrain,
                                   Terrain::TerrainCache& cache, const Mat4& world, const Vec3& cameraPos,
                                   const Spatial::Frustum& frustum, std::vector<VegetationBatch>& out, VegetationFrameStats* stats,
                                   u32 buildBudget) {
    using Terrain::TerrainVegetation;
    if (!device) return;
    const TerrainVegetation* veg = cache.vegetationFor(entity);
    Entry& entry = m_entries[entity.id()];

    // What changed since the last frame: whole terrain, some chunks, or the terrain moved.
    std::vector<u64> dirty;
    bool all = false;
    cache.takeVegetationDirty(entity, dirty, all);
    if (entry.hasWorld && std::memcmp(entry.world.data(), world.data(), sizeof(f32) * 16) != 0) all = true;
    entry.world = world;
    entry.hasWorld = true;
    if (all) {
        for (auto& chunk : entry.chunks) releaseChunk(device, chunk.second);
        entry.chunks.clear();
    } else {
        for (u64 key : dirty) {
            auto it = entry.chunks.find(key);
            if (it == entry.chunks.end()) continue;
            releaseChunk(device, it->second);
            entry.chunks.erase(it);
        }
    }
    if (!veg || veg->species().empty()) {
        for (auto& chunk : entry.chunks) releaseChunk(device, chunk.second);
        entry.chunks.clear();
        return;
    }

    f32 farthest = 0.0f;
    for (const Terrain::VegetationSpecies& s : veg->species()) {
        if (s.visible && !s.meshes.empty()) farthest = std::max(farthest, s.maxDistance);
    }
    if (farthest <= 0.0f) return;

    const f32 halfX = terrain.worldSizeX * 0.5f, halfZ = terrain.worldSizeZ * 0.5f;
    const i32 x0 = TerrainVegetation::chunkOf(-halfX), x1 = TerrainVegetation::chunkOf(halfX);
    const i32 z0 = TerrainVegetation::chunkOf(-halfZ), z1 = TerrainVegetation::chunkOf(halfZ);
    const f32 chunk = TerrainVegetation::kChunkM;

    struct Wanted {
        i32 cx, cz;
        f32 distance;
    };
    std::vector<Wanted> waiting;
    for (i32 cz = z0; cz <= z1; ++cz) {
        for (i32 cx = x0; cx <= x1; ++cx) {
            // The chunk's box in world space (the ground and the tallest plant over it), by its eight corners.
            const f32 lx[2] = {static_cast<f32>(cx) * chunk, static_cast<f32>(cx + 1) * chunk};
            const f32 lz[2] = {static_cast<f32>(cz) * chunk, static_cast<f32>(cz + 1) * chunk};
            Spatial::AABB3D box;
            box.min = Vec3(1e30f, 1e30f, 1e30f);
            box.max = Vec3(-1e30f, -1e30f, -1e30f);
            for (int ix = 0; ix < 2; ++ix) {
                for (int iz = 0; iz < 2; ++iz) {
                    for (int iy = 0; iy < 2; ++iy) {
                        const Vec3 p = world.transformPoint(Vec3(lx[ix], iy ? terrain.maxHeight + 40.0f : -10.0f, lz[iz]));
                        box.min = Vec3(std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z));
                        box.max = Vec3(std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z));
                    }
                }
            }
            // The nearest point of the box: past the farthest species' reach nothing of it is drawn.
            const Vec3 nearest(std::clamp(cameraPos.x, box.min.x, box.max.x), std::clamp(cameraPos.y, box.min.y, box.max.y),
                               std::clamp(cameraPos.z, box.min.z, box.max.z));
            const f32 dx = nearest.x - cameraPos.x, dy = nearest.y - cameraPos.y, dz = nearest.z - cameraPos.z;
            const f32 distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > farthest) continue;
            if (!frustum.intersects(box)) continue;
            if (stats) ++stats->chunksVisible;
            const u64 key = TerrainVegetation::chunkKey(cx, cz);
            auto found = entry.chunks.find(key);
            if (found == entry.chunks.end() || !found->second.built) {
                waiting.push_back({cx, cz, distance});
                continue;
            }
            for (const VegetationBatch& batch : found->second.batches) {
                const Terrain::VegetationSpecies& s = veg->species()[batch.species];
                if (distance > s.maxDistance) continue;
                VegetationBatch visible = batch;
                visible.distance = distance;
                out.push_back(visible);
                if (stats) {
                    ++stats->batches;
                    stats->instances += batch.count;
                }
            }
        }
    }

    // Build the nearest waiting chunks, a few a frame.
    std::sort(waiting.begin(), waiting.end(), [](const Wanted& a, const Wanted& b) { return a.distance < b.distance; });
    const Terrain::VegetationField field = cache.vegetationField(entity, terrain);
    std::vector<Terrain::VegetationResolved> resolved;
    u32 built = 0;
    for (const Wanted& wanted : waiting) {
        if (built >= buildBudget) break;
        ++built;
        Chunk& made = entry.chunks[TerrainVegetation::chunkKey(wanted.cx, wanted.cz)];
        releaseChunk(device, made);
        resolved.clear();
        veg->resolveChunk(wanted.cx, wanted.cz, field, resolved);
        usize i = 0;
        while (i < resolved.size()) {
            usize j = i;
            while (j < resolved.size() && resolved[j].species == resolved[i].species && resolved[j].variant == resolved[i].variant) ++j;
            const u32 count = static_cast<u32>(j - i);
            std::vector<f32> matrices;
            matrices.reserve(static_cast<usize>(count) * 16);
            Mat4 first = Mat4::identity();
            for (usize k = i; k < j; ++k) {
                const Terrain::VegetationResolved& r = resolved[k];
                const Mat4 model = world * instanceMatrix(r.x, r.y, r.z, r.up, r.yaw, r.scale);
                if (k == i) first = model;
                matrices.insert(matrices.end(), model.data(), model.data() + 16);
            }
            RHI::BufferDesc desc;
            desc.size = static_cast<u64>(matrices.size()) * sizeof(f32);
            desc.name = "TerrainVegetation";
            RHI::Buffer* buffer = device->createBuffer(desc, RHI::BufferUsage::Vertex);
            if (buffer && device->uploadBuffer(buffer, matrices.data(), desc.size)) {
                VegetationBatch batch;
                batch.buffer = buffer;
                batch.count = count;
                batch.species = resolved[i].species;
                batch.variant = resolved[i].variant;
                batch.first = first;
                made.batches.push_back(batch);
            } else if (buffer) {
                device->destroyBuffer(buffer);
            }
            i = j;
        }
        made.built = true;
        if (stats) ++stats->chunksBuilt;
    }
    if (stats) stats->chunksWaiting += static_cast<u32>(waiting.size() > built ? waiting.size() - built : 0);
}

}  // namespace Caffeine::Render
#endif
