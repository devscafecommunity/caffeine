#include "assets/AssetManager.hpp"
#include "core/io/BlobLoader.hpp"
#include "tools/PipelineTypes.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

#ifdef CF_DEBUG
#include <filesystem>
#endif

namespace Caffeine::Assets {

using namespace Caffeine;

AssetManager::AssetManager(Threading::JobSystem* jobs,
                            const char*           basePath,
                            u64                   maxCacheMB)
    : m_jobs(jobs)
    , m_basePath(basePath ? basePath : "")
    , m_maxCacheBytes(maxCacheMB * 1024ull * 1024ull)
{
    if (!m_basePath.empty() &&
        m_basePath.back() != '/' &&
        m_basePath.back() != '\\')
    {
        m_basePath += '/';
    }
}

AssetManager::~AssetManager() = default;

u32 AssetManager::acquireOrCreate(const char* path, AssetType type) {
    std::string key = m_basePath + path;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_pathIndex.find(key);
    if (it != m_pathIndex.end()) {
        u32 id = it->second;
        m_assets[id]->lastAccessFrame = m_frame;
        ++m_cacheHits;
        ++m_totalLoads;
        return id;
    }

    u32  id    = static_cast<u32>(m_assets.size());
    auto entry = std::make_unique<AssetEntry>();
    entry->path       = key;
    entry->cafType    = type;
    entry->generation = 1;
    entry->status.store(LoadStatus::Pending, std::memory_order_release);
    m_assets.push_back(std::move(entry));
    m_pathIndex[key] = id;
    ++m_totalLoads;
    return id;
}

void AssetManager::clearEntryData(AssetEntry& e) {
    m_cachedBytes.fetch_sub(e.sizeBytes, std::memory_order_relaxed);
    e.allocator.reset();
    e.header    = nullptr;
    e.metadata  = nullptr;
    e.payload   = nullptr;
    e.resolved  = {};
    e.sizeBytes = 0;
}

void AssetManager::touchEntry(AssetEntry& e) {
    e.lastAccessFrame = m_frame;
}

void AssetManager::notifyInvalidated(u32 id, u16 generation, AssetType type) {
    if (m_invalidationListeners.empty()) return;

    const InvalidatedAsset info{id, generation, type};
    for (const auto& listener : m_invalidationListeners) {
        if (listener.callback) {
            listener.callback(info, listener.userData);
        }
    }
}

void AssetManager::invalidateEntry(u32 id, AssetEntry& e) {
    const u16 invalidatedGeneration = e.generation;
    const AssetType type = e.cafType;
    clearEntryData(e);
    ++e.generation;
    if (e.generation == 0) e.generation = 1;
    e.status.store(LoadStatus::Invalid, std::memory_order_release);
    ++m_evictedCount;
    notifyInvalidated(id, invalidatedGeneration, type);
}

void AssetManager::evictLruToFitBudget() {
    while (m_cachedBytes.load(std::memory_order_relaxed) > m_maxCacheBytes) {
        u32 victimId = ~u32(0);
        u64 oldestFrame = std::numeric_limits<u64>::max();

        for (u32 i = 0; i < static_cast<u32>(m_assets.size()); ++i) {
            const auto& entry = m_assets[i];
            if (!entry) continue;
            if (entry->refCount.load(std::memory_order_acquire) != 0) continue;
            if (entry->status.load(std::memory_order_acquire) != LoadStatus::Ready) continue;
            if (entry->lastAccessFrame <= oldestFrame) {
                oldestFrame = entry->lastAccessFrame;
                victimId = i;
            }
        }

        if (victimId == ~u32(0)) break;
        invalidateEntry(victimId, *m_assets[victimId]);
    }
}

void AssetManager::scheduleLoad(u32 id) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AssetEntry& e      = *m_assets[id];
        LoadStatus  status = e.status.load(std::memory_order_acquire);
        if (status == LoadStatus::Loading || status == LoadStatus::Ready) return;
        e.status.store(LoadStatus::Loading, std::memory_order_release);
    }

    m_pendingJobs.fetch_add(1, std::memory_order_relaxed);

    if (m_jobs) {
        m_jobs->scheduleData(id, [this](u32& assetId) {
            loadInternal(assetId);
            m_pendingJobs.fetch_sub(1, std::memory_order_relaxed);
        }, nullptr, Threading::JobPriority::Background);
    } else {
        loadInternal(id);
        m_pendingJobs.fetch_sub(1, std::memory_order_relaxed);
    }
}

void AssetManager::loadInternal(u32 id) {
    AssetEntry* eptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= static_cast<u32>(m_assets.size())) return;
        eptr = m_assets[id].get();
        LoadStatus status = eptr->status.load(std::memory_order_acquire);
        if (status == LoadStatus::Ready) return;
        eptr->status.store(LoadStatus::Loading, std::memory_order_release);
    }

    AssetEntry& e = *eptr;

    FILE* f = std::fopen(e.path.c_str(), "rb");
    if (!f) {
        e.status.store(LoadStatus::Failed, std::memory_order_release);
        return;
    }
    std::fseek(f, 0, SEEK_END);
    u64 fileSize = static_cast<u64>(std::ftell(f));
    std::fclose(f);

    if (fileSize == 0) {
        e.status.store(LoadStatus::Failed, std::memory_order_release);
        return;
    }

    auto alloc = std::make_unique<LinearAllocator>(static_cast<usize>(fileSize + 64));
    IO::BlobLoader::LoadResult result = IO::BlobLoader::load(e.path.c_str(), alloc.get());

    if (!result.valid) {
        e.status.store(LoadStatus::Failed, std::memory_order_release);
        return;
    }

    e.allocator = std::move(alloc);
    e.header    = result.header;
    e.metadata  = result.metadata;
    e.payload   = result.payload;
    e.sizeBytes = fileSize;

    resolveEntry(e);

    m_cachedBytes.fetch_add(fileSize, std::memory_order_relaxed);

#ifdef CF_DEBUG
    e.lastWriteTime = getFileWriteTime(e.path);
#endif

    e.status.store(LoadStatus::Ready, std::memory_order_release);
    e.lastAccessFrame = m_frame;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        evictLruToFitBudget();
    }
}

void AssetManager::resolveEntry(AssetEntry& e) {
    switch (e.header->type) {
        case AssetType::Texture: resolveTexture(e); break;
        case AssetType::Audio:   resolveAudio(e);   break;
        case AssetType::Shader:  resolveShader(e);  break;
        case AssetType::Mesh:    resolveMesh(e);    break;
        case AssetType::Prefab:  resolvePrefab(e);  break;
        default: break;
    }
}

void AssetManager::resolveTexture(AssetEntry& e) {
    const auto* meta = static_cast<const TextureMetadata*>(e.metadata);
    e.resolved.texture = Texture{
        meta->width,
        meta->height,
        meta->format,
        meta->mipLevels,
        static_cast<const u8*>(e.payload),
        e.header->dataSize
    };
}

void AssetManager::resolveAudio(AssetEntry& e) {
    const auto* meta = static_cast<const AudioMetadata*>(e.metadata);
    e.resolved.audio = AudioClip{
        meta->sampleRate,
        meta->channels,
        meta->bitsPerSample,
        meta->sampleCount,
        static_cast<const u8*>(e.payload),
        e.header->dataSize
    };
}

void AssetManager::resolveShader(AssetEntry& e) {
    const auto* meta = static_cast<const ShaderMetadata*>(e.metadata);
    e.resolved.shader = ShaderBlob{
        meta->stage,
        static_cast<const u8*>(e.payload),
        e.header->dataSize
    };
}

void AssetManager::resolveMesh(AssetEntry& e) {
    const auto* meta = static_cast<const Tools::MeshMetadata*>(e.metadata);
    const u8* payload = static_cast<const u8*>(e.payload);

    u64 vertexDataSize = static_cast<u64>(meta->vertexCount) * sizeof(Vertex3D);
    const Vertex3D* vertices = reinterpret_cast<const Vertex3D*>(payload);
    const u32* indices = reinterpret_cast<const u32*>(payload + vertexDataSize);

    e.resolved.mesh = Mesh{
        vertices,
        meta->vertexCount,
        indices,
        meta->indexCount,
        vertexDataSize,
        static_cast<u64>(meta->indexCount) * sizeof(u32)
    };
}

void AssetManager::resolvePrefab(AssetEntry& e) {
    e.resolved.prefab = Prefab{
        static_cast<const u8*>(e.payload),
        e.header->dataSize
    };
}

void AssetManager::collectGarbage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (u32 i = 0; i < static_cast<u32>(m_assets.size()); ++i) {
        auto& entry = m_assets[i];
        if (!entry) continue;
        if (entry->refCount.load(std::memory_order_acquire) != 0) continue;
        if (entry->status.load(std::memory_order_acquire) != LoadStatus::Ready) continue;
        invalidateEntry(i, *entry);
    }
    evictLruToFitBudget();
}

CacheStats AssetManager::cacheStats() const {
    CacheStats stats{};
    stats.totalCachedBytes = m_cachedBytes.load(std::memory_order_relaxed);
    stats.maxCacheBytes    = m_maxCacheBytes;
    stats.pendingJobs      = m_pendingJobs.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_assets) {
        if (!entry) continue;
        if (entry->status.load(std::memory_order_relaxed) != LoadStatus::Ready) continue;
        switch (entry->cafType) {
            case AssetType::Texture: ++stats.textureCount; break;
            case AssetType::Audio:   ++stats.audioCount;   break;
            default: break;
        }
    }
    stats.cacheHitRate = (m_totalLoads > 0)
        ? static_cast<f32>(m_cacheHits) / static_cast<f32>(m_totalLoads)
        : 0.0f;
    stats.evictedCount = static_cast<u32>(m_evictedCount);

    return stats;
}

void AssetManager::tick(u64 frameIndex) {
    m_frame = frameIndex;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        evictLruToFitBudget();
    }
#ifdef CF_DEBUG
    checkHotReload();
#endif
}

void AssetManager::registerInvalidationCallback(AssetInvalidationCallback callback,
                                                 void* userData) {
    if (!callback) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& listener : m_invalidationListeners) {
        if (listener.callback == callback && listener.userData == userData) return;
    }
    m_invalidationListeners.push_back({callback, userData});
}

void AssetManager::unregisterInvalidationCallback(AssetInvalidationCallback callback,
                                                   void* userData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_invalidationListeners.erase(
        std::remove_if(m_invalidationListeners.begin(), m_invalidationListeners.end(),
                       [&](const InvalidationListener& listener) {
                           return listener.callback == callback &&
                                  listener.userData == userData;
                       }),
        m_invalidationListeners.end());
}

void AssetManager::incRef(u32 id, u16 generation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return;
    AssetEntry& e = *m_assets[id];
    if (e.generation != generation) return;
    touchEntry(e);
    e.refCount.fetch_add(1, std::memory_order_relaxed);
}

void AssetManager::decRef(u32 id, u16 generation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return;
    AssetEntry& e = *m_assets[id];
    if (e.generation != generation) return;
    e.refCount.fetch_sub(1, std::memory_order_acq_rel);
}

LoadStatus AssetManager::getStatus(u32 id, u16 generation) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) {
        return LoadStatus::Invalid;
    }
    const AssetEntry& e = *m_assets[id];
    if (e.generation != generation) return LoadStatus::Invalid;
    return e.status.load(std::memory_order_acquire);
}

bool AssetManager::isHandleAlive(u32 id, u16 generation) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return false;
    return m_assets[id]->generation == generation;
}

u16 AssetManager::entryGeneration(u32 id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return 0;
    return m_assets[id]->generation;
}

u32 AssetManager::reloadAsset(const char* path) {
    if (!path) return ~0u;

    std::string key = m_basePath + path;
    u32 id = ~0u;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pathIndex.find(key);
        if (it == m_pathIndex.end()) return ~0u;
        id = it->second;
        clearEntryData(*m_assets[id]);
        m_assets[id]->status.store(LoadStatus::Pending, std::memory_order_release);
    }

    loadInternal(id);
    return id;
}

#ifdef CF_DEBUG
u64 AssetManager::getFileWriteTime(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<u64>(t.time_since_epoch().count());
}

void AssetManager::checkHotReload() {
    std::vector<u32> toReload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (u32 i = 0; i < static_cast<u32>(m_assets.size()); ++i) {
            const auto& e = m_assets[i];
            if (!e) continue;
            if (e->status.load(std::memory_order_acquire) != LoadStatus::Ready) continue;
            u64 current = getFileWriteTime(e->path);
            if (current != 0 && current != e->lastWriteTime) {
                toReload.push_back(i);
            }
        }
    }

    for (u32 id : toReload) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            clearEntryData(*m_assets[id]);
            m_assets[id]->status.store(LoadStatus::Pending, std::memory_order_release);
        }
        loadInternal(id);
    }
}
#endif

} // namespace Caffeine::Assets
