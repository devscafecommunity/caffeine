#pragma once

#include "core/Types.hpp"

#ifdef CF_HAS_SDL3

#include "rhi/RenderDevice.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caffeine::Render {

/// Global GPU texture cache: path → RHI::Texture with refcount and mipmaps.
/// Decode happens once per path; render loop must only call acquire() (cache hit).
class GpuTextureCache {
public:
    static GpuTextureCache& instance();

    /// Returns a cached, fully mip-mapped 2D texture. Increments refcount.
    /// qualityTier: 0=full, 1=half, 2=quarter, 3=eighth resolution.
    /// `srgb` = colour data (albedo, emission) decoded to linear by the sampler; leave it off
    /// for normals, ORM and splat weights.
    RHI::Texture* acquire(RHI::RenderDevice* device, const std::string& path,
                          const std::string& projectRoot, u32 qualityTier = 0, bool srgb = false);

    /// Uploads pixel data under a stable cache key (embedded glTF images, etc.).
    RHI::Texture* acquireFromPixels(RHI::RenderDevice* device, const std::string& cacheKey,
                                    const u8* pixels, u32 width, u32 height, int channels,
                                    u32 qualityTier = 0, bool srgb = false);

    /// Decrement refcount for a resolved filesystem path.
    void release(RHI::RenderDevice* device, const std::string& resolvedPath);

    /// Decrement refcount by texture pointer (linear lookup).
    void release(RHI::RenderDevice* device, RHI::Texture* texture);

    RHI::Texture* whiteTexture(RHI::RenderDevice* device);

    /// The mean colour of a picture (linear, from its sRGB texels), cached per file and reloaded when the
    /// file changes. Photon's voxels stand in a textured surface with it. False when the file is missing.
    bool averageColor(const std::string& path, const std::string& projectRoot, Vec3& out);

    /// Drop cache entry if file changed on disk (next acquire reloads).
    void invalidate(const std::string& resolvedPath);

    void releaseAll(RHI::RenderDevice* device);

    usize cachedEntryCount() const { return m_cache.size(); }

private:
    struct Entry {
        RHI::Texture*       texture = nullptr;
        RHI::RenderDevice*  device  = nullptr;
        u32                 refCount = 0;
        u64                 fileStamp = 0;
        u32                 width = 0;
        u32                 height = 0;
    };

    std::string resolvePath(const std::string& path, const std::string& projectRoot) const;
    u64         fileTimestamp(const std::string& path) const;
    Entry*      findByTexture(RHI::Texture* texture);
    RHI::Texture* loadTexture(RHI::RenderDevice* device, const std::string& resolved,
                              u32 qualityTier, bool srgb);
    static void downscaleRgba(std::vector<u8>& rgba, u32& width, u32& height, u32 qualityTier);
    void        destroyEntry(RHI::RenderDevice* device, Entry& entry);

    struct Average {
        u64  fileStamp = 0;
        Vec3 colour{0.5f, 0.5f, 0.5f};
        bool valid = false;
    };
    std::unordered_map<std::string, Average> m_averages;
    std::unordered_map<std::string, Entry> m_cache;
    /// A draw asks for the same textures every frame. Resolving the path and reading the file's timestamp costs a
    /// few file-system calls, so a hit here (path, root, tier and colour space) returns the entry at once and the
    /// disk is looked at again only every kRecheckMs (a hot reload shows up that late). Cleared whenever an entry
    /// is dropped or invalidated, because it points into `m_cache`.
    struct Fast {
        Entry* entry = nullptr;
        u64    recheckAtMs = 0;
    };
    std::unordered_map<std::string, Fast> m_fast;
    static constexpr u64 kRecheckMs = 500;
    /// Textures replaced by an in-place file change while other systems still
    /// held the old pointer. Kept alive until their device shuts down.
    std::vector<std::pair<RHI::RenderDevice*, RHI::Texture*>> m_retired;
    RHI::Texture* m_white = nullptr;
    RHI::RenderDevice* m_whiteDevice = nullptr;
};

}  // namespace Caffeine::Render

#endif
