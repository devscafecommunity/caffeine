#include "terrain/TerrainCache.hpp"

#include "ecs/MeshComponents.hpp"
#include "terrain/TerrainMeshBuilder.hpp"
#include "terrain/TerrainResolution.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "terrain/TerrainCollisionMeshBuilder.hpp"
#include "assets/MeshCache.hpp"
#include "core/WorldUnits.hpp"

#include <algorithm>
#include <cstring>

namespace Caffeine::Terrain {
namespace {

void copyPath(char* dest, size_t destSize, const std::string& path) {
    if (!dest || destSize == 0) return;
    std::strncpy(dest, path.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

}  // namespace

void TerrainCache::repairTexturePaths(ECS::TerrainComponent& terrain) {
    copyPath(terrain.texturePath, sizeof(terrain.texturePath),
             Assets::MeshCache::normalizeTexturePath(terrain.texturePath));
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        copyPath(terrain.splatLayerPaths[i], sizeof(terrain.splatLayerPaths[i]),
                 Assets::MeshCache::normalizeTexturePath(terrain.splatLayerPaths[i]));
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
        filter->customTexturePath = terrain.splatLayerPaths[ECS::kTerrainSplatLayerCount - 1];
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
    entry.splatmap.fillLayer(3, 1.0f);
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
#endif

void TerrainCache::rebuildMesh(ECS::Entity entity, TerrainEntry& entry,
                               ECS::TerrainComponent& component) {
    entry.useChunks = component.useChunks;
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
    (void)entity;
}

void TerrainCache::rebuildRegion(ECS::Entity entity,
                                 const ECS::TerrainComponent& settings,
                                 u32 minVertexX, u32 minVertexZ,
                                 u32 maxVertexX, u32 maxVertexZ) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;

    TerrainEntry& entry = it->second;
    if (entry.heightmap.empty()) return;

    if (entry.useChunks && !entry.chunks.empty()) {
        TerrainLodSystem::rebuildChunksInRegion(entry.chunks, entry.heightmap, settings,
                                                minVertexX, minVertexZ, maxVertexX, maxVertexZ);
        return;
    }

    if (entry.mesh) {
        *entry.mesh = TerrainMeshBuilder::build(entry.heightmap, settings);
    }
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
            entry.splatmap.fillLayer(3, 1.0f);
        }
        terrain->splatRevision++;
        resolutionChanged = true;
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
#ifdef CF_HAS_SDL3
    TerrainGpuTextureCache::instance().removeEntity(entity, nullptr);
#endif
    m_entries.erase(entity.id());
}

void TerrainCache::clear() {
#ifdef CF_HAS_SDL3
    TerrainGpuTextureCache::instance().releaseAll(nullptr);
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

TerrainSplatmap* TerrainCache::splatmapFor(ECS::Entity entity) {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.splatmap : nullptr;
}

const TerrainSplatmap* TerrainCache::splatmapFor(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second.splatmap : nullptr;
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
    return TerrainSerializer::save(path, it->second.heightmap, it->second.splatmap, includeSplat);
}

bool TerrainCache::loadTerrainFile(ECS::World& world, ECS::Entity entity,
                                   ECS::TerrainComponent& terrain,
                                   const std::filesystem::path& path) {
    TerrainEntry& entry = ensureEntry(entity);
    bool hadSplat = false;
    if (!TerrainSerializer::load(path, entry.heightmap, entry.splatmap, &hadSplat)) {
        return false;
    }

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
    return true;
}

void TerrainCache::gatherDrawMeshes(ECS::Entity entity,
                                    const ECS::TerrainComponent& settings,
                                    const Mat4& worldMatrix,
                                    const Vec3& cameraPos,
                                    const Spatial::Frustum& frustum,
                                    std::vector<TerrainDrawChunk>& outDraws,
                                    TerrainCullStats* stats) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;

    TerrainEntry& entry = it->second;
    if (entry.useChunks && !entry.chunks.empty()) {
        TerrainLodSystem::gatherDrawMeshes(entry.chunks, entry.chunkActiveLods, settings,
                                           worldMatrix, cameraPos, frustum, outDraws, stats);
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
    TerrainGpuTextureCache::instance().sync(device, entity, terrain, splatmap, projectRoot);
}

const TerrainGpuTextures* TerrainCache::gpuTexturesFor(ECS::Entity entity) const {
    return TerrainGpuTextureCache::instance().get(entity);
}

void TerrainCache::releaseGpuResources(RHI::RenderDevice* device) {
    if (!device) return;
    m_gpuDevice = device;
    TerrainGpuTextureCache::instance().releaseAll(device);

    for (auto& pair : m_entries) {
        if (pair.second.mesh) {
            releaseMeshGpu(*pair.second.mesh);
        }
        for (auto& chunk : pair.second.chunks) {
            for (auto& lodMesh : chunk.lodMeshes) {
                if (lodMesh) releaseMeshGpu(*lodMesh);
            }
        }
    }
}
#endif

}  // namespace Caffeine::Terrain
