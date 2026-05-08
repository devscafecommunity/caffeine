// ============================================================================
// @file    AssetTypes.hpp
// @brief   Asset runtime types for the AssetManager.
//
//  Texture, AudioClip, ShaderBlob are zero-copy views into the memory-mapped
//  .caf payload.  CacheStats and AssetTypeTrait are helpers for the manager.
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"

namespace Caffeine::Assets {

using namespace Caffeine;

// ============================================================================
// Load state of a single asset slot
// ============================================================================
enum class LoadStatus : u8 {
    Unloaded = 0,
    Loading,
    Loaded,
    Failed
};

// ============================================================================
// Runtime asset views (zero-copy — pointers into LinearAllocator buffer)
// ============================================================================

struct Texture {
    u32       width         = 0;
    u32       height        = 0;
    u32       format        = 0;   // SDL_GPUTextureFormat or 0 = RGBA8
    u32       mipLevels     = 1;
    const u8* pixels        = nullptr;
    u64       pixelDataSize = 0;
};

struct AudioClip {
    u32       sampleRate    = 0;
    u16       channels      = 0;
    u16       bitsPerSample = 0;
    u32       sampleCount   = 0;
    const u8* pcmData       = nullptr;
    u64       pcmDataSize   = 0;
};

struct ShaderBlob {
    u32       stage        = 0;   // 0=vertex, 1=fragment, 2=compute
    const u8* bytecode     = nullptr;
    u64       bytecodeSize = 0;
};

// ============================================================================
// CacheStats — returned by AssetManager::cacheStats()
// ============================================================================
struct CacheStats {
    u64 totalCachedBytes = 0;
    u64 maxCacheBytes    = 0;
    u32 textureCount     = 0;
    u32 audioCount       = 0;
    u32 pendingJobs      = 0;
    f32 cacheHitRate     = 0.0f;
};

// ============================================================================
// AssetTypeTrait — maps C++ type → CafTypes AssetType discriminator
// ============================================================================
template<typename T> struct AssetTypeTrait;

template<> struct AssetTypeTrait<Texture> {
    static constexpr AssetType cafType = AssetType::Texture;
};
template<> struct AssetTypeTrait<AudioClip> {
    static constexpr AssetType cafType = AssetType::Audio;
};
template<> struct AssetTypeTrait<ShaderBlob> {
    static constexpr AssetType cafType = AssetType::Shader;
};

} // namespace Caffeine::Assets
