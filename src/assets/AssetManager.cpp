#include "assets/AssetManager.hpp"
#include "core/io/BlobLoader.hpp"

#include <cassert>
#include <cstdio>

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
    entry->path    = key;
    entry->cafType = type;
    m_assets.push_back(std::move(entry));
    m_pathIndex[key] = id;
    ++m_totalLoads;
    return id;
}

void AssetManager::scheduleLoad(u32 id) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AssetEntry& e      = *m_assets[id];
        LoadStatus  status = e.status.load(std::memory_order_acquire);
        if (status == LoadStatus::Loading || status == LoadStatus::Loaded) return;
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
        if (status == LoadStatus::Loading || status == LoadStatus::Loaded) {
            if (status != LoadStatus::Loading) return;
        }
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

    e.status.store(LoadStatus::Loaded, std::memory_order_release);
}

void AssetManager::resolveEntry(AssetEntry& e) {
    switch (e.header->type) {
        case AssetType::Texture: resolveTexture(e); break;
        case AssetType::Audio:   resolveAudio(e);   break;
        case AssetType::Shader:  resolveShader(e);  break;
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

void AssetManager::collectGarbage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_assets) {
        if (!entry) continue;
        if (entry->refCount.load(std::memory_order_acquire) != 0) continue;
        if (entry->status.load(std::memory_order_acquire) != LoadStatus::Loaded) continue;

        m_cachedBytes.fetch_sub(entry->sizeBytes, std::memory_order_relaxed);
        entry->allocator.reset();
        entry->header    = nullptr;
        entry->metadata  = nullptr;
        entry->payload   = nullptr;
        entry->resolved  = {};
        entry->sizeBytes = 0;
        entry->status.store(LoadStatus::Unloaded, std::memory_order_release);
    }
}

CacheStats AssetManager::cacheStats() const {
    CacheStats stats{};
    stats.totalCachedBytes = m_cachedBytes.load(std::memory_order_relaxed);
    stats.maxCacheBytes    = m_maxCacheBytes;
    stats.pendingJobs      = m_pendingJobs.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_assets) {
        if (!entry) continue;
        if (entry->status.load(std::memory_order_relaxed) != LoadStatus::Loaded) continue;
        switch (entry->cafType) {
            case AssetType::Texture: ++stats.textureCount; break;
            case AssetType::Audio:   ++stats.audioCount;   break;
            default: break;
        }
    }
    stats.cacheHitRate = (m_totalLoads > 0)
        ? static_cast<f32>(m_cacheHits) / static_cast<f32>(m_totalLoads)
        : 0.0f;

    return stats;
}

void AssetManager::tick(u64 frameIndex) {
    m_frame = frameIndex;
#ifdef CF_DEBUG
    checkHotReload();
#endif
}

void AssetManager::incRef(u32 id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id < static_cast<u32>(m_assets.size()) && m_assets[id]) {
        m_assets[id]->refCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void AssetManager::decRef(u32 id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id < static_cast<u32>(m_assets.size()) && m_assets[id]) {
        m_assets[id]->refCount.fetch_sub(1, std::memory_order_acq_rel);
    }
}

LoadStatus AssetManager::getStatus(u32 id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) {
        return LoadStatus::Unloaded;
    }
    return m_assets[id]->status.load(std::memory_order_acquire);
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

        auto& e = *m_assets[id];
        m_cachedBytes.fetch_sub(e.sizeBytes, std::memory_order_relaxed);
        e.allocator.reset();
        e.header    = nullptr;
        e.metadata  = nullptr;
        e.payload   = nullptr;
        e.resolved  = {};
        e.sizeBytes = 0;
        e.status.store(LoadStatus::Unloaded, std::memory_order_release);
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
            if (e->status.load(std::memory_order_acquire) != LoadStatus::Loaded) continue;
            u64 current = getFileWriteTime(e->path);
            if (current != 0 && current != e->lastWriteTime) {
                toReload.push_back(i);
            }
        }
    }

    for (u32 id : toReload) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto& e = *m_assets[id];
            m_cachedBytes.fetch_sub(e.sizeBytes, std::memory_order_relaxed);
            e.allocator.reset();
            e.header    = nullptr;
            e.metadata  = nullptr;
            e.payload   = nullptr;
            e.resolved  = {};
            e.sizeBytes = 0;
            e.status.store(LoadStatus::Unloaded, std::memory_order_release);
        }
        loadInternal(id);
    }
}
#endif

} // namespace Caffeine::Assets
