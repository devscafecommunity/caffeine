#include "terrain/TerrainCache.hpp"

#include "ecs/MeshComponents.hpp"
#include "terrain/TerrainMeshBuilder.hpp"
#include "terrain/TerrainResolution.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#ifdef CF_HAS_SDL3
#include "render/TerrainVegetationGpu.hpp"
#endif
#include "terrain/TerrainCollisionMeshBuilder.hpp"
#include "assets/MeshCache.hpp"
#include "core/WorldUnits.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

void copyPath(char* dest, size_t destSize, const std::string& path) {
    if (!dest || destSize == 0) return;
    std::strncpy(dest, path.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

constexpr const char* kDefaultTexture =
    "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_Color.jpg";
constexpr const char* kDefaultNormal =
    "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_NormalGL.jpg";
constexpr const char* kLegacyLayers[ECS::kTerrainSplatLayerCount] = {
    "kenney_prototype-textures/PNG/Green/texture_07.png",
    "kenney_prototype-textures/PNG/Dark/texture_07.png",
    "kenney_prototype-textures/PNG/Orange/texture_07.png",
    "kenney_prototype-textures/PNG/Light/texture_07.png",
};
constexpr const char* kDefaultLayers[ECS::kTerrainSplatLayerCount] = {
    "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground108_1K-JPG-dirt-with-rocks/Ground108_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground079L_1K-JPG-dirt/Ground079L_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground051_1K-JPG-mud/Ground051_1K-JPG_Color.jpg",
};

bool hasLegacyKenneyLayerSet(const ECS::TerrainComponent& terrain) {
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        if (std::strcmp(terrain.splatLayerPaths[i], kLegacyLayers[i]) != 0) return false;
    }
    return true;
}

bool isUniformLayer3(const TerrainSplatmap& splatmap) {
    if (splatmap.empty()) return false;
    for (const Vec4& weights : splatmap.weights()) {
        if (std::abs(weights.x) > 1e-4f || std::abs(weights.y) > 1e-4f ||
            std::abs(weights.z) > 1e-4f || std::abs(weights.w - 1.0f) > 1e-4f) {
            return false;
        }
    }
    return true;
}

}  // namespace

void TerrainCache::repairTexturePaths(ECS::TerrainComponent& terrain) {
    copyPath(terrain.texturePath, sizeof(terrain.texturePath),
             Assets::MeshCache::normalizeTexturePath(terrain.texturePath));
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        copyPath(terrain.splatLayerPaths[i], sizeof(terrain.splatLayerPaths[i]),
                 Assets::MeshCache::normalizeTexturePath(terrain.splatLayerPaths[i]));
    }

    if (hasLegacyKenneyLayerSet(terrain)) {
        copyPath(terrain.texturePath, sizeof(terrain.texturePath), kDefaultTexture);
        if (terrain.normalMapPath[0] == '\0') {
            copyPath(terrain.normalMapPath, sizeof(terrain.normalMapPath), kDefaultNormal);
        }
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            copyPath(terrain.splatLayerPaths[i], sizeof(terrain.splatLayerPaths[i]),
                     kDefaultLayers[i]);
        }
    }
}

TerrainCache& TerrainCache::instance() {
    static TerrainCache cache;
    return cache;
}

TerrainCache::TerrainEntry& TerrainCache::ensureEntry(ECS::Entity entity) {
    return m_entries[entity.id()];
}

void TerrainCache::syncTextureToFilter(ECS::World& world, ECS::Entity entity,
                                       const ECS::TerrainComponent& terrain) {
    auto* filter = world.get<ECS::MeshFilterComponent>(entity);
    if (!filter) return;
    if (terrain.useSplatmap) {
        filter->customTexturePath = terrain.splatLayerPaths[0];
    } else {
        filter->customTexturePath = terrain.texturePath;
    }
}

void TerrainCache::rebuildCollisionMesh(TerrainEntry& entry, const ECS::TerrainComponent& component) {
    if (!component.buildCollisionMesh || entry.heightmap.empty()) {
        entry.collisionMesh.reset();
        return;
    }
    entry.collisionMesh = std::make_unique<Assets::Mesh3D>(
        TerrainCollisionMeshBuilder::build(entry.heightmap, component));
}

void TerrainCache::initializeEntity(ECS::World& world, ECS::Entity entity) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;

    repairTexturePaths(*terrain);
    TerrainEntry& entry = ensureEntry(entity);
    entry.heightmap.resize(terrain->resolutionX, terrain->resolutionZ);
    entry.heightmap.fill(0.0f);
    entry.splatmap.resize(splatResolutionX(*terrain), splatResolutionZ(*terrain));
    entry.splatmap.fillLayer(0, 1.0f);
    terrain->dataRevision = 1;
    terrain->meshRevision = 0;
    terrain->splatRevision = 1;
    rebuildMesh(entity, entry, *terrain);

    if (!world.has<ECS::MeshFilterComponent>(entity)) {
        ECS::MeshFilterComponent filter;
        filter.primitive = ECS::MeshPrimitive::Custom;
        world.add<ECS::MeshFilterComponent>(entity, filter);
    }
    if (!world.has<ECS::MeshRendererComponent>(entity)) {
        ECS::MeshRendererComponent renderer;
        renderer.castShadows = terrain->castShadows;
        renderer.receiveShadows = terrain->receiveShadows;
        world.add<ECS::MeshRendererComponent>(entity, renderer);
    }

    syncTextureToFilter(world, entity, *terrain);
}

#ifdef CF_HAS_SDL3
void TerrainCache::releaseMeshGpu(Assets::Mesh3D& mesh) {
    if (!m_gpuDevice) {
        mesh.vertexBuffer = nullptr;
        mesh.indexBuffer = nullptr;
        return;
    }
    if (mesh.vertexBuffer) {
        m_gpuDevice->destroyBuffer(mesh.vertexBuffer);
        mesh.vertexBuffer = nullptr;
    }
    if (mesh.indexBuffer) {
        m_gpuDevice->destroyBuffer(mesh.indexBuffer);
        mesh.indexBuffer = nullptr;
    }
}

void TerrainCache::releaseChunkGpu(TerrainChunk& chunk) {
    for (auto& lodMesh : chunk.lodMeshes) {
        if (lodMesh) releaseMeshGpu(*lodMesh);
    }
}

void TerrainCache::releaseChunksGpu(std::vector<TerrainChunk>& chunks) {
    for (TerrainChunk& chunk : chunks) {
        releaseChunkGpu(chunk);
    }
}

void TerrainCache::releaseEntryGpu(TerrainEntry& entry) {
    releaseChunksGpu(entry.chunks);
    if (entry.mesh) releaseMeshGpu(*entry.mesh);
}
#endif

void TerrainCache::rebuildMesh(ECS::Entity entity, TerrainEntry& entry,
                               ECS::TerrainComponent& component) {
    entry.useChunks = component.useChunks;
#ifdef CF_HAS_SDL3
    releaseEntryGpu(entry);
#endif
    if (entry.useChunks) {
        TerrainLodSystem::rebuildChunks(entry.chunks, entry.heightmap, component);
        entry.chunkActiveLods.assign(entry.chunks.size(), 0);
        entry.mesh.reset();
    } else {
        entry.chunks.clear();
        entry.chunkActiveLods.clear();
        entry.mesh = std::make_unique<Assets::Mesh3D>(
            TerrainMeshBuilder::build(entry.heightmap, component));
    }

    component.meshRevision = component.dataRevision;
    entry.builtRevision = component.dataRevision;
    rebuildCollisionMesh(entry, component);
    markTilesEdited(entry, component, nullptr);  // the heights may all have changed (a generator, a load, a resample)
    (void)entity;
}

void TerrainCache::markTilesEdited(TerrainEntry& entry, const ECS::TerrainComponent& settings,
                                   const TerrainVertexBounds* region) {
    const TileGrid grid(entry.heightmap.resolutionX(), entry.heightmap.resolutionZ(), settings.worldSizeX,
                        settings.worldSizeZ);
    if (!grid.valid()) return;
    if (entry.tileRevision.size() != grid.tileCount()) entry.tileRevision.assign(grid.tileCount(), 0);
    if (!region || !region->valid) {
        for (u64& revision : entry.tileRevision) revision = ++entry.editSerial;
        return;
    }
    // A changed vertex also moves the triangles of the next tile: the tiles' mesh regions, as the dirty set does.
    DirtyTiles touched(grid);
    touched.mark(*region);
    for (const TileCoord tile : touched.tiles()) entry.tileRevision[grid.index(tile)] = ++entry.editSerial;
}

u32 TerrainCache::giStepFor(u32 resolutionX, u32 resolutionZ, f64 sizeX, f64 sizeZ) {
    constexpr f64 kTriangleBudget = 400000.0;  // of the voxeliser's million: the rest is for the scene's objects
    const f64 cellsX = std::max(1u, resolutionX - 1), cellsZ = std::max(1u, resolutionZ - 1);
    const f64 cellX = sizeX / cellsX, cellZ = sizeZ / cellsZ;
    // The voxel volume resolves about a metre at best: finer cells would only cost triangles.
    f64 byDetail = std::max(1.0 / std::max(cellX, 1e-6), 1.0 / std::max(cellZ, 1e-6));
    f64 byBudget = std::sqrt(std::max(1.0, 2.0 * cellsX * cellsZ / kTriangleBudget));
    u32 step = 1;
    while (step < 64 && static_cast<f64>(step) < std::max(byDetail, byBudget)) step *= 2;
    return step;
}

const std::vector<TerrainGiTile>& TerrainCache::giTilesFor(ECS::Entity entity,
                                                           const ECS::TerrainComponent& settings) {
    static const std::vector<TerrainGiTile> none;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || it->second.heightmap.empty()) return none;
    TerrainEntry& entry = it->second;
    const u32 resX = entry.heightmap.resolutionX(), resZ = entry.heightmap.resolutionZ();
    const TileGrid grid(resX, resZ, settings.worldSizeX, settings.worldSizeZ);
    if (!grid.valid()) return none;
    const u32 step = giStepFor(resX, resZ, settings.worldSizeX, settings.worldSizeZ);

    // The tiles' shape depends on the grid and the step; when it changes they all start again.
    u64 signature = 1469598103934665603ull;
    for (const u64 part : {static_cast<u64>(resX), static_cast<u64>(resZ),
                           static_cast<u64>(static_cast<f32>(settings.worldSizeX) * 1000.0f),
                           static_cast<u64>(static_cast<f32>(settings.worldSizeZ) * 1000.0f),
                           static_cast<u64>(settings.maxHeight * 1000.0f), static_cast<u64>(step)}) {
        signature = (signature ^ part) * 1099511628211ull;
    }
    if (entry.tileRevision.size() != grid.tileCount()) entry.tileRevision.assign(grid.tileCount(), ++entry.editSerial);
    if (entry.giSignature != signature || entry.giTiles.size() != grid.tileCount()) {
        entry.giSignature = signature;
        entry.giTiles.assign(grid.tileCount(), TerrainGiTile{});
        for (u32 i = 0; i < grid.tileCount(); ++i) entry.giTiles[i].index = i;
        for (u64& revision : entry.tileRevision) revision = ++entry.editSerial;
    }

    for (u32 i = 0; i < grid.tileCount(); ++i) {
        TerrainGiTile& tile = entry.giTiles[i];
        if (tile.revision == entry.tileRevision[i]) continue;
        const TerrainVertexBounds owned = grid.owned(grid.coordOf(i));
        // One vertex past the tile joins it to the next; the step divides the 64-cell tile.
        const u32 endX = std::min(owned.minX + kTileCells, resX - 1);
        const u32 endZ = std::min(owned.minZ + kTileCells, resZ - 1);
        tile.mesh = TerrainMeshBuilder::buildRegion(entry.heightmap, settings, owned.minX, owned.minZ, endX, endZ, step);
        tile.lo = tile.mesh.bounds.min;
        tile.hi = tile.mesh.bounds.max;
        tile.revision = entry.tileRevision[i];
    }
    return entry.giTiles;
}

void TerrainCache::rebuildRegion(ECS::Entity entity,
                                 const ECS::TerrainComponent& settings,
                                 u32 minVertexX, u32 minVertexZ,
                                 u32 maxVertexX, u32 maxVertexZ) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;

    TerrainEntry& entry = it->second;
    if (entry.heightmap.empty()) return;
    {
        // The global illumination follows by tile: only the tiles this region touches get a new revision.
        TerrainVertexBounds region;
        region.minX = minVertexX;
        region.minZ = minVertexZ;
        region.maxX = maxVertexX;
        region.maxZ = maxVertexZ;
        region.valid = true;
        markTilesEdited(entry, settings, &region);
    }

    if (entry.useChunks && !entry.chunks.empty()) {
#ifdef CF_HAS_SDL3
        for (TerrainChunk& chunk : entry.chunks) {
            const bool overlapsX = chunk.startVertexX <= maxVertexX && minVertexX <= chunk.endVertexX;
            const bool overlapsZ = chunk.startVertexZ <= maxVertexZ && minVertexZ <= chunk.endVertexZ;
            if (overlapsX && overlapsZ) {
                releaseChunkGpu(chunk);
            }
        }
#endif
        TerrainLodSystem::rebuildChunksInRegion(entry.chunks, entry.heightmap, settings,
                                                minVertexX, minVertexZ, maxVertexX, maxVertexZ);
        return;
    }

    if (entry.mesh) {
#ifdef CF_HAS_SDL3
        releaseMeshGpu(*entry.mesh);
#endif
        *entry.mesh = TerrainMeshBuilder::build(entry.heightmap, settings);
    }
}

void TerrainCache::commitHeightEdit(ECS::Entity entity, ECS::TerrainComponent& settings,
                                    const TerrainVertexBounds& region, bool final) {
    if (!region.valid) return;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || it->second.heightmap.empty()) return;
    TerrainEntry& entry = it->second;
    ++settings.dataRevision;
    if (entry.hasVegetation && !entry.vegetationDirtyAll) {
        // The ground moved under the vegetation standing on it: those chunks are drawn again.
        const f32 stepX = settings.worldSizeX / static_cast<f32>(std::max(settings.resolutionX, 2u) - 1);
        const f32 stepZ = settings.worldSizeZ / static_cast<f32>(std::max(settings.resolutionZ, 2u) - 1);
        const f32 x0 = static_cast<f32>(region.minX) * stepX - settings.worldSizeX * 0.5f - stepX;
        const f32 x1 = static_cast<f32>(region.maxX) * stepX - settings.worldSizeX * 0.5f + stepX;
        const f32 z0 = static_cast<f32>(region.minZ) * stepZ - settings.worldSizeZ * 0.5f - stepZ;
        const f32 z1 = static_cast<f32>(region.maxZ) * stepZ - settings.worldSizeZ * 0.5f + stepZ;
        const i32 cx0 = TerrainVegetation::chunkOf(x0), cx1 = TerrainVegetation::chunkOf(x1);
        const i32 cz0 = TerrainVegetation::chunkOf(z0), cz1 = TerrainVegetation::chunkOf(z1);
        if (static_cast<i64>(cx1 - cx0 + 1) * (cz1 - cz0 + 1) > 4096) {
            entry.vegetationDirtyAll = true;
        } else {
            for (i32 cz = cz0; cz <= cz1; ++cz) {
                for (i32 cx = cx0; cx <= cx1; ++cx) entry.vegetationDirty.insert(TerrainVegetation::chunkKey(cx, cz));
            }
        }
        ++settings.vegetationRevision;
    }
    rebuildRegion(entity, settings, region.minX, region.minZ, region.maxX, region.maxZ);
    settings.meshRevision = settings.dataRevision;
    entry.builtRevision = settings.dataRevision;
    if (final) rebuildCollisionMesh(entry, settings);
}

void TerrainCache::syncEntity(ECS::World& world, ECS::Entity entity) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;

    const std::string textureBefore = terrain->texturePath;
    repairTexturePaths(*terrain);
#ifdef CF_HAS_SDL3
    if (textureBefore != terrain->texturePath) {
        TerrainGpuTextureCache::instance().invalidateEntity(entity, nullptr);
    }
#endif

    TerrainEntry& entry = ensureEntry(entity);
    const u32 targetSplatX = splatResolutionX(*terrain);
    const u32 targetSplatZ = splatResolutionZ(*terrain);
    bool resolutionChanged = false;

    if (entry.heightmap.resolutionX() != terrain->resolutionX ||
        entry.heightmap.resolutionZ() != terrain->resolutionZ ||
        entry.heightmap.empty()) {
        const bool resample = !entry.heightmap.empty();
        entry.heightmap.resize(terrain->resolutionX, terrain->resolutionZ, resample);
        if (!resample) {
            entry.heightmap.fill(0.0f);
        }
        resolutionChanged = true;
    }

    if (entry.splatmap.resolutionX() != targetSplatX ||
        entry.splatmap.resolutionZ() != targetSplatZ ||
        entry.splatmap.empty()) {
        const bool resample = !entry.splatmap.empty();
        entry.splatmap.resize(targetSplatX, targetSplatZ, resample);
        if (!resample) {
            entry.splatmap.fillLayer(0, 1.0f);
        }
        terrain->splatRevision++;
        resolutionChanged = true;
    }

    if (terrain->useLayers) {
        // The layer map follows the splat resolution: a resized terrain keeps its painting (nearest texel).
        if (entry.layers.empty() || entry.layers.resolutionX() != targetSplatX || entry.layers.resolutionZ() != targetSplatZ) {
            const bool keep = !entry.layers.empty();
            if (!keep) entry.layers = TerrainLayerMap::fromSplatmap(entry.splatmap);
            else entry.layers.resize(targetSplatX, targetSplatZ, true);
            terrain->layerRevision++;
        }
        if (entry.palette.empty()) entry.palette = TerrainPalette::fromLegacyLayers(*terrain);
    }

    if (resolutionChanged || terrain->meshRevision != terrain->dataRevision ||
        (!entry.mesh && entry.chunks.empty())) {
        rebuildMesh(entity, entry, *terrain);
    }

    if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
        renderer->castShadows = terrain->castShadows;
        renderer->receiveShadows = terrain->receiveShadows;
    }

    syncTextureToFilter(world, entity, *terrain);
}

void TerrainCache::removeEntity(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
#ifdef CF_HAS_SDL3
    releaseEntryGpu(it->second);
    TerrainGpuTextureCache::instance().removeEntity(entity, m_gpuDevice);
    Render::TerrainVegetationGpu::instance().removeEntity(entity, m_gpuDevice);
#endif
    m_entries.erase(it);
}

void TerrainCache::clear() {
#ifdef CF_HAS_SDL3
    for (auto& [_, entry] : m_entries) {
        releaseEntryGpu(entry);
    }
    TerrainGpuTextureCache::instance().releaseAll(m_gpuDevice);
#endif
    m_entries.clear();
}

TerrainHeightmap* TerrainCache::heightmapFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.heightmap : nullptr;
}

const TerrainHeightmap* TerrainCache::heightmapFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.heightmap : nullptr;
}

TerrainVegetation* TerrainCache::vegetationFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() && it->second.hasVegetation ? &it->second.vegetation : nullptr;
}

const TerrainVegetation* TerrainCache::vegetationFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() && it->second.hasVegetation ? &it->second.vegetation : nullptr;
}

bool TerrainCache::enableVegetation(ECS::Entity entity, ECS::TerrainComponent& terrain) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return false;
    TerrainEntry& entry = it->second;
    if (!terrain.useVegetation || !entry.hasVegetation) {
        // Turning vegetation on starts with an empty store, never with one an earlier terrain of the same id left behind.
        entry.vegetation = TerrainVegetation();
        entry.hasVegetation = true;
    }
    entry.vegetationDirtyAll = true;
    terrain.useVegetation = true;
    ++terrain.vegetationRevision;
    return true;
}

VegetationField TerrainCache::vegetationField(ECS::Entity entity, const ECS::TerrainComponent& terrain) const {
    VegetationField field;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return field;
    const TerrainEntry* entry = &it->second;
    const f32 sizeX = std::max(terrain.worldSizeX, 0.001f), sizeZ = std::max(terrain.worldSizeZ, 0.001f);
    const f32 maxHeight = std::max(terrain.maxHeight, 0.001f);
    const f32 stepX = sizeX / static_cast<f32>(std::max(terrain.resolutionX, 2u) - 1);
    const f32 stepZ = sizeZ / static_cast<f32>(std::max(terrain.resolutionZ, 2u) - 1);
    auto heightAt = [entry, sizeX, sizeZ, maxHeight](f32 x, f32 z) {
        const f32 u = std::clamp((x + sizeX * 0.5f) / sizeX, 0.0f, 1.0f);
        const f32 v = std::clamp((z + sizeZ * 0.5f) / sizeZ, 0.0f, 1.0f);
        return entry->heightmap.sampleBilinear(u, v) * maxHeight;
    };
    field.halfX = sizeX * 0.5f;
    field.halfZ = sizeZ * 0.5f;
    field.height = heightAt;
    field.normal = [heightAt, stepX, stepZ](f32 x, f32 z) {
        const f32 dx = heightAt(x + stepX, z) - heightAt(x - stepX, z);
        const f32 dz = heightAt(x, z + stepZ) - heightAt(x, z - stepZ);
        const Vec3 n(-dx / (2.0f * stepX), 1.0f, -dz / (2.0f * stepZ));
        const f32 length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return Vec3(n.x / length, n.y / length, n.z / length);
    };
    const TerrainFrame frame{&entry->heightmap, sizeX, sizeZ, maxHeight};
    field.mask = [entry, frame, sizeX, sizeZ](const std::string& mask, f32 x, f32 z, f32& value) {
        return entry->masks.sample(frame, mask, x + sizeX * 0.5f, z + sizeZ * 0.5f, value);
    };
    field.density = [entry, frame, sizeX, sizeZ](u32 index, f32 x, f32 z) {
        if (!entry->hasVegetation || index >= entry->vegetation.species().size()) return 1.0f;
        f32 weight = 1.0f;
        for (const BiomeTerm& term : entry->vegetation.species()[index].rules) {
            f32 value = 0.0f;
            // A mask that does not exist suits nowhere (the same rule as the biome table).
            if (!entry->masks.sample(frame, term.mask, x + sizeX * 0.5f, z + sizeZ * 0.5f, value)) return 0.0f;
            weight *= term.weight(value);
            if (weight <= 0.0f) return 0.0f;
        }
        return weight;
    };
    return field;
}

void TerrainCache::commitVegetationEdit(ECS::Entity entity, ECS::TerrainComponent& terrain, const VegetationRecord& record) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    TerrainEntry& entry = it->second;
    ++terrain.vegetationRevision;
    if (record.wholeTerrain()) {
        entry.vegetationDirtyAll = true;
        return;
    }
    if (!record.hasChunks) return;
    if (static_cast<i64>(record.maxChunkX - record.minChunkX + 1) * (record.maxChunkZ - record.minChunkZ + 1) > 4096) {
        entry.vegetationDirtyAll = true;
        return;
    }
    // A conflict can reach into the next chunk (a tree takes the spot of a bush across the border): one chunk around.
    for (i32 cz = record.minChunkZ - 1; cz <= record.maxChunkZ + 1; ++cz) {
        for (i32 cx = record.minChunkX - 1; cx <= record.maxChunkX + 1; ++cx) entry.vegetationDirty.insert(TerrainVegetation::chunkKey(cx, cz));
    }
}

void TerrainCache::takeVegetationDirty(ECS::Entity entity, std::vector<u64>& chunks, bool& all) {
    chunks.clear();
    all = false;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    TerrainEntry& entry = it->second;
    all = entry.vegetationDirtyAll;
    if (!all) chunks.assign(entry.vegetationDirty.begin(), entry.vegetationDirty.end());
    entry.vegetationDirty.clear();
    entry.vegetationDirtyAll = false;
}

bool TerrainCache::saveVegetation(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                                  std::string* error) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || !it->second.hasVegetation) {
        if (error) *error = "the terrain has no vegetation to save";
        return false;
    }
    if (terrain.vegetationPath[0] == '\0') {
        std::filesystem::path base = terrain.terrainDataPath[0] != '\0' ? std::filesystem::path(terrain.terrainDataPath)
                                                                        : std::filesystem::path("terrain/Terrain_" + std::to_string(entity.id()));
        base.replace_extension();
        const std::string relative = base.string() + ".terrainveg";
        std::memset(terrain.vegetationPath, 0, sizeof(terrain.vegetationPath));
        std::memcpy(terrain.vegetationPath, relative.c_str(), std::min(relative.size(), sizeof(terrain.vegetationPath) - 1));
    }
    const std::filesystem::path file = projectRoot / terrain.vegetationPath;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return it->second.vegetation.save(file.string(), error);
}

bool TerrainCache::loadVegetation(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                                  std::string* error) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || terrain.vegetationPath[0] == '\0') return false;
    TerrainVegetation loaded;
    if (!TerrainVegetation::load((projectRoot / terrain.vegetationPath).string(), loaded, error)) return false;
    it->second.vegetation = std::move(loaded);
    it->second.hasVegetation = true;
    it->second.vegetationDirtyAll = true;
    ++terrain.vegetationRevision;
    return true;
}

TerrainPalette* TerrainCache::paletteFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.palette : nullptr;
}

const TerrainPalette* TerrainCache::paletteFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.palette : nullptr;
}

TerrainLayerMap* TerrainCache::layerMapFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.layers : nullptr;
}

const TerrainLayerMap* TerrainCache::layerMapFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.layers : nullptr;
}

bool TerrainCache::enableLayers(ECS::Entity entity, ECS::TerrainComponent& terrain) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return false;
    TerrainEntry& entry = it->second;
    if (!terrain.useLayers) {
        // Turning layers on starts from what the terrain shows now (its four layers and splat map): never from layers an
        // earlier terrain with the same id left in the cache.
        entry.palette = TerrainPalette::fromLegacyLayers(terrain);
        entry.layers = TerrainLayerMap::fromSplatmap(entry.splatmap);
        entry.layerDirty = {};
        entry.layerDirtyAll = true;
    } else {
        if (entry.palette.empty()) entry.palette = TerrainPalette::fromLegacyLayers(terrain);
        if (entry.layers.empty()) entry.layers = TerrainLayerMap::fromSplatmap(entry.splatmap);
    }
    terrain.useLayers = true;
    terrain.layerRevision++;
    return true;
}

void TerrainCache::disableLayers(ECS::Entity entity, ECS::TerrainComponent& terrain) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    TerrainEntry& entry = it->second;
    if (!entry.layers.empty()) {
        entry.splatmap = entry.layers.toSplatmap();
        terrain.splatRevision++;
    }
    // The palette and the map stay in the cache: an undo that turns the layers on again finds them (a scene restore sets
    // them aside and hands them back). Turning layers on by hand starts over from the four-layer view (`enableLayers`).
    terrain.useLayers = false;
    terrain.layerRevision++;
}

TerrainCache::LayerState TerrainCache::takeLayers(ECS::Entity entity) {
    LayerState state;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return state;
    state.palette = std::move(it->second.palette);
    state.layers = std::move(it->second.layers);
    it->second.palette = TerrainPalette();
    it->second.layers = TerrainLayerMap();
    if (it->second.hasVegetation) {
        state.vegetation = std::move(it->second.vegetation);
        state.hasVegetation = true;
        it->second.vegetation = TerrainVegetation();
        it->second.hasVegetation = false;
    }
    return state;
}

void TerrainCache::adoptLayers(ECS::Entity entity, LayerState&& state) {
    TerrainEntry& entry = ensureEntry(entity);
    entry.palette = std::move(state.palette);
    entry.layers = std::move(state.layers);
    entry.layerDirtyAll = true;
    if (state.hasVegetation) {
        entry.vegetation = std::move(state.vegetation);
        entry.hasVegetation = true;
        entry.vegetationDirtyAll = true;
    }
}

void TerrainCache::commitLayerEdit(ECS::Entity entity, ECS::TerrainComponent& terrain, const LayerBounds& region) {
    terrain.layerRevision++;
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    TerrainEntry& entry = it->second;
    if (!region.valid) {
        entry.layerDirtyAll = true;
    } else if (!entry.layerDirty.valid) {
        entry.layerDirty = region;
    } else {
        entry.layerDirty.minX = std::min(entry.layerDirty.minX, region.minX);
        entry.layerDirty.minZ = std::min(entry.layerDirty.minZ, region.minZ);
        entry.layerDirty.maxX = std::max(entry.layerDirty.maxX, region.maxX);
        entry.layerDirty.maxZ = std::max(entry.layerDirty.maxZ, region.maxZ);
    }
}

void TerrainCache::commitMaskEdit(ECS::Entity entity, ECS::TerrainComponent& terrain, const LayerBounds& region, u32 resolutionX,
                                  u32 resolutionZ) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || !it->second.hasVegetation) return;
    TerrainEntry& entry = it->second;
    ++terrain.vegetationRevision;
    if (!region.valid || resolutionX < 2 || resolutionZ < 2) {
        entry.vegetationDirtyAll = true;
        return;
    }
    if (entry.vegetationDirtyAll) return;
    const f32 stepX = terrain.worldSizeX / static_cast<f32>(resolutionX - 1);
    const f32 stepZ = terrain.worldSizeZ / static_cast<f32>(resolutionZ - 1);
    // Masks are sampled bilinearly: a texel reaches one texel past itself.
    const f32 x0 = (static_cast<f32>(region.minX) - 1.0f) * stepX - terrain.worldSizeX * 0.5f;
    const f32 x1 = (static_cast<f32>(region.maxX) + 1.0f) * stepX - terrain.worldSizeX * 0.5f;
    const f32 z0 = (static_cast<f32>(region.minZ) - 1.0f) * stepZ - terrain.worldSizeZ * 0.5f;
    const f32 z1 = (static_cast<f32>(region.maxZ) + 1.0f) * stepZ - terrain.worldSizeZ * 0.5f;
    const i32 cx0 = TerrainVegetation::chunkOf(x0), cx1 = TerrainVegetation::chunkOf(x1);
    const i32 cz0 = TerrainVegetation::chunkOf(z0), cz1 = TerrainVegetation::chunkOf(z1);
    if (static_cast<i64>(cx1 - cx0 + 1) * (cz1 - cz0 + 1) > 4096) {
        entry.vegetationDirtyAll = true;
        return;
    }
    // Conflicts can reach into the next chunk, as for any vegetation edit: one chunk around.
    for (i32 cz = cz0 - 1; cz <= cz1 + 1; ++cz) {
        for (i32 cx = cx0 - 1; cx <= cx1 + 1; ++cx) entry.vegetationDirty.insert(TerrainVegetation::chunkKey(cx, cz));
    }
}

namespace {

void copyRelative(char* destination, usize capacity, const std::string& text) {
    std::memset(destination, 0, capacity);
    std::memcpy(destination, text.c_str(), std::min(text.size(), capacity - 1));
}

}  // namespace

bool TerrainCache::saveLayers(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                              std::string* error) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || it->second.layers.empty()) {
        if (error) *error = "the terrain has no layers to save";
        return false;
    }
    std::filesystem::path base = terrain.terrainDataPath[0] != '\0' ? std::filesystem::path(terrain.terrainDataPath)
                                                                    : std::filesystem::path("terrain/Terrain_" + std::to_string(entity.id()));
    base.replace_extension();
    if (terrain.palettePath[0] == '\0') copyRelative(terrain.palettePath, sizeof(terrain.palettePath), (base.string() + ".terrainpalette"));
    if (terrain.layerMapPath[0] == '\0') copyRelative(terrain.layerMapPath, sizeof(terrain.layerMapPath), (base.string() + ".terrainlayers"));
    const std::filesystem::path paletteFile = projectRoot / terrain.palettePath;
    const std::filesystem::path mapFile = projectRoot / terrain.layerMapPath;
    std::error_code ec;
    std::filesystem::create_directories(paletteFile.parent_path(), ec);
    std::filesystem::create_directories(mapFile.parent_path(), ec);
    {
        std::ofstream out(paletteFile, std::ios::binary | std::ios::trunc);
        out << it->second.palette.toText();
        if (!out) {
            if (error) *error = "could not write " + paletteFile.string();
            return false;
        }
    }
    return it->second.layers.save(mapFile.string(), error);
}

bool TerrainCache::loadLayers(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                              std::string* error) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return false;
    TerrainEntry& entry = it->second;
    if (terrain.palettePath[0] != '\0') {
        std::ifstream in(projectRoot / terrain.palettePath, std::ios::binary);
        if (in) {
            std::stringstream text;
            text << in.rdbuf();
            TerrainPalette palette;
            if (!TerrainPalette::fromText(text.str(), palette, error)) return false;
            entry.palette = std::move(palette);
        }
    }
    if (entry.palette.empty()) entry.palette = TerrainPalette::fromLegacyLayers(terrain);
    bool haveMap = false;
    if (terrain.layerMapPath[0] != '\0') {
        const std::filesystem::path mapFile = projectRoot / terrain.layerMapPath;
        std::error_code ec;
        if (std::filesystem::exists(mapFile, ec)) {
            TerrainLayerMap map;
            if (!TerrainLayerMap::load(mapFile.string(), map, error)) return false;
            entry.layers = std::move(map);
            haveMap = true;
        }
    }
    if (!haveMap) entry.layers = TerrainLayerMap::fromSplatmap(entry.splatmap);
    terrain.layerRevision++;
    return true;
}

TerrainSplatmap* TerrainCache::splatmapFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.splatmap : nullptr;
}

const TerrainSplatmap* TerrainCache::splatmapFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.splatmap : nullptr;
}

TerrainMasks* TerrainCache::masksFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.masks : nullptr;
}

const TerrainMasks* TerrainCache::masksFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.masks : nullptr;
}

Assets::Mesh3D* TerrainCache::collisionMeshFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || !it->second.collisionMesh) return nullptr;
    return it->second.collisionMesh.get();
}

const Assets::Mesh3D* TerrainCache::collisionMeshFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || !it->second.collisionMesh) return nullptr;
    return it->second.collisionMesh.get();
}

Assets::Mesh3D* TerrainCache::meshFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return nullptr;
    if (it->second.mesh) return it->second.mesh.get();
    if (!it->second.chunks.empty() && it->second.chunks[0].lodMeshes[0]) {
        return it->second.chunks[0].lodMeshes[0].get();
    }
    return nullptr;
}

bool TerrainCache::saveTerrainFile(ECS::Entity entity, const std::filesystem::path& path,
                                   bool includeSplat) const {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return false;
    if (path.extension() == TerrainContainer::kExtension) {
        // The native container records the extent and height scale, which the cache does not hold: the caller's
        // terrain component does, so a scene that saves the native format goes through `saveNativeTerrainFile`.
        return false;
    }
    return TerrainSerializer::save(path, it->second.heightmap, it->second.splatmap, includeSplat,
                                   &it->second.masks);
}

bool TerrainCache::saveNativeTerrainFile(ECS::Entity entity, const ECS::TerrainComponent& terrain,
                                         const std::filesystem::path& path, bool includeSplat) const {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return false;
    const TerrainDimensions dimensions{static_cast<f64>(terrain.worldSizeX), static_cast<f64>(terrain.worldSizeZ),
                                       terrain.maxHeight};
    return TerrainContainer::save(path, it->second.heightmap, it->second.splatmap, includeSplat, &it->second.masks,
                                  dimensions);
}

bool TerrainCache::dataFileIsLegacy(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() && it->second.fromLegacy;
}

bool TerrainCache::convertToNative(ECS::Entity entity, ECS::TerrainComponent& terrain,
                                   const std::filesystem::path& projectRoot) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end() || it->second.heightmap.empty() || projectRoot.empty()) return false;
    if (terrain.terrainDataPath[0] == '\0') return false;
    const std::filesystem::path legacy = projectRoot / terrain.terrainDataPath;
    const std::filesystem::path native = nativeTerrainPath(legacy);
    if (!saveNativeTerrainFile(entity, terrain, native, terrain.useSplatmap)) return false;

    // What was written must read back as what is in memory, bit for bit, before the scene is pointed at it.
    TerrainHeightmap heights;
    TerrainSplatmap splat;
    TerrainMasks masks;
    bool hadSplat = false;
    if (!TerrainContainer::load(native, heights, splat, &hadSplat, &masks)) {
        std::error_code ec;
        std::filesystem::remove(native, ec);
        return false;
    }
    const TerrainEntry& entry = it->second;
    const bool sameSplat = !terrain.useSplatmap || terrainsIdentical(entry.heightmap, heights, entry.splatmap, splat,
                                                                      entry.masks, masks);
    const bool sameHeights = entry.heightmap.heights() == heights.heights();
    if (!sameHeights || !sameSplat) {
        std::error_code ec;
        std::filesystem::remove(native, ec);
        return false;
    }
    std::error_code ec;
    const std::string relative = std::filesystem::relative(native, projectRoot, ec).generic_string();
    if (ec || relative.empty() || relative.size() >= sizeof(terrain.terrainDataPath)) return false;
    std::memcpy(terrain.terrainDataPath, relative.c_str(), relative.size() + 1);
    it->second.fromLegacy = false;
    return true;
}

bool TerrainCache::loadTerrainFile(ECS::World& world, ECS::Entity entity,
                                   ECS::TerrainComponent& terrain,
                                   const std::filesystem::path& path) {
    const bool legacyLayerSet = hasLegacyKenneyLayerSet(terrain);
    repairTexturePaths(terrain);
    TerrainEntry& entry = ensureEntry(entity);
    bool hadSplat = false;
    bool fromLegacy = false;
    // Either format: a legacy `.cterrain` (an old project) opens as it is, a native `.terrain` is read tile by tile.
    if (!loadTerrainAny(path, entry.heightmap, entry.splatmap, &hadSplat, &entry.masks, &fromLegacy)) {
        return false;
    }
    entry.fromLegacy = fromLegacy;

    terrain.resolutionX = entry.heightmap.resolutionX();
    terrain.resolutionZ = entry.heightmap.resolutionZ();
    terrain.splatResolutionScale =
        inferSplatResolutionScale(terrain.resolutionX, entry.splatmap.resolutionX());
    // Scenes authored before the grass default stored an unpainted splatmap as
    // layer 3 (the old Light prototype texture). Migrate that uniform state so
    // existing terrains stop rendering as white checkerboards.
    if (legacyLayerSet && terrain.useSplatmap && isUniformLayer3(entry.splatmap)) {
        entry.splatmap.fillLayer(0, 1.0f);
        copyPath(terrain.texturePath, sizeof(terrain.texturePath), kDefaultTexture);
    }
    terrain.dataRevision++;
    terrain.splatRevision++;
    rebuildMesh(entity, entry, terrain);

    if (!world.has<ECS::MeshFilterComponent>(entity)) {
        ECS::MeshFilterComponent filter;
        filter.primitive = ECS::MeshPrimitive::Custom;
        world.add<ECS::MeshFilterComponent>(entity, filter);
    }
    if (!world.has<ECS::MeshRendererComponent>(entity)) {
        ECS::MeshRendererComponent renderer;
        renderer.castShadows = terrain.castShadows;
        renderer.receiveShadows = terrain.receiveShadows;
        world.add<ECS::MeshRendererComponent>(entity, renderer);
    }

    syncTextureToFilter(world, entity, terrain);
    return true;
}

void TerrainCache::restoreSnapshot(ECS::World& world, ECS::Entity entity,
                                    ECS::TerrainComponent& terrain,
                                    const TerrainHeightmap& heightmap,
                                    const TerrainSplatmap& splatmap) {
    if (heightmap.empty()) return;

    TerrainEntry& entry = ensureEntry(entity);
    entry.heightmap = heightmap;
    entry.splatmap = splatmap;

    terrain.resolutionX = entry.heightmap.resolutionX();
    terrain.resolutionZ = entry.heightmap.resolutionZ();
    terrain.splatResolutionScale =
        inferSplatResolutionScale(terrain.resolutionX, entry.splatmap.resolutionX());
    terrain.dataRevision++;
    terrain.splatRevision++;
    rebuildMesh(entity, entry, terrain);

    if (!world.has<ECS::MeshFilterComponent>(entity)) {
        ECS::MeshFilterComponent filter;
        filter.primitive = ECS::MeshPrimitive::Custom;
        world.add<ECS::MeshFilterComponent>(entity, filter);
    }
    if (!world.has<ECS::MeshRendererComponent>(entity)) {
        ECS::MeshRendererComponent renderer;
        renderer.castShadows = terrain.castShadows;
        renderer.receiveShadows = terrain.receiveShadows;
        world.add<ECS::MeshRendererComponent>(entity, renderer);
    }
    syncTextureToFilter(world, entity, terrain);
}

void TerrainCache::gatherDrawMeshes(ECS::Entity entity,
                                    const ECS::TerrainComponent& settings,
                                    const Mat4& worldMatrix,
                                    const Vec3& cameraPos,
                                    const Spatial::Frustum& frustum,
                                    std::vector<TerrainDrawChunk>& outDraws,
                                    TerrainCullStats* stats,
                                    f32 lodDistanceScale) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;

    TerrainEntry& entry = it->second;
    if (entry.useChunks && !entry.chunks.empty()) {
        TerrainLodSystem::gatherDrawMeshes(entry.chunks, entry.chunkActiveLods, settings,
                                           worldMatrix, cameraPos, frustum, outDraws, stats,
                                           lodDistanceScale);
        return;
    }

    if (entry.mesh && !entry.mesh->vertices.empty()) {
        TerrainDrawChunk draw;
        draw.mesh = entry.mesh.get();
        draw.lodLevel = 0;
        outDraws.push_back(draw);
        if (stats) {
            stats->totalChunks = 1;
            stats->visibleChunks = 1;
            stats->culledChunks = 0;
            for (u32 i = 0; i < 8; ++i) stats->lodCounts[i] = 0;
            stats->lodCounts[0] = 1;
        }
    }
}

#ifdef CF_HAS_SDL3
void TerrainCache::syncGpuTextures(RHI::RenderDevice* device, ECS::Entity entity,
                                   const ECS::TerrainComponent& terrain,
                                   const std::string& projectRoot) {
    if (device) {
        m_gpuDevice = device;
    }
    auto it = m_entries.find(entity.id());
    const TerrainSplatmap* splatmap = (it != m_entries.end()) ? &it->second.splatmap : nullptr;
    const TerrainLayerMap* layers = (it != m_entries.end() && terrain.useLayers && !it->second.layers.empty()) ? &it->second.layers : nullptr;
    const TerrainPalette* palette = (it != m_entries.end() && terrain.useLayers) ? &it->second.palette : nullptr;
    LayerBounds dirty;
    bool dirtyAll = false;
    if (it != m_entries.end()) {
        dirty = it->second.layerDirty;
        dirtyAll = it->second.layerDirtyAll;
    }
    const bool uploaded = TerrainGpuTextureCache::instance().sync(device, entity, terrain, splatmap, layers, palette, dirtyAll ? nullptr : &dirty, projectRoot);
    if (it != m_entries.end() && uploaded) {
        it->second.layerDirty = {};
        it->second.layerDirtyAll = false;
    }
}

const TerrainGpuTextures* TerrainCache::gpuTexturesFor(ECS::Entity entity) const {
    return TerrainGpuTextureCache::instance().get(entity);
}

void TerrainCache::releaseGpuResources(RHI::RenderDevice* device) {
    if (!device) return;
    m_gpuDevice = device;
    TerrainGpuTextureCache::instance().releaseAll(device);
    Render::TerrainVegetationGpu::instance().releaseAll(device);

    for (auto& [_, entry] : m_entries) {
        releaseEntryGpu(entry);
    }
    // Nothing of this device is kept: a later call that reaches for the device must not find it (it is about to go).
    if (m_gpuDevice == device) m_gpuDevice = nullptr;
}
#endif

}  // namespace Caffeine::Terrain
