#include "terrain/TerrainGpuTextures.hpp"

#ifdef CF_HAS_SDL3

#include "assets/MeshCache.hpp"
#include "render/GpuTextureCache.hpp"
#include "terrain/TerrainSplatmap.hpp"
#include "terrain/paint/TerrainLayerMap.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"
#include "terrain/paint/TerrainPalette.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Caffeine::Terrain {
namespace {

RHI::Texture* createTexture2D(RHI::RenderDevice* device, u32 width, u32 height,
                              RHI::TextureFormat format) {
    RHI::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = RHI::TextureUsage::Sampler;
    return device->createTexture(desc);
}

void releaseCachedPath(RHI::RenderDevice* device, RHI::Texture*& texture) {
    if (!texture) return;
    Render::GpuTextureCache::instance().release(device, texture);
    texture = nullptr;
}

}  // namespace

TerrainGpuTextureCache& TerrainGpuTextureCache::instance() {
    static TerrainGpuTextureCache cache;
    return cache;
}

RHI::Texture* TerrainGpuTextureCache::ensureWhiteTexture(RHI::RenderDevice* device) {
    return Render::GpuTextureCache::instance().whiteTexture(device);
}

RHI::Texture* TerrainGpuTextureCache::loadTexture(RHI::RenderDevice* device,
                                                  const std::string& path,
                                                  const std::string& projectRoot) {
    return Render::GpuTextureCache::instance().acquire(device, path, projectRoot);
}

void TerrainGpuTextureCache::releaseLayerTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device) {
    if (device) {
        if (gpu.indexMap) device->destroyTexture(gpu.indexMap);
        if (gpu.weightMap) device->destroyTexture(gpu.weightMap);
        if (gpu.layerArray) device->destroyTexture(gpu.layerArray);
    }
    gpu.indexMap = gpu.weightMap = gpu.layerArray = nullptr;
    gpu.uploadedLayerRevision = 0;
    gpu.arraySize = 0;
    gpu.slotLayers.clear();
    gpu.arrayKey.clear();
    gpu.arrayBytes = 0;
    gpu.useLayers = false;
}

namespace {

// The VRAM a mipped RGBA8 array takes: every slice, a third more for the chain.
u64 arrayBytesFor(u32 slices, u32 size) { return static_cast<u64>(slices) * size * size * 4 * 4 / 3; }

}  // namespace

bool TerrainGpuTextureCache::syncLayers(RHI::RenderDevice* device, TerrainGpuTextures& gpu, const ECS::TerrainComponent& terrain,
                                        const TerrainLayerMap& layers, const TerrainPalette& palette,
                                        const LayerBounds* dirty, const std::string& projectRoot) {
    const u32 resX = layers.resolutionX(), resZ = layers.resolutionZ();
    gpu.useLayers = true;

    // The layers in use: only those get a slice, so 64 layers cost what the terrain really shows.
    const std::vector<u32> usage = layers.usage(static_cast<u32>(palette.size()));
    bool covered = !gpu.slotLayers.empty();
    for (u32 layer = 0; layer < usage.size() && covered; ++layer) {
        if (usage[layer] > 0 && std::find(gpu.slotLayers.begin(), gpu.slotLayers.end(), layer) == gpu.slotLayers.end()) covered = false;
    }
    std::vector<u32> wanted = gpu.slotLayers;
    if (!covered) {
        wanted.clear();
        for (u32 layer = 0; layer < usage.size(); ++layer) {
            if (usage[layer] > 0) wanted.push_back(layer);
        }
        if (wanted.empty()) wanted.push_back(0);
    }

    // The sharpness class: the largest any layer in use asks for, lowered until the array fits the budget.
    u32 size = kMinLayerResolution;
    for (u32 layer : wanted) {
        if (const PaletteLayer* l = palette.find(layer)) size = std::max(size, TerrainPalette::resolutionClass(l->resolution));
    }
    const u64 budget = static_cast<u64>(std::max(terrain.layerVramBudgetMb, 16u)) * 1024ull * 1024ull;
    while (size > kMinLayerResolution && arrayBytesFor(static_cast<u32>(wanted.size()), size) > budget) size /= 2;

    std::string key = std::to_string(size) + "|";
    for (u32 layer : wanted) {
        const PaletteLayer* l = palette.find(layer);
        if (!l) continue;
        key += std::to_string(layer) + ":" + l->albedo + ":" + std::to_string(l->tint[0]) + "," + std::to_string(l->tint[1]) + "," +
               std::to_string(l->tint[2]) + ";";
    }
    bool slotsChanged = false;
    if (!gpu.layerArray || key != gpu.arrayKey) {
        std::vector<Render::GpuTextureCache::ArraySlice> slices;
        for (u32 layer : wanted) {
            Render::GpuTextureCache::ArraySlice slice;
            if (const PaletteLayer* l = palette.find(layer)) {
                slice.path = l->albedo;
                std::copy(l->tint, l->tint + 3, slice.tint);
            }
            slices.push_back(slice);
        }
        RHI::Texture* array = Render::GpuTextureCache::instance().buildTextureArray(device, slices, size, projectRoot, true);
        if (!array) return false;
        if (gpu.layerArray) device->destroyTexture(gpu.layerArray);
        gpu.layerArray = array;
        gpu.arraySize = size;
        gpu.arrayKey = key;
        gpu.slotLayers = wanted;
        gpu.arrayBytes = arrayBytesFor(static_cast<u32>(wanted.size()), size);
        slotsChanged = true;
    }
    // Tiling and turn of every slice, read by the shader.
    std::fill(std::begin(gpu.layerTile), std::end(gpu.layerTile), 8.0f);
    std::fill(std::begin(gpu.layerRotation), std::end(gpu.layerRotation), 0.0f);
    std::vector<u8> sliceOfLayer(TerrainLayerMap::kMaxLayers, 0);
    for (u32 slot = 0; slot < gpu.slotLayers.size() && slot < 64; ++slot) {
        sliceOfLayer[gpu.slotLayers[slot]] = static_cast<u8>(slot);
        if (const PaletteLayer* l = palette.find(gpu.slotLayers[slot])) {
            gpu.layerTile[slot] = l->tileSizeM;
            gpu.layerRotation[slot] = l->rotationDegrees * 3.14159265f / 180.0f;
        }
    }

    const bool sized = gpu.indexMap && gpu.weightMap && gpu.indexMap->width == resX && gpu.indexMap->height == resZ;
    const bool revisionMoved = gpu.uploadedLayerRevision != terrain.layerRevision;
    if (sized && !slotsChanged && !revisionMoved) return true;

    auto packRect = [&](u32 x0, u32 z0, u32 x1, u32 z1, std::vector<u8>& index, std::vector<u8>& weight) {
        const u32 w = x1 - x0 + 1, h = z1 - z0 + 1;
        index.assign(static_cast<size_t>(w) * h * 4, 0);
        weight.assign(static_cast<size_t>(w) * h * 4, 0);
        for (u32 z = z0; z <= z1; ++z) {
            for (u32 x = x0; x <= x1; ++x) {
                const LayerTexel& texel = layers.at(x, z);
                const size_t at = (static_cast<size_t>(z - z0) * w + (x - x0)) * 4;
                for (u32 s = 0; s < 4; ++s) {
                    index[at + s] = sliceOfLayer[texel.index[s] % TerrainLayerMap::kMaxLayers];
                    weight[at + s] = texel.weight[s];
                }
            }
        }
    };

    if (!sized) {
        if (gpu.indexMap) device->destroyTexture(gpu.indexMap);
        if (gpu.weightMap) device->destroyTexture(gpu.weightMap);
        gpu.indexMap = createTexture2D(device, resX, resZ, RHI::TextureFormat::R8G8B8A8_UNORM);
        gpu.weightMap = createTexture2D(device, resX, resZ, RHI::TextureFormat::R8G8B8A8_UNORM);
        if (!gpu.indexMap || !gpu.weightMap) return false;
    }
    std::vector<u8> index, weight;
    if (sized && !slotsChanged && dirty && dirty->valid) {
        // Only the texels a stroke touched (a stroke on a 4k map is a few kilobytes, not the whole 134 MB).
        const u32 x1 = std::min(dirty->maxX, resX - 1), z1 = std::min(dirty->maxZ, resZ - 1);
        const u32 x0 = std::min(dirty->minX, x1), z0 = std::min(dirty->minZ, z1);
        packRect(x0, z0, x1, z1, index, weight);
        device->uploadTextureRect(gpu.indexMap, index.data(), x0, z0, x1 - x0 + 1, z1 - z0 + 1, 4, 0);
        device->uploadTextureRect(gpu.weightMap, weight.data(), x0, z0, x1 - x0 + 1, z1 - z0 + 1, 4, 0);
    } else {
        packRect(0, 0, resX - 1, resZ - 1, index, weight);
        device->uploadTexture(gpu.indexMap, index.data(), resX, resZ, 4, 0);
        device->uploadTexture(gpu.weightMap, weight.data(), resX, resZ, 4, 0);
    }
    gpu.uploadedLayerRevision = terrain.layerRevision;
    return true;
}

void TerrainGpuTextureCache::releaseTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device) {
    releaseLayerTextures(gpu, device);
    if (device && gpu.splatMap) {
        device->destroyTexture(gpu.splatMap);
        gpu.splatMap = nullptr;
    }

    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        releaseCachedPath(device, gpu.layers[i]);
    }
    releaseCachedPath(device, gpu.albedo);
    releaseCachedPath(device, gpu.normalMap);
}

bool TerrainGpuTextureCache::sync(RHI::RenderDevice* device, ECS::Entity entity,
                                  const ECS::TerrainComponent& terrain,
                                  const TerrainSplatmap* splatmap,
                                  const TerrainLayerMap* layers,
                                  const TerrainPalette* palette,
                                  const LayerBounds* dirty,
                                  const std::string& projectRoot) {
    if (!device) return false;

    TerrainGpuTextures& gpu = m_entries[entity.id()];
    gpu.useSplatmap = terrain.useSplatmap;
    bool layersUpToDate = true;
    if (terrain.useLayers && layers && palette && !layers->empty() && !palette->empty()) {
        layersUpToDate = syncLayers(device, gpu, terrain, *layers, *palette, dirty, projectRoot);
    } else if (gpu.useLayers || gpu.layerArray) {
        releaseLayerTextures(gpu, device);
    }

    RHI::Texture* fallback = ensureWhiteTexture(device);
    auto destroyOwned = [&](RHI::Texture*& texture, std::string& cachedPath) {
        if (!texture) return;
        releaseCachedPath(device, texture);
        cachedPath.clear();
    };

    if (terrain.useSplatmap && splatmap && !splatmap->empty()) {
        bool layerPathsChanged = false;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            const std::string path = terrain.splatLayerPaths[i];
            if (gpu.cachedLayerPaths[i] != path) {
                layerPathsChanged = true;
                destroyOwned(gpu.layers[i], gpu.cachedLayerPaths[i]);
                gpu.cachedLayerPaths[i] = path;
            }
            if (!gpu.layers[i] || gpu.layers[i] == fallback) {
                if (gpu.layers[i] == fallback) gpu.layers[i] = nullptr;
                gpu.layers[i] = Render::GpuTextureCache::instance().acquire(device, path, projectRoot, 0, true);
            }
        }

        if (layerPathsChanged || gpu.uploadedSplatRevision != terrain.splatRevision || !gpu.splatMap) {
            if (gpu.splatMap) {
                device->destroyTexture(gpu.splatMap);
                gpu.splatMap = nullptr;
            }

            const u32 resX = splatmap->resolutionX();
            const u32 resZ = splatmap->resolutionZ();
            gpu.splatMap = createTexture2D(device, resX, resZ, RHI::TextureFormat::R8G8B8A8_UNORM);
            if (gpu.splatMap) {
                std::vector<u8> pixels(static_cast<size_t>(resX) * static_cast<size_t>(resZ) * 4);
                const auto& weights = splatmap->weights();
                for (usize i = 0; i < weights.size(); ++i) {
                    const Vec4& w = weights[i];
                    pixels[i * 4 + 0] = static_cast<u8>(std::clamp(w.x, 0.0f, 1.0f) * 255.0f);
                    pixels[i * 4 + 1] = static_cast<u8>(std::clamp(w.y, 0.0f, 1.0f) * 255.0f);
                    pixels[i * 4 + 2] = static_cast<u8>(std::clamp(w.z, 0.0f, 1.0f) * 255.0f);
                    pixels[i * 4 + 3] = static_cast<u8>(std::clamp(w.w, 0.0f, 1.0f) * 255.0f);
                }
                device->uploadTexture(gpu.splatMap, pixels.data(), resX, resZ, 4, 0);
            }
            gpu.uploadedSplatRevision = terrain.splatRevision;
        }
    } else {
        releaseTextures(gpu, device);
        gpu.uploadedSplatRevision = 0;
        gpu.useSplatmap = false;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            gpu.cachedLayerPaths[i].clear();
        }

        const std::string albedoPath = terrain.texturePath;
        if (gpu.cachedAlbedoPath != albedoPath) {
            destroyOwned(gpu.albedo, gpu.cachedAlbedoPath);
            gpu.cachedAlbedoPath = albedoPath;
        }
        if (!gpu.albedo || gpu.albedo == fallback) {
            if (gpu.albedo == fallback) gpu.albedo = nullptr;
            gpu.albedo = Render::GpuTextureCache::instance().acquire(device, albedoPath, projectRoot, 0, true);
        }
    }

    const std::string normalPath = terrain.normalMapPath;
    if (gpu.cachedNormalPath != normalPath) {
        destroyOwned(gpu.normalMap, gpu.cachedNormalPath);
        gpu.cachedNormalPath = normalPath;
    }
    if (!normalPath.empty() && !gpu.normalMap) {
        gpu.normalMap = loadTexture(device, normalPath, projectRoot);
    }
    return layersUpToDate;
}

const TerrainGpuTextures* TerrainGpuTextureCache::get(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second : nullptr;
}

bool TerrainGpuTextureCache::hasRenderableTextures(ECS::Entity entity) const {
    const TerrainGpuTextures* gpu = get(entity);
    if (!gpu) return false;
    if (gpu->useLayers) return gpu->layerArray != nullptr && gpu->indexMap != nullptr;
    RHI::Texture* white = m_whiteTexture;
    if (gpu->useSplatmap) {
        if (!gpu->splatMap) return false;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            if (gpu->layers[i] && gpu->layers[i] != white) return true;
        }
        return false;
    }
    return gpu->albedo != nullptr && gpu->albedo != white;
}

void TerrainGpuTextureCache::invalidateEntity(ECS::Entity entity, RHI::RenderDevice* device) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    releaseTextures(it->second, device);
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        it->second.cachedLayerPaths[i].clear();
    }
    it->second.cachedAlbedoPath.clear();
    it->second.cachedNormalPath.clear();
    it->second.uploadedSplatRevision = 0;
}

RHI::Texture* TerrainGpuTextureCache::whiteTexture(RHI::RenderDevice* device) {
    m_whiteTexture = ensureWhiteTexture(device);
    return m_whiteTexture;
}

RHI::Texture* TerrainGpuTextureCache::textureFromPath(RHI::RenderDevice* device,
                                                      const std::string& path,
                                                      const std::string& projectRoot) {
    return loadTexture(device, path, projectRoot);
}

void TerrainGpuTextureCache::removeEntity(ECS::Entity entity, RHI::RenderDevice* device) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    releaseTextures(it->second, device);
    m_entries.erase(it);
}

void TerrainGpuTextureCache::releaseAll(RHI::RenderDevice* device) {
    for (auto& [_, gpu] : m_entries) {
        releaseTextures(gpu, device);
    }
    m_entries.clear();
    m_whiteTexture = nullptr;
}

}  // namespace Caffeine::Terrain
#endif
