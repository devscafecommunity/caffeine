#pragma once

#include "assets/AssetTypes.hpp"
#include "assets/AssetHandle.hpp"
#include "core/io/CafTypes.hpp"
#include "memory/LinearAllocator.hpp"
#include "threading/JobSystem.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caffeine::Assets {

using namespace Caffeine;

/// How a path given by a component, an Inspector field or a generator node was understood.
enum class ReferenceStatus : u8 {
    Ok,              // a file inside the project (or an engine built-in)
    Empty,           // no path at all
    Missing,         // nothing at that path in the project, the assets folder or the engine's assets
    OutsideProject,  // a real file, but not inside the project: it has to be imported first
    NotAFile,        // a folder, not an asset
};

/// Where a valid reference lives.
enum class ReferenceOrigin : u8 {
    Project,  // under the project root (usually its assets folder)
    Engine,   // an engine built-in, referenced by its own relative path
};

/// The one form in which anything may refer to an asset: a project-relative path with `/` separators.
struct AssetReference {
    ReferenceStatus status     = ReferenceStatus::Empty;
    ReferenceOrigin origin     = ReferenceOrigin::Project;
    std::string     projectPath;   // relative to the project root: what scenes and .mat files store
    std::string     assetPath;     // relative to the assets folder when the file is in it (what the picker
                                   // lists and components store), else the same as projectPath
    std::string     absolutePath;  // where the file really is (also set for OutsideProject)
    std::string     kind;          // image, mesh, audio, material, script, animation, generator, asset, file
    bool            imported = false;  // this call copied the file into the project

    bool ok() const { return status == ReferenceStatus::Ok; }
};

class AssetManager {
public:
    explicit AssetManager(Threading::JobSystem* jobs,
                          const char*           basePath,
                          u64                   maxCacheMB = 256);
    ~AssetManager();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// The handles below hold a reference from the moment the slot is found, so a load that finishes before
    /// the caller's handle exists (a tiny budget, a fast worker) cannot be evicted in between. A path that
    /// escapes the base folder, or one already loaded as another type, gives an empty handle.
    template<typename T>
    AssetHandle<T> loadAsync(const char* path) {
        const u32 id = acquireOrCreate(path, AssetTypeTrait<T>::cafType);
        if (id == ~u32(0)) return AssetHandle<T>();
        AssetHandle<T> handle = makeHandle<T>(id);
        unpin(id);
        scheduleLoad(id);
        return handle;
    }

    template<typename T>
    AssetHandle<T> loadSync(const char* path) {
        const u32 id = acquireOrCreate(path, AssetTypeTrait<T>::cafType);
        if (id == ~u32(0)) return AssetHandle<T>();
        AssetHandle<T> handle = makeHandle<T>(id);
        unpin(id);
        loadBlocking(id);
        return handle;
    }

    // ------------------------------------------------------------------
    // Project references. Every asset a component, an Inspector field or a generator node names goes through
    // here: the result is a file that is *in the project* (or an engine built-in), written as a path relative
    // to the project root. Nothing outside the project survives as a reference, so a project that moves to
    // another machine still finds everything it points at.
    // ------------------------------------------------------------------
    /// The project folder; references are relative to it.
    void setProjectRoot(const std::string& path);
    /// Where imported files land (default `<project>/assets`). A folder inside the project.
    void setAssetRoot(const std::string& path);
    /// The engine's own assets (built-in textures and meshes), referenced by their relative path.
    void setEngineAssetRoot(const std::string& path);
    const std::string& projectRoot() const { return m_projectRoot; }
    const std::string& assetRoot() const { return m_assetRoot; }

    /// Understands `path` without touching the disk's contents: relative to the project, to the assets
    /// folder or to the engine's assets, or absolute. `Ok` only for a file inside the project or the engine.
    AssetReference resolveReference(const std::string& path) const;
    /// [`resolveReference`], and a real file outside the project is copied into the assets folder (in the
    /// folder of its kind: `Textures`, `Models`...) first. Identical files are not duplicated and names
    /// that are taken get a number.
    AssetReference importReference(const std::string& path);

    /// image, mesh, audio, material, script, animation, generator, asset, file (or folder) for a path.
    static std::string classify(const std::string& path, bool isDirectory = false);
    /// `a/./b//c.png` and `a\\b\\c.png` as `a/b/c.png`; false when it climbs out with `..`.
    static bool normalizeRelative(const std::string& path, std::string& out);

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
    void unpin(u32 id);
    /// Takes the load of a slot for the caller: true when the caller must load it (it was not loading, not
    /// ready); the other states mean there is nothing to do (ready) or someone else is on it (loading).
    bool claimLoad(u32 id, LoadStatus& seen);
    void scheduleLoad(u32 id);
    /// Loads on this thread, or waits for the thread that is already loading the slot.
    void loadBlocking(u32 id);
    void loadInternal(u32 id);
    /// Runs the invalidation callbacks queued while the lock was held, outside it (a callback may call back
    /// into the manager).
    void flushNotifications();
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
    std::vector<InvalidatedAsset>                m_pendingNotifications;
    std::condition_variable                      m_loadDone;

    std::string m_projectRoot;
    std::string m_assetRoot;
    std::string m_engineAssetRoot;

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
