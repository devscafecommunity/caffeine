#include "assets/AssetManager.hpp"
#include "core/io/BlobLoader.hpp"
#include "tools/PipelineTypes.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

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
    if (!path || !path[0]) return ~u32(0);

    // One slot per file: `a/./b.caf`, `a//b.caf` and `a\\b.caf` are the same asset. A relative path that climbs
    // out of the base folder is refused (it would name something that is not in the project).
    std::string key;
    {
        const std::string given = path;
        const bool absolute = std::filesystem::path(given).is_absolute();
        if (absolute) {
            key = std::filesystem::path(given).lexically_normal().generic_string();
        } else {
            std::string relative;
            if (!normalizeRelative(given, relative)) return ~u32(0);
            key = m_basePath + relative;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_pathIndex.find(key);
    if (it != m_pathIndex.end()) {
        const u32 id = it->second;
        // A file loaded as one kind of asset is not another kind: the typed view would be empty.
        if (m_assets[id]->cafType != type) return ~u32(0);
        m_assets[id]->lastAccessFrame = m_frame;
        m_assets[id]->refCount.fetch_add(1, std::memory_order_relaxed);
        ++m_cacheHits;
        ++m_totalLoads;
        return id;
    }

    u32  id    = static_cast<u32>(m_assets.size());
    auto entry = std::make_unique<AssetEntry>();
    entry->path       = key;
    entry->cafType    = type;
    entry->generation = 1;
    entry->refCount.store(1, std::memory_order_relaxed);
    entry->status.store(LoadStatus::Pending, std::memory_order_release);
    m_assets.push_back(std::move(entry));
    m_pathIndex[key] = id;
    ++m_totalLoads;
    return id;
}

void AssetManager::unpin(u32 id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return;
    m_assets[id]->refCount.fetch_sub(1, std::memory_order_acq_rel);
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
    // Called with the lock held: queue it, and `flushNotifications` calls the listeners once it is released.
    if (m_invalidationListeners.empty()) return;
    m_pendingNotifications.push_back(InvalidatedAsset{id, generation, type});
}

void AssetManager::flushNotifications() {
    std::vector<InvalidatedAsset> events;
    std::vector<InvalidationListener> listeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingNotifications.empty()) return;
        events.swap(m_pendingNotifications);
        listeners = m_invalidationListeners;
    }
    for (const InvalidatedAsset& info : events) {
        for (const auto& listener : listeners) {
            if (listener.callback) listener.callback(info, listener.userData);
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

bool AssetManager::claimLoad(u32 id, LoadStatus& seen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) {
        seen = LoadStatus::Invalid;
        return false;
    }
    AssetEntry& e = *m_assets[id];
    seen = e.status.load(std::memory_order_acquire);
    if (seen == LoadStatus::Loading || seen == LoadStatus::Ready) return false;
    e.status.store(LoadStatus::Loading, std::memory_order_release);
    return true;
}

void AssetManager::scheduleLoad(u32 id) {
    LoadStatus seen;
    if (!claimLoad(id, seen)) return;

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

void AssetManager::loadBlocking(u32 id) {
    LoadStatus seen;
    if (claimLoad(id, seen)) {
        loadInternal(id);
        return;
    }
    if (seen != LoadStatus::Loading) return;
    // Another thread is loading it: wait for that load instead of reading the file a second time.
    std::unique_lock<std::mutex> lock(m_mutex);
    m_loadDone.wait(lock, [&] {
        return id >= static_cast<u32>(m_assets.size()) || !m_assets[id] ||
               m_assets[id]->status.load(std::memory_order_acquire) != LoadStatus::Loading;
    });
}

void AssetManager::loadInternal(u32 id) {
    // The caller has claimed the slot (it is Loading and nobody else loads it).
    AssetEntry* eptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= static_cast<u32>(m_assets.size()) || !m_assets[id]) return;
        eptr = m_assets[id].get();
    }
    auto finish = [&](LoadStatus status) {
        eptr->status.store(status, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_mutex);  // the waiter checks under this lock: no lost wake-up
        }
        m_loadDone.notify_all();
    };

    AssetEntry& e = *eptr;

    FILE* f = std::fopen(e.path.c_str(), "rb");
    if (!f) {
        finish(LoadStatus::Failed);
        return;
    }
    std::fseek(f, 0, SEEK_END);
    const long measured = std::ftell(f);
    std::fclose(f);

    if (measured <= 0) {
        finish(LoadStatus::Failed);
        return;
    }
    const u64 fileSize = static_cast<u64>(measured);

    auto alloc = std::make_unique<LinearAllocator>(static_cast<usize>(fileSize + 64));
    IO::BlobLoader::LoadResult result = IO::BlobLoader::load(e.path.c_str(), alloc.get());

    if (!result.valid || result.header->type != e.cafType) {
        // Corrupt, or a different kind of asset than the one asked for.
        finish(LoadStatus::Failed);
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

    e.lastAccessFrame = m_frame;
    finish(LoadStatus::Ready);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        evictLruToFitBudget();
    }
    flushNotifications();
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
    {
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
    flushNotifications();
}

CacheStats AssetManager::cacheStats() const {
    CacheStats stats{};
    u32 shaders = 0, meshes = 0, prefabs = 0;
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
            case AssetType::Shader:  ++shaders;            break;
            case AssetType::Mesh:    ++meshes;             break;
            case AssetType::Prefab:  ++prefabs;            break;
            default: break;
        }
    }
    stats.cacheHitRate = (m_totalLoads > 0)
        ? static_cast<f32>(m_cacheHits) / static_cast<f32>(m_totalLoads)
        : 0.0f;
    stats.evictedCount = static_cast<u32>(m_evictedCount);
    stats.shaderCount = shaders;
    stats.meshCount = meshes;
    stats.prefabCount = prefabs;

    return stats;
}

void AssetManager::tick(u64 frameIndex) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frame = frameIndex;
        evictLruToFitBudget();
    }
    flushNotifications();
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

    std::string relative;
    std::string key;
    if (std::filesystem::path(path).is_absolute()) {
        key = std::filesystem::path(path).lexically_normal().generic_string();
    } else if (normalizeRelative(path, relative)) {
        key = m_basePath + relative;
    } else {
        return ~0u;
    }
    u32 id = ~0u;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pathIndex.find(key);
        if (it == m_pathIndex.end()) return ~0u;
        id = it->second;
        // Someone is loading it right now: that load reads the newest file anyway.
        if (m_assets[id]->status.load(std::memory_order_acquire) == LoadStatus::Loading) return id;
        clearEntryData(*m_assets[id]);
        m_assets[id]->status.store(LoadStatus::Pending, std::memory_order_release);
    }

    loadBlocking(id);
    return id;
}

// ============================================================================
// Project references
// ============================================================================

namespace {

namespace fs = std::filesystem;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string toGeneric(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

/// `path` inside `root` as a `/`-separated relative path; nullopt when it is not inside.
std::optional<std::string> insideRoot(const fs::path& root, const fs::path& path) {
    if (root.empty()) return std::nullopt;
    std::error_code ec;
    const fs::path relative = path.lexically_relative(root);
    if (relative.empty() || relative == "." ) return std::nullopt;
    const std::string text = relative.generic_string();
    if (text == ".." || text.rfind("../", 0) == 0) return std::nullopt;
    (void)ec;
    return text;
}

fs::path canonicalOrNormal(const fs::path& path) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

/// The child of `dir` named `name` ignoring case ("textures" for "Textures"); empty when there is none.
fs::path childIgnoringCase(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    if (fs::is_directory(dir / name, ec)) return dir / name;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        if (lowered(entry.path().filename().string()) == lowered(name)) return entry.path();
    }
    return {};
}

bool sameContent(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    char bufA[8192], bufB[8192];
    while (fa && fb) {
        fa.read(bufA, sizeof(bufA));
        fb.read(bufB, sizeof(bufB));
        if (fa.gcount() != fb.gcount() || std::memcmp(bufA, bufB, static_cast<size_t>(fa.gcount())) != 0) return false;
    }
    return true;
}

const char* folderForKind(const std::string& kind) {
    if (kind == "image") return "Textures";
    if (kind == "mesh") return "Models";
    if (kind == "audio") return "Audio";
    if (kind == "material" || kind == "generator") return "Materials";
    if (kind == "script") return "Scripts";
    if (kind == "animation") return "Animations";
    return "Files";
}

}  // namespace

std::string AssetManager::classify(const std::string& path, bool isDirectory) {
    if (isDirectory) return "folder";
    const fs::path file(path);
    const std::string ext = lowered(file.extension().string());
    // `Rock.generator.material`: the editable recipe of a generator, not a material.
    const std::string stem = lowered(file.stem().string());
    const std::string marker = ".generator";
    if (stem.size() > marker.size() && stem.compare(stem.size() - marker.size(), marker.size(), marker) == 0) {
        return "generator";
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".gif" ||
        ext == ".hdr" || ext == ".exr") {
        return "image";
    }
    if (ext == ".obj" || ext == ".glb" || ext == ".gltf") return "mesh";
    if (ext == ".mat" || ext == ".material") return "material";
    if (ext == ".lua" || ext == ".dyd") return "script";
    if (ext == ".caf" || ext == ".cap") return "asset";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return "audio";
    if (ext == ".anim") return "animation";
    return "file";
}

bool AssetManager::normalizeRelative(const std::string& path, std::string& out) {
    std::vector<std::string> parts;
    std::string current;
    const std::string text = toGeneric(path) + "/";
    for (const char c : text) {
        if (c != '/') {
            current += c;
            continue;
        }
        if (current.empty() || current == ".") {
            // skip
        } else if (current == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
        } else {
            parts.push_back(current);
        }
        current.clear();
    }
    out.clear();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return true;
}

void AssetManager::setProjectRoot(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot = path.empty() ? std::string() : canonicalOrNormal(path).generic_string();
}

void AssetManager::setAssetRoot(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assetRoot = path.empty() ? std::string() : canonicalOrNormal(path).generic_string();
}

void AssetManager::setEngineAssetRoot(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_engineAssetRoot = path.empty() ? std::string() : canonicalOrNormal(path).generic_string();
}

AssetReference AssetManager::resolveReference(const std::string& path) const {
    AssetReference ref;
    std::string root, assets, engine;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        root = m_projectRoot;
        assets = m_assetRoot;
        engine = m_engineAssetRoot;
    }
    // Without a project folder the assets folder is the project.
    if (root.empty()) root = assets;
    if (assets.empty()) assets = root;

    if (path.empty()) return ref;  // Empty
    const std::string given = toGeneric(path);
    std::error_code ec;

    auto accept = [&](const fs::path& file, ReferenceOrigin origin, const std::string& projectPath) {
        ref.status = ReferenceStatus::Ok;
        ref.origin = origin;
        ref.projectPath = projectPath;
        ref.assetPath = projectPath;
        if (origin == ReferenceOrigin::Project) {
            if (const auto inAssets = insideRoot(canonicalOrNormal(assets), canonicalOrNormal(file))) ref.assetPath = *inAssets;
        }
        ref.absolutePath = file.generic_string();
        ref.kind = classify(file.generic_string());
        return ref;
    };
    auto stateOf = [&](const fs::path& file) {
        if (fs::is_regular_file(file, ec)) return 1;
        if (fs::is_directory(file, ec)) return 2;
        return 0;
    };

    if (fs::path(given).is_absolute()) {
        const fs::path absolute = canonicalOrNormal(given);
        ref.absolutePath = absolute.generic_string();
        const int state = stateOf(absolute);
        if (state == 0) {
            ref.status = ReferenceStatus::Missing;
            return ref;
        }
        if (state == 2) {
            ref.status = ReferenceStatus::NotAFile;
            return ref;
        }
        if (const auto relative = insideRoot(root, absolute)) return accept(absolute, ReferenceOrigin::Project, *relative);
        if (const auto relative = insideRoot(engine, absolute)) return accept(absolute, ReferenceOrigin::Engine, *relative);
        ref.status = ReferenceStatus::OutsideProject;
        ref.kind = classify(absolute.generic_string());
        return ref;
    }

    std::string relative;
    if (!normalizeRelative(given, relative) || relative.empty()) {
        ref.status = ReferenceStatus::Missing;
        return ref;
    }
    bool sawFolder = false;
    auto tryRoot = [&](const std::string& base, ReferenceOrigin origin, bool relativeToRoot) -> bool {
        if (base.empty()) return false;
        const fs::path file = fs::path(base) / relative;
        const int state = stateOf(file);
        if (state == 2) sawFolder = true;
        if (state != 1) return false;
        std::string projectPath = relative;
        if (relativeToRoot && origin == ReferenceOrigin::Project) {
            if (const auto inRoot = insideRoot(root, canonicalOrNormal(file))) projectPath = *inRoot;
        }
        accept(canonicalOrNormal(file), origin, projectPath);
        return true;
    };
    if (tryRoot(root, ReferenceOrigin::Project, false)) return ref;
    if (assets != root && tryRoot(assets, ReferenceOrigin::Project, true)) return ref;
    if (tryRoot(engine, ReferenceOrigin::Engine, false)) return ref;
    ref.status = sawFolder ? ReferenceStatus::NotAFile : ReferenceStatus::Missing;
    return ref;
}

AssetReference AssetManager::importReference(const std::string& path) {
    AssetReference ref = resolveReference(path);
    if (ref.status != ReferenceStatus::OutsideProject) return ref;

    std::string root, assets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        root = m_projectRoot.empty() ? m_assetRoot : m_projectRoot;
        assets = m_assetRoot.empty() ? root : m_assetRoot;
    }
    if (root.empty()) return ref;  // no project to import into

    std::error_code ec;
    const fs::path source(ref.absolutePath);
    fs::path destination = fs::path(assets);
    const fs::path existing = childIgnoringCase(destination, folderForKind(ref.kind));
    destination = existing.empty() ? destination / folderForKind(ref.kind) : existing;
    fs::create_directories(destination, ec);
    if (ec) return ref;

    const std::string stem = source.stem().string();
    const std::string extension = source.extension().string();
    fs::path target;
    bool reused = false;
    for (int n = 0; n < 1000; ++n) {
        const fs::path candidate = destination / (n == 0 ? stem + extension : stem + "_" + std::to_string(n) + extension);
        if (!fs::exists(candidate, ec)) {
            target = candidate;
            break;
        }
        if (sameContent(source, candidate)) {  // already imported: use that one
            target = candidate;
            reused = true;
            break;
        }
    }
    if (target.empty()) return ref;
    if (!reused) {
        fs::copy_file(source, target, fs::copy_options::none, ec);
        if (ec) return ref;
    }

    AssetReference imported;
    imported.status = ReferenceStatus::Ok;
    imported.origin = ReferenceOrigin::Project;
    imported.absolutePath = canonicalOrNormal(target).generic_string();
    imported.kind = ref.kind;
    imported.imported = !reused;
    if (const auto relative = insideRoot(canonicalOrNormal(root), canonicalOrNormal(target))) {
        imported.projectPath = *relative;
    } else {
        imported.projectPath = target.filename().generic_string();
    }
    imported.assetPath = imported.projectPath;
    if (const auto inAssets = insideRoot(canonicalOrNormal(assets), canonicalOrNormal(target))) imported.assetPath = *inAssets;
    return imported;
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
        loadBlocking(id);
    }
}
#endif

} // namespace Caffeine::Assets
