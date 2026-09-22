#include "render/GpuTextureCache.hpp"

#ifdef CF_HAS_SDL3

#include "assets/MeshCache.hpp"
#include "render/TextureQuality.hpp"

#include <stb/stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Caffeine::Render {
namespace {

void halveRgbaImage(const u8* src, u32 srcW, u32 srcH, std::vector<u8>& dst, u32& dstW, u32& dstH) {
    dstW = std::max(1u, srcW / 2);
    dstH = std::max(1u, srcH / 2);
    dst.resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    for (u32 y = 0; y < dstH; ++y) {
        for (u32 x = 0; x < dstW; ++x) {
            const u32 sx = x * 2;
            const u32 sy = y * 2;
            u32 r = 0, g = 0, b = 0, a = 0;
            u32 count = 0;
            for (u32 oy = 0; oy < 2 && sy + oy < srcH; ++oy) {
                for (u32 ox = 0; ox < 2 && sx + ox < srcW; ++ox) {
                    const size_t idx =
                        (static_cast<size_t>(sy + oy) * static_cast<size_t>(srcW) +
                         static_cast<size_t>(sx + ox)) *
                        4;
                    r += src[idx + 0];
                    g += src[idx + 1];
                    b += src[idx + 2];
                    a += src[idx + 3];
                    ++count;
                }
            }
            if (count == 0) count = 1;
            const size_t out = (static_cast<size_t>(y) * static_cast<size_t>(dstW) +
                                static_cast<size_t>(x)) *
                               4;
            dst[out + 0] = static_cast<u8>(r / count);
            dst[out + 1] = static_cast<u8>(g / count);
            dst[out + 2] = static_cast<u8>(b / count);
            dst[out + 3] = static_cast<u8>(a / count);
        }
    }
}

}  // namespace

GpuTextureCache& GpuTextureCache::instance() {
    static GpuTextureCache cache;
    return cache;
}

void GpuTextureCache::downscaleRgba(std::vector<u8>& rgba, u32& width, u32& height, u32 qualityTier) {
    for (u32 tier = 0; tier < qualityTier; ++tier) {
        if (width <= 16 && height <= 16) break;
        std::vector<u8> smaller;
        u32 newW = 0;
        u32 newH = 0;
        halveRgbaImage(rgba.data(), width, height, smaller, newW, newH);
        rgba = std::move(smaller);
        width = newW;
        height = newH;
    }
}

std::string GpuTextureCache::resolvePath(const std::string& path,
                                         const std::string& projectRoot) const {
    return Assets::MeshCache::resolveTexturePath(path, projectRoot);
}

u64 GpuTextureCache::fileTimestamp(const std::string& path) const {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<u64>(ft.time_since_epoch().count());
}

GpuTextureCache::Entry* GpuTextureCache::findByTexture(RHI::Texture* texture) {
    if (!texture) return nullptr;
    for (auto& [_, entry] : m_cache) {
        if (entry.texture == texture) return &entry;
    }
    return nullptr;
}

void GpuTextureCache::destroyEntry(RHI::RenderDevice* device, Entry& entry) {
    if (device && entry.texture) {
        device->destroyTexture(entry.texture);
    }
    entry.texture = nullptr;
    entry.refCount = 0;
}

RHI::Texture* GpuTextureCache::loadTexture(RHI::RenderDevice* device, const std::string& resolved,
                                           u32 qualityTier) {
    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(resolved.c_str(), &width, &height, &channels, 4);
    if (!pixels || width < 1 || height < 1) {
        if (pixels) stbi_image_free(pixels);
        return nullptr;
    }

    u32 uploadW = static_cast<u32>(width);
    u32 uploadH = static_cast<u32>(height);
    std::vector<u8> rgba;
    if (qualityTier > 0) {
        rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        stbi_image_free(pixels);
        pixels = nullptr;
        downscaleRgba(rgba, uploadW, uploadH, qualityTier);
    }

    RHI::TextureDesc desc;
    desc.width = uploadW;
    desc.height = uploadH;
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    desc.usage = RHI::TextureUsage::Sampler;
    desc.mipLevels = 1;

    RHI::Texture* texture = device->createTexture(desc);
    if (!texture) {
        if (pixels) stbi_image_free(pixels);
        return nullptr;
    }

    const bool uploaded = pixels
        ? device->uploadTexture(texture, pixels, uploadW, uploadH, 4, 0)
        : device->uploadTexture(texture, rgba.data(), uploadW, uploadH, 4, 0);
    if (pixels) stbi_image_free(pixels);
    if (!uploaded) {
        device->destroyTexture(texture);
        return nullptr;
    }

    return texture;
}

RHI::Texture* GpuTextureCache::acquireFromPixels(RHI::RenderDevice* device,
                                                 const std::string& cacheKey, const u8* pixels,
                                                 u32 width, u32 height, int channels,
                                                 u32 qualityTier) {
    if (!device || !pixels || width < 1 || height < 1 || cacheKey.empty()) return nullptr;

    const std::string tieredKey = textureCacheKeyForTier(cacheKey, qualityTier);
    auto it = m_cache.find(tieredKey);
    if (it != m_cache.end() && it->second.texture) {
        it->second.refCount++;
        return it->second.texture;
    }

    const int srcChannels = channels > 0 ? channels : 4;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<u8> rgba(pixelCount * 4);
    if (srcChannels == 4) {
        std::memcpy(rgba.data(), pixels, pixelCount * 4);
    } else if (srcChannels == 3) {
        for (size_t i = 0; i < pixelCount; ++i) {
            rgba[i * 4 + 0] = pixels[i * 3 + 0];
            rgba[i * 4 + 1] = pixels[i * 3 + 1];
            rgba[i * 4 + 2] = pixels[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
    } else if (srcChannels == 1) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const u8 v = pixels[i];
            rgba[i * 4 + 0] = v;
            rgba[i * 4 + 1] = v;
            rgba[i * 4 + 2] = v;
            rgba[i * 4 + 3] = 255;
        }
    } else {
        return nullptr;
    }

    u32 uploadW = width;
    u32 uploadH = height;
    downscaleRgba(rgba, uploadW, uploadH, qualityTier);

    RHI::TextureDesc desc;
    desc.width = uploadW;
    desc.height = uploadH;
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    desc.usage = RHI::TextureUsage::Sampler;
    desc.mipLevels = 1;

    RHI::Texture* texture = device->createTexture(desc);
    if (!texture) return nullptr;

    if (!device->uploadTexture(texture, rgba.data(), uploadW, uploadH, 4, 0)) {
        device->destroyTexture(texture);
        return nullptr;
    }

    Entry entry;
    entry.texture = texture;
    entry.refCount = 1;
    entry.width = uploadW;
    entry.height = uploadH;
    entry.mipLevels = 1;
    m_cache[tieredKey] = entry;
    return texture;
}

RHI::Texture* GpuTextureCache::acquire(RHI::RenderDevice* device, const std::string& path,
                                       const std::string& projectRoot, u32 qualityTier) {
    if (!device || path.empty()) return nullptr;

    const std::string resolved = resolvePath(path, projectRoot);
    if (resolved.empty()) return nullptr;

    const std::string cacheKey = textureCacheKeyForTier(resolved, qualityTier);
    const u64 stamp = fileTimestamp(resolved);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end() && it->second.texture) {
        Entry& entry = it->second;
        if (stamp != 0 && entry.fileStamp != stamp && entry.refCount == 0) {
            destroyEntry(device, entry);
            m_cache.erase(it);
        } else {
            entry.refCount++;
            return entry.texture;
        }
    }

    RHI::Texture* texture = loadTexture(device, resolved, qualityTier);
    if (!texture) return nullptr;

    Entry entry;
    entry.texture = texture;
    entry.refCount = 1;
    entry.fileStamp = stamp;
    entry.width = texture->width;
    entry.height = texture->height;
    entry.mipLevels = 1;
    m_cache[cacheKey] = entry;
    return texture;
}

void GpuTextureCache::release(RHI::RenderDevice* device, const std::string& resolvedPath) {
    if (resolvedPath.empty()) return;
    for (u32 tier = 0; tier <= kTextureQualityMaxTier; ++tier) {
        const std::string key = textureCacheKeyForTier(resolvedPath, tier);
        auto it = m_cache.find(key);
        if (it == m_cache.end()) continue;
        Entry& entry = it->second;
        if (entry.refCount > 0) entry.refCount--;
        if (entry.refCount == 0) {
            destroyEntry(device, entry);
            m_cache.erase(it);
        }
    }
}

void GpuTextureCache::release(RHI::RenderDevice* device, RHI::Texture* texture) {
    if (!texture || texture == m_white) return;
    Entry* entry = findByTexture(texture);
    if (!entry) return;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (&it->second == entry) {
            if (entry->refCount > 0) entry->refCount--;
            if (entry->refCount == 0) {
                destroyEntry(device, *entry);
                m_cache.erase(it);
            }
            return;
        }
    }
}

RHI::Texture* GpuTextureCache::whiteTexture(RHI::RenderDevice* device) {
    if (m_white) return m_white;
    if (!device) return nullptr;

    RHI::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    desc.usage = RHI::TextureUsage::Sampler;
    desc.mipLevels = 1;
    m_white = device->createTexture(desc);
    if (!m_white) return nullptr;
    const u8 white[4] = {255, 255, 255, 255};
    device->uploadTexture(m_white, white, 1, 1, 4, 0);
    return m_white;
}

void GpuTextureCache::invalidate(const std::string& resolvedPath) {
    for (u32 tier = 0; tier <= kTextureQualityMaxTier; ++tier) {
        const std::string key = textureCacheKeyForTier(resolvedPath, tier);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            it->second.fileStamp = 0;
        }
    }
    auto it = m_cache.find(resolvedPath);
    if (it != m_cache.end()) {
        it->second.fileStamp = 0;
    }
}

void GpuTextureCache::releaseAll(RHI::RenderDevice* device) {
    for (auto& [_, entry] : m_cache) {
        destroyEntry(device, entry);
    }
    m_cache.clear();
    if (m_white) {
        if (device) device->destroyTexture(m_white);
        m_white = nullptr;
    }
}

}  // namespace Caffeine::Render

#endif
