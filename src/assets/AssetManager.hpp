#pragma once

#include "assets/AssetTypes.hpp"
#include "assets/AssetHandle.hpp"
#include "core/io/CafTypes.hpp"
#include "memory/LinearAllocator.hpp"
#include "threading/JobSystem.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Caffeine::Assets {

using namespace Caffeine;

class AssetManager {
public:
    explicit AssetManager(Threading::JobSystem* jobs,
                          const char*           basePath,
                          u64                   maxCacheMB = 256);
    ~AssetManager();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    template<typename T>
    AssetHandle<T> loadAsync(const char* path) {
        u32 id = acquireOrCreate(path, AssetTypeTrait<T>::cafType);
        scheduleLoad(id);
        return AssetHandle<T>(this, id);
    }

    template<typename T>
    AssetHandle<T> loadSync(const char* path) {
        u32 id = acquireOrCreate(path, AssetTypeTrait<T>::cafType);
        loadInternal(id);
        return AssetHandle<T>(this, id);
    }

    void       collectGarbage();
    CacheStats cacheStats() const;
    void       tick(u64 frameIndex = 0);

    void       incRef(u32 id);
    void       decRef(u32 id);
    LoadStatus getStatus(u32 id) const;

    template<typename T>
    const T* getResolved(u32 id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= static_cast<u32>(m_assets.size())) return nullptr;
        const AssetEntry& e = *m_assets[id];
        if (e.status.load(std::memory_order_acquire) != LoadStatus::Loaded) return nullptr;
        if constexpr (std::is_same_v<T, Texture>)    return &e.resolved.texture;
        if constexpr (std::is_same_v<T, AudioClip>)  return &e.resolved.audio;
        if constexpr (std::is_same_v<T, ShaderBlob>) return &e.resolved.shader;
        return nullptr;
    }

private:
    struct ResolvedData {
        Texture    texture  {};
        AudioClip  audio    {};
        ShaderBlob shader   {};
    };

    struct AssetEntry {
        std::string                      path;
        AssetType                        cafType         = AssetType::Unknown;
        std::atomic<LoadStatus>          status          { LoadStatus::Unloaded };
        std::unique_ptr<LinearAllocator> allocator;
        const CafHeader*                 header          = nullptr;
        const void*                      metadata        = nullptr;
        const void*                      payload         = nullptr;
        ResolvedData                     resolved        {};
        std::atomic<u32>                 refCount        { 0 };
        u64                              sizeBytes       = 0;
        u64                              lastAccessFrame = 0;
#ifdef CF_DEBUG
        u64                              lastWriteTime   = 0;
#endif
    };

    u32  acquireOrCreate(const char* path, AssetType type);
    void scheduleLoad(u32 id);
    void loadInternal(u32 id);
    void resolveEntry(AssetEntry& e);
    void resolveTexture(AssetEntry& e);
    void resolveAudio(AssetEntry& e);
    void resolveShader(AssetEntry& e);

#ifdef CF_DEBUG
    u64  getFileWriteTime(const std::string& path);
    void checkHotReload();
#endif

    mutable std::mutex                        m_mutex;
    std::vector<std::unique_ptr<AssetEntry>>  m_assets;
    std::unordered_map<std::string, u32>      m_pathIndex;

    Threading::JobSystem* m_jobs;
    std::string           m_basePath;
    u64                   m_maxCacheBytes;

    std::atomic<u64>      m_cachedBytes   { 0 };
    std::atomic<u32>      m_pendingJobs   { 0 };

    u64                   m_totalLoads    = 0;
    u64                   m_cacheHits     = 0;
    u64                   m_frame         = 0;
};

// ============================================================================
// AssetHandle<T> method definitions (require full AssetManager definition)
// ============================================================================

template<typename T>
AssetHandle<T>::AssetHandle()
    : m_mgr(nullptr), m_id(~u32(0)) {}

template<typename T>
AssetHandle<T>::AssetHandle(AssetManager* mgr, u32 id)
    : m_mgr(mgr), m_id(id)
{
    if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id);
}

template<typename T>
AssetHandle<T>::~AssetHandle() { reset(); }

template<typename T>
AssetHandle<T>::AssetHandle(const AssetHandle& o)
    : m_mgr(o.m_mgr), m_id(o.m_id)
{
    if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id);
}

template<typename T>
AssetHandle<T>& AssetHandle<T>::operator=(const AssetHandle& o) {
    if (this != &o) {
        reset();
        m_mgr = o.m_mgr;
        m_id  = o.m_id;
        if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id);
    }
    return *this;
}

template<typename T>
AssetHandle<T>::AssetHandle(AssetHandle&& o) noexcept
    : m_mgr(o.m_mgr), m_id(o.m_id)
{
    o.m_mgr = nullptr;
    o.m_id  = ~u32(0);
}

template<typename T>
AssetHandle<T>& AssetHandle<T>::operator=(AssetHandle&& o) noexcept {
    if (this != &o) {
        reset();
        m_mgr   = o.m_mgr;
        m_id    = o.m_id;
        o.m_mgr = nullptr;
        o.m_id  = ~u32(0);
    }
    return *this;
}

template<typename T>
bool AssetHandle<T>::isValid() const {
    return m_mgr != nullptr && m_id != ~u32(0);
}

template<typename T>
bool AssetHandle<T>::isReady() const {
    return isValid() && m_mgr->getStatus(m_id) == LoadStatus::Loaded;
}

template<typename T>
const T* AssetHandle<T>::get() const {
    if (!isReady()) return nullptr;
    return m_mgr->getResolved<T>(m_id);
}

template<typename T>
AssetHandle<T>::operator bool() const { return isReady(); }

template<typename T>
void AssetHandle<T>::reset() {
    if (m_mgr && m_id != ~u32(0)) m_mgr->decRef(m_id);
    m_mgr = nullptr;
    m_id  = ~u32(0);
}

} // namespace Caffeine::Assets
