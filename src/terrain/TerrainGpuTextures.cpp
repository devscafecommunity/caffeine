#include "terrain/TerrainGpuTextures.hpp"

#ifdef CF_HAS_SDL3

#include "assets/MeshCache.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <stb/stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Caffeine::Terrain {
namespace {

std::string resolveTextureFile(const std::string& path, const std::string& projectRoot) {
    return Assets::MeshCache::resolveTexturePath(path, projectRoot);
}

RHI::Texture* createTexture2D(RHI::RenderDevice* device, u32 width, u32 height,
                              RHI::TextureFormat format) {
    RHI::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = RHI::TextureUsage::Sampler;
    return device->createTexture(desc);
}

}  // namespace

TerrainGpuTextureCache& TerrainGpuTextureCache::instance() {
    static TerrainGpuTextureCache cache;
    return cache;
}

RHI::Texture* TerrainGpuTextureCache::ensureWhiteTexture(RHI::RenderDevice* device) {
    if (m_whiteTexture) return m_whiteTexture;
    m_whiteTexture = createTexture2D(device, 1, 1, RHI::TextureFormat::R8G8B8A8_UNORM);
    if (!m_whiteTexture) return nullptr;
    const u8 white[4] = {255, 255, 255, 255};
    device->uploadTexture(m_whiteTexture, white, 1, 1, 4);
    return m_whiteTexture;
}

RHI::Texture* TerrainGpuTextureCache::loadTexture(RHI::RenderDevice* device,
                                                  const std::string& path,
                                                  const std::string& projectRoot) {
    const std::string resolved = resolveTextureFile(path, projectRoot);
    if (resolved.empty()) return nullptr;

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(resolved.c_str(), &width, &height, &channels, 4);
    if (!pixels || width < 1 || height < 1) {
        if (pixels) stbi_image_free(pixels);
        return nullptr;
    }

    RHI::Texture* texture =
        createTexture2D(device, static_cast<u32>(width), static_cast<u32>(height),
                        RHI::TextureFormat::R8G8B8A8_UNORM);
    if (!texture) {
        stbi_image_free(pixels);
        return nullptr;
    }

    if (!device->uploadTexture(texture, pixels, static_cast<u32>(width),
                               static_cast<u32>(height), 4)) {
        device->destroyTexture(texture);
        stbi_image_free(pixels);
        return nullptr;
    }

    stbi_image_free(pixels);
    return texture;
}

void TerrainGpuTextureCache::releaseTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device) {
    auto releaseOwned = [&](RHI::Texture*& texture) {
        if (!texture) return;
        // m_whiteTexture is shared across entities and bound at draw time as a fallback.
        // Never destroy it here — releaseAll() owns its lifetime.
        if (device && texture != m_whiteTexture) {
            device->destroyTexture(texture);
        }
        texture = nullptr;
    };

    releaseOwned(gpu.splatMap);
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        releaseOwned(gpu.layers[i]);
    }
    releaseOwned(gpu.albedo);
}

void TerrainGpuTextureCache::sync(RHI::RenderDevice* device, ECS::Entity entity,
                                  const ECS::TerrainComponent& terrain,
                                  const TerrainSplatmap* splatmap,
                                  const std::string& projectRoot) {
    if (!device) return;

    TerrainGpuTextures& gpu = m_entries[entity.id()];
    gpu.useSplatmap = terrain.useSplatmap;

    RHI::Texture* fallback = ensureWhiteTexture(device);
    auto destroyOwned = [&](RHI::Texture*& texture) {
        if (!texture) return;
        if (texture != m_whiteTexture) {
            device->destroyTexture(texture);
        }
        texture = nullptr;
    };

    if (terrain.useSplatmap && splatmap && !splatmap->empty()) {
        bool layerPathsChanged = false;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            const std::string path = terrain.splatLayerPaths[i];
            if (gpu.cachedLayerPaths[i] != path) {
                layerPathsChanged = true;
                gpu.cachedLayerPaths[i] = path;
                destroyOwned(gpu.layers[i]);
            }
            if (!gpu.layers[i] || gpu.layers[i] == fallback) {
                if (gpu.layers[i] == fallback) {
                    gpu.layers[i] = nullptr;
                }
                gpu.layers[i] = loadTexture(device, path, projectRoot);
            }
        }

        if (layerPathsChanged || gpu.uploadedSplatRevision != terrain.splatRevision || !gpu.splatMap) {
            destroyOwned(gpu.splatMap);

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
                device->uploadTexture(gpu.splatMap, pixels.data(), resX, resZ, 4);
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
            gpu.cachedAlbedoPath = albedoPath;
            destroyOwned(gpu.albedo);
        }
        if (!gpu.albedo || gpu.albedo == fallback) {
            if (gpu.albedo == fallback) {
                gpu.albedo = nullptr;
            }
            gpu.albedo = loadTexture(device, albedoPath, projectRoot);
        }
    }
}

const TerrainGpuTextures* TerrainGpuTextureCache::get(ECS::Entity entity) const {
    auto it = m_entries.find(entity.id());
    return it != m_entries.end() ? &it->second : nullptr;
}

bool TerrainGpuTextureCache::hasRenderableTextures(ECS::Entity entity) const {
    const TerrainGpuTextures* gpu = get(entity);
    if (!gpu) return false;
    if (gpu->useSplatmap) {
        if (!gpu->splatMap) return false;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            if (gpu->layers[i] && gpu->layers[i] != m_whiteTexture) return true;
        }
        return false;
    }
    return gpu->albedo != nullptr && gpu->albedo != m_whiteTexture;
}

void TerrainGpuTextureCache::invalidateEntity(ECS::Entity entity, RHI::RenderDevice* device) {
    auto it = m_entries.find(entity.id());
    if (it == m_entries.end()) return;
    releaseTextures(it->second, device);
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        it->second.cachedLayerPaths[i].clear();
    }
    it->second.cachedAlbedoPath.clear();
    it->second.uploadedSplatRevision = 0;
}

RHI::Texture* TerrainGpuTextureCache::whiteTexture(RHI::RenderDevice* device) {
    return ensureWhiteTexture(device);
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

    if (m_whiteTexture) {
        if (device) {
            device->destroyTexture(m_whiteTexture);
        }
        m_whiteTexture = nullptr;
    }
}

}  // namespace Caffeine::Terrain
#endif
