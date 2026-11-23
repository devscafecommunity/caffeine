#include "render/GpuTextureCache.hpp"

#ifdef CF_HAS_SDL3

#include "assets/MeshCache.hpp"
#include "render/TextureQuality.hpp"

#include <stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include "debug/Profiler.hpp"

#include <chrono>
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

/// Uploads `rgba` with a full box-filtered mip chain so minified textures don't shimmer.
RHI::Texture* createMippedTexture(RHI::RenderDevice* device, std::vector<u8> rgba, u32 width,
                                  u32 height, bool srgb) {
    u32 mipLevels = 1;
    for (u32 size = std::max(width, height); size > 1; size >>= 1) ++mipLevels;

    RHI::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = srgb ? RHI::TextureFormat::R8G8B8A8_UNORM_SRGB : RHI::TextureFormat::R8G8B8A8_UNORM;
    desc.usage = RHI::TextureUsage::Sampler;
    desc.mipLevels = mipLevels;
    RHI::Texture* texture = device->createTexture(desc);
    if (!texture) return nullptr;

    u32 levelW = width;
    u32 levelH = height;
    for (u32 level = 0; level < mipLevels; ++level) {
        if (!device->uploadTexture(texture, rgba.data(), levelW, levelH, 4, level)) {
            device->destroyTexture(texture);
            return nullptr;
        }
        if (level + 1 == mipLevels) break;
        std::vector<u8> next;
        u32 nextW = 0;
        u32 nextH = 0;
        halveRgbaImage(rgba.data(), levelW, levelH, next, nextW, nextH);
        rgba = std::move(next);
        levelW = nextW;
        levelH = nextH;
    }
    return texture;
}

std::string colorSpaceKey(const std::string& key, bool srgb) {
    return srgb ? key + "#srgb" : key;
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
                                           u32 qualityTier, bool srgb) {
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
    std::vector<u8> rgba(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);
    downscaleRgba(rgba, uploadW, uploadH, qualityTier);
    return createMippedTexture(device, std::move(rgba), uploadW, uploadH, srgb);
}

bool GpuTextureCache::averageColor(const std::string& path, const std::string& projectRoot, Vec3& out) {
    if (path.empty()) return false;
    const std::string resolved = resolvePath(path, projectRoot);
    if (resolved.empty()) return false;
    const u64 stamp = fileTimestamp(resolved);
    auto it = m_averages.find(resolved);
    if (it != m_averages.end() && it->second.fileStamp == stamp) {
        out = it->second.colour;
        return it->second.valid;
    }
    Average entry;
    entry.fileStamp = stamp;
    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(resolved.c_str(), &width, &height, &channels, 4);
    if (pixels && width > 0 && height > 0) {
        // At most about 65k samples, evenly spread: a mean does not need every texel of a 4k picture.
        const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t stride = std::max<size_t>(1, total / 65536);
        double sum[3] = {0.0, 0.0, 0.0};
        double weight = 0.0;
        for (size_t i = 0; i < total; i += stride) {
            const u8* p = pixels + i * 4;
            const double a = p[3] / 255.0;
            for (int c = 0; c < 3; ++c) {
                const double v = p[c] / 255.0;
                const double linear = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
                sum[c] += linear * a;
            }
            weight += a;
        }
        if (weight > 0.0) {
            entry.colour = Vec3(static_cast<f32>(sum[0] / weight), static_cast<f32>(sum[1] / weight),
                                static_cast<f32>(sum[2] / weight));
            entry.valid = true;
        }
    }
    if (pixels) stbi_image_free(pixels);
    m_averages[resolved] = entry;
    out = entry.colour;
    return entry.valid;
}

RHI::Texture* GpuTextureCache::acquireFromPixels(RHI::RenderDevice* device,
                                                 const std::string& cacheKey, const u8* pixels,
                                                 u32 width, u32 height, int channels,
                                                 u32 qualityTier, bool srgb) {
    if (!device || !pixels || width < 1 || height < 1 || cacheKey.empty()) return nullptr;

    const std::string tieredKey = colorSpaceKey(textureCacheKeyForTier(cacheKey, qualityTier), srgb);
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

    RHI::Texture* texture = createMippedTexture(device, std::move(rgba), uploadW, uploadH, srgb);
    if (!texture) return nullptr;

    Entry entry;
    entry.texture = texture;
    entry.device = device;
    entry.refCount = 1;
    entry.width = uploadW;
    entry.height = uploadH;
    m_cache[tieredKey] = entry;
    return texture;
}

RHI::Texture* GpuTextureCache::acquire(RHI::RenderDevice* device, const std::string& path,
                                       const std::string& projectRoot, u32 qualityTier,
                                       bool srgb) {
    CF_PROFILE_SCOPE("texcache::acquire");
    if (!device || path.empty()) return nullptr;

    // The per-draw fast path: no file-system call while the entry was checked recently.
    static thread_local std::string fastKey;
    fastKey.assign(path);
    fastKey.push_back('\0');
    fastKey.append(projectRoot);
    fastKey.push_back('\0');
    fastKey.push_back(static_cast<char>('0' + std::min<u32>(qualityTier, 9u)));
    fastKey.push_back(srgb ? 's' : 'l');
    const u64 nowMs = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (auto fast = m_fast.find(fastKey); fast != m_fast.end()) {
        Entry* entry = fast->second.entry;
        if (nowMs < fast->second.recheckAtMs) {
            // A null entry is a remembered miss (a path that does not resolve or a picture that does not load):
            // asking the disk again every frame for a file that is not there costs about as much as a draw.
            if (!entry) return nullptr;
            if (entry->texture) {
                entry->refCount++;
                return entry->texture;
            }
        }
    }

    CF_PROFILE_SCOPE("texcache::slow");
    const std::string resolved = resolvePath(path, projectRoot);
    if (resolved.empty()) {
        m_fast[fastKey] = Fast{nullptr, nowMs + kRecheckMs};
        return nullptr;
    }

    const std::string cacheKey = colorSpaceKey(textureCacheKeyForTier(resolved, qualityTier), srgb);
    const u64 stamp = fileTimestamp(resolved);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end() && it->second.texture) {
        Entry& entry = it->second;
        if (stamp != 0 && entry.fileStamp != stamp) {
            // Hot reload: a bake rewrote the same file. The scene renderer and
            // preview hold raw texture pointers across frames and do not
            // release them, so retire the old texture instead of destroying a
            // pointer that may still be bound by another system. The retired
            // list is drained on device shutdown.
            m_retired.emplace_back(entry.device, entry.texture);
            entry.texture = nullptr;
            entry.device = nullptr;
            entry.refCount = 0;
            entry.fileStamp = 0;
        } else {
            entry.refCount++;
            m_fast[fastKey] = Fast{&entry, nowMs + kRecheckMs};
            return entry.texture;
        }
    }

    RHI::Texture* texture = loadTexture(device, resolved, qualityTier, srgb);
    if (!texture) {
        m_fast[fastKey] = Fast{nullptr, nowMs + kRecheckMs};
        return nullptr;
    }

    Entry entry;
    entry.texture = texture;
    entry.device = device;
    entry.refCount = 1;
    entry.fileStamp = stamp;
    entry.width = texture->width;
    entry.height = texture->height;
    m_cache[cacheKey] = entry;
    m_fast[fastKey] = Fast{&m_cache[cacheKey], nowMs + kRecheckMs};
    return texture;
}

void GpuTextureCache::release(RHI::RenderDevice* device, const std::string& resolvedPath) {
    if (resolvedPath.empty()) return;
    for (u32 tier = 0; tier <= kTextureQualityMaxTier; ++tier) {
        for (bool srgb : {false, true}) {
            const std::string key = colorSpaceKey(textureCacheKeyForTier(resolvedPath, tier), srgb);
            auto it = m_cache.find(key);
            if (it == m_cache.end()) continue;
            Entry& entry = it->second;
            if (entry.refCount > 0) entry.refCount--;
            if (entry.refCount == 0) {
                destroyEntry(device, entry);
                m_fast.clear();  // its entries point into m_cache
                m_cache.erase(it);
            }
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
                m_fast.clear();  // its entries point into m_cache
                m_cache.erase(it);
            }
            return;
        }
    }
}

RHI::Texture* GpuTextureCache::whiteTexture(RHI::RenderDevice* device) {
    if (m_white && m_whiteDevice == device) return m_white;
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
    m_whiteDevice = device;
    return m_white;
}

void GpuTextureCache::invalidate(const std::string& resolvedPath) {
    m_fast.clear();
    for (u32 tier = 0; tier <= kTextureQualityMaxTier; ++tier) {
        for (bool srgb : {false, true}) {
            const std::string key = colorSpaceKey(textureCacheKeyForTier(resolvedPath, tier), srgb);
            auto it = m_cache.find(key);
            if (it != m_cache.end()) {
                it->second.fileStamp = 0;
            }
        }
    }
    auto it = m_cache.find(resolvedPath);
    if (it != m_cache.end()) {
        it->second.fileStamp = 0;
    }
}

void GpuTextureCache::releaseAll(RHI::RenderDevice* device) {
    m_fast.clear();
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!device || it->second.device == device) {
            destroyEntry(device, it->second);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_retired.begin(); it != m_retired.end();) {
        if (!device || it->first == device) {
            if (it->first && it->second) it->first->destroyTexture(it->second);
            it = m_retired.erase(it);
        } else {
            ++it;
        }
    }
    if (m_white && (!device || m_whiteDevice == device)) {
        if (device) device->destroyTexture(m_white);
        m_white = nullptr;
        m_whiteDevice = nullptr;
    }
}

}  // namespace Caffeine::Render

#endif
