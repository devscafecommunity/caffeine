#pragma once

#include "assets/AssetTypes.hpp"
#include "assets/AssetHandle.hpp"
#include "core/io/CafTypes.hpp"
#include "memory/LinearAllocator.hpp"
#include "threading/JobSystem.hpp"

#include <atomic>
#include <functional>
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
        return makeHandle<T>(id);
    }

    template<typename T>
    AssetHandle<T> loadSync(const char* path) {
        u32 id = acquireOrCreate(path, AssetTypeTrait<T>::cafType);
        loadInternal(id);
        return makeHandle<T>(id);
    }

    void       collectGarbage();
    CacheStats cacheStats() const;
    void       tick(u64 frameIndex = 0);

    void registerInvalidationCallback(AssetInvalidationCallback callback,
                                      void* userData = nullptr);
    void unregisterInvalidationCallback(AssetInvalidationCallback callback,
                                          void* userData = nullptr);

    // Reload an already-loaded asset from disk by its loaded .caf path.
    // Returns the asset ID, or ~0u if not found. Thread-safe.
    u32        reloadAsset(const char* path);

    void       incRef(u32 id, u16 generation);
    void       decRef(u32 id, u16 generation);
    LoadStatus getStatus(u32 id, u16 generation) const;
    bool       isHandleAlive(u32 id, u16 generation) const;
    u16        entryGeneration(u32 id) const;

    template<typename T>
    const T* getResolved(u32 id, u16 generation) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return nullptr;
        const AssetEntry& e = *m_assets[id];
        if (e.generation != generation) return nullptr;
        if (e.status.load(std::memory_order_acquire) != LoadStatus::Ready) return nullptr;
        e.lastAccessFrame = m_frame;
        if constexpr (std::is_same_v<T, Texture>)    return &e.resolved.texture;
        if constexpr (std::is_same_v<T, AudioClip>)  return &e.resolved.audio;
        if constexpr (std::is_same_v<T, ShaderBlob>) return &e.resolved.shader;
        if constexpr (std::is_same_v<T, Mesh>)       return &e.resolved.mesh;
        if constexpr (std::is_same_v<T, Prefab>)     return &e.resolved.prefab;
        return nullptr;
    }

private:
    struct ResolvedData {
        Texture    texture  {};
        AudioClip  audio    {};
        ShaderBlob shader   {};
        Mesh       mesh     {};
        Prefab     prefab   {};
    };

    struct AssetEntry {
        std::string                      path;
        AssetType                        cafType         = AssetType::Unknown;
        std::atomic<LoadStatus>          status          { LoadStatus::Pending };
        u16                              generation      = 1;
        std::unique_ptr<LinearAllocator> allocator;
        const CafHeader*                 header          = nullptr;
        const void*                      metadata        = nullptr;
        const void*                      payload         = nullptr;
        ResolvedData                     resolved        {};
        std::atomic<u32>                 refCount        { 0 };
        u64                              sizeBytes       = 0;
        mutable u64                      lastAccessFrame = 0;
#ifdef CF_DEBUG
        u64                              lastWriteTime   = 0;
#endif
    };

    template<typename T>
    AssetHandle<T> makeHandle(u32 id) {
        return AssetHandle<T>(this, id, entryGeneration(id));
    }

    u32  acquireOrCreate(const char* path, AssetType type);
    void scheduleLoad(u32 id);
    void loadInternal(u32 id);
    void clearEntryData(AssetEntry& e);
    void invalidateEntry(u32 id, AssetEntry& e);
    void evictLruToFitBudget();
    void touchEntry(AssetEntry& e);
    void notifyInvalidated(u32 id, u16 generation, AssetType type);
    void resolveEntry(AssetEntry& e);
    void resolveTexture(AssetEntry& e);
    void resolveAudio(AssetEntry& e);
    void resolveShader(AssetEntry& e);
    void resolveMesh(AssetEntry& e);
    void resolvePrefab(AssetEntry& e);

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

    struct InvalidationListener {
        AssetInvalidationCallback callback = nullptr;
        void*                     userData = nullptr;
    };

    std::vector<InvalidationListener> m_invalidationListeners;

    u64                   m_totalLoads    = 0;
    u64                   m_cacheHits     = 0;
    u64                   m_evictedCount  = 0;
    u64                   m_frame         = 0;
};

// ============================================================================
// AssetHandle<T> method definitions (require full AssetManager definition)
// ============================================================================

template<typename T>
AssetHandle<T>::AssetHandle()
    : m_mgr(nullptr), m_id(~u32(0)), m_generation(0) {}

template<typename T>
AssetHandle<T>::AssetHandle(AssetManager* mgr, u32 id, u16 generation)
    : m_mgr(mgr), m_id(id), m_generation(generation)
{
    if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id, m_generation);
}

template<typename T>
AssetHandle<T>::~AssetHandle() { reset(); }

template<typename T>
AssetHandle<T>::AssetHandle(const AssetHandle& o)
    : m_mgr(o.m_mgr), m_id(o.m_id), m_generation(o.m_generation)
{
    if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id, m_generation);
}

template<typename T>
AssetHandle<T>& AssetHandle<T>::operator=(const AssetHandle& o) {
    if (this != &o) {
        reset();
        m_mgr        = o.m_mgr;
        m_id         = o.m_id;
        m_generation = o.m_generation;
        if (m_mgr && m_id != ~u32(0)) m_mgr->incRef(m_id, m_generation);
    }
    return *this;
}

template<typename T>
AssetHandle<T>::AssetHandle(AssetHandle&& o) noexcept
    : m_mgr(o.m_mgr), m_id(o.m_id), m_generation(o.m_generation)
{
    o.m_mgr        = nullptr;
    o.m_id         = ~u32(0);
    o.m_generation = 0;
}

template<typename T>
AssetHandle<T>& AssetHandle<T>::operator=(AssetHandle&& o) noexcept {
    if (this != &o) {
        reset();
        m_mgr        = o.m_mgr;
        m_id         = o.m_id;
        m_generation = o.m_generation;
        o.m_mgr        = nullptr;
        o.m_id         = ~u32(0);
        o.m_generation = 0;
    }
    return *this;
}

template<typename T>
bool AssetHandle<T>::isValid() const {
    return m_mgr != nullptr && m_id != ~u32(0) &&
           m_mgr->isHandleAlive(m_id, m_generation);
}

template<typename T>
bool AssetHandle<T>::isReady() const {
    return isValid() && m_mgr->getStatus(m_id, m_generation) == LoadStatus::Ready;
}

template<typename T>
const T* AssetHandle<T>::get() const {
    if (!isValid()) return nullptr;
    return m_mgr->getResolved<T>(m_id, m_generation);
}

template<typename T>
AssetHandle<T>::operator bool() const { return isReady(); }

template<typename T>
void AssetHandle<T>::reset() {
    if (m_mgr && m_id != ~u32(0)) m_mgr->decRef(m_id, m_generation);
    m_mgr        = nullptr;
    m_id         = ~u32(0);
    m_generation = 0;
}

} // namespace Caffeine::Assets
