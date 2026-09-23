#pragma once

#include "core/Types.hpp"

#ifdef CF_HAS_SDL3

#include "rhi/RenderDevice.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::Render {

/// Global GPU texture cache: path → RHI::Texture with refcount and mipmaps.
/// Decode happens once per path; render loop must only call acquire() (cache hit).
class GpuTextureCache {
public:
    static GpuTextureCache& instance();

    /// Returns a cached 2D texture (RGBA8). Increments refcount.
    /// qualityTier: 0=full, 1=half, 2=quarter, 3=eighth resolution.
    RHI::Texture* acquire(RHI::RenderDevice* device, const std::string& path,
                          const std::string& projectRoot, u32 qualityTier = 0);

    /// Uploads pixel data under a stable cache key (embedded glTF images, etc.).
    RHI::Texture* acquireFromPixels(RHI::RenderDevice* device, const std::string& cacheKey,
                                    const u8* pixels, u32 width, u32 height, int channels,
                                    u32 qualityTier = 0);

    /// Decrement refcount for a resolved filesystem path.
    void release(RHI::RenderDevice* device, const std::string& resolvedPath);

    /// Decrement refcount by texture pointer (linear lookup).
    void release(RHI::RenderDevice* device, RHI::Texture* texture);

    RHI::Texture* whiteTexture(RHI::RenderDevice* device);

    /// Drop cache entry if file changed on disk (next acquire reloads).
    void invalidate(const std::string& resolvedPath);

    void releaseAll(RHI::RenderDevice* device);

    usize cachedEntryCount() const { return m_cache.size(); }

private:
    struct Entry {
        RHI::Texture* texture     = nullptr;
        u32           refCount    = 0;
        u64           fileStamp   = 0;
        u32           width       = 0;
        u32           height      = 0;
        u32           mipLevels   = 1;
    };

    std::string resolvePath(const std::string& path, const std::string& projectRoot) const;
    u64         fileTimestamp(const std::string& path) const;
    Entry*      findByTexture(RHI::Texture* texture);
    RHI::Texture* loadTexture(RHI::RenderDevice* device, const std::string& resolved,
                              u32 qualityTier);
    static void downscaleRgba(std::vector<u8>& rgba, u32& width, u32& height, u32 qualityTier);
    void        destroyEntry(RHI::RenderDevice* device, Entry& entry);

    std::unordered_map<std::string, Entry> m_cache;
    RHI::Texture* m_white = nullptr;
};

}  // namespace Caffeine::Render

#endif
