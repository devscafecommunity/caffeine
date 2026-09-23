#include "terrain/TerrainGpuTextures.hpp"

#ifdef CF_HAS_SDL3

#include "assets/MeshCache.hpp"
#include "render/GpuTextureCache.hpp"
#include "terrain/TerrainSplatmap.hpp"

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

void TerrainGpuTextureCache::releaseTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device) {
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

void TerrainGpuTextureCache::sync(RHI::RenderDevice* device, ECS::Entity entity,
                                  const ECS::TerrainComponent& terrain,
                                  const TerrainSplatmap* splatmap,
                                  const std::string& projectRoot) {
    if (!device) return;

    TerrainGpuTextures& gpu = m_entries[entity.id()];
    gpu.useSplatmap = terrain.useSplatmap;

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
                gpu.layers[i] = loadTexture(device, path, projectRoot);
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
            gpu.albedo = loadTexture(device, albedoPath, projectRoot);
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
}

const TerrainGpuTextures* TerrainGpuTextureCache::get(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second : nullptr;
}

bool TerrainGpuTextureCache::hasRenderableTextures(ECS::Entity entity) const {
    const TerrainGpuTextures* gpu = get(entity);
    if (!gpu) return false;
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
