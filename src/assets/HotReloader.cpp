#include "assets/HotReloader.hpp"
#include "assets/AssetManager.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace Caffeine::Assets {

HotReloader::~HotReloader() { Stop(); }

void HotReloader::Start(AssetManager* manager,
                         const std::filesystem::path& processedDir,
                         u32 debounceMs) {
    Stop();

    m_Manager      = manager;
    m_ProcessedDir = std::filesystem::absolute(processedDir);
    m_DebounceMs   = debounceMs;
    m_StopRequested.store(false, std::memory_order_release);
    m_Running.store(true, std::memory_order_release);

    m_Thread = std::thread(&HotReloader::PollThreadMain, this);
}

void HotReloader::Stop() {
    if (!m_Running.load(std::memory_order_acquire)) return;

    m_StopRequested.store(true, std::memory_order_release);
    if (m_Thread.joinable()) {
        m_Thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Files.clear();
        m_PendingReloads.clear();
    }

    m_Running.store(false, std::memory_order_release);
    m_Manager = nullptr;
}

void HotReloader::Update() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        batch.swap(m_PendingReloads);
    }

    for (const auto& cafPath : batch) {
        ReloadOne(cafPath);
    }
}

// ── Private ──────────────────────────────────────────────────────────────────

void HotReloader::ReloadOne(const std::string& cafPath) {
    if (!m_Manager) return;

    // AssetManager expects paths relative to its base path; the cafPath is
    // the full absolute path.  We pass it through verbosely — reloadAsset()
    // will match it inside m_pathIndex (the key stored there is the full
    // path built from basePath + relative part supplied at loadAsync/Sync
    // time).
    u32 assetId = m_Manager->reloadAsset(cafPath.c_str());
    if (assetId == ~0u) return;

    auto status = m_Manager->getStatus(assetId);
    if (status != LoadStatus::Loaded) return;

    auto* tex = m_Manager->getResolved<Texture>(assetId);

    HotReloadedAsset info;
    info.assetId = assetId;
    info.name    = std::filesystem::path(cafPath).stem().string();

    // Try to determine type from the loaded header.  We query the resolved
    // data as a proxy — if getResolved<Texture> returns non-null we know the
    // type.  This is a lightweight heuristic for the notification only.
    if (tex) {
        info.type = AssetType::Texture;
    } else if (m_Manager->getResolved<AudioClip>(assetId)) {
        info.type = AssetType::Audio;
    } else if (m_Manager->getResolved<ShaderBlob>(assetId)) {
        info.type = AssetType::Shader;
    }

    if (m_NotificationCallback) {
        m_NotificationCallback(info);
    }
}

void HotReloader::PollThreadMain() {
    using Clock = std::chrono::steady_clock;

    while (!m_StopRequested.load(std::memory_order_acquire)) {
        std::error_code ec;

        // ── 1. Scan the processed directory ────────────────────────
        std::vector<FileEntry> currentFiles;
        for (const auto& entry :
             std::filesystem::directory_iterator(m_ProcessedDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".caf") continue;

            auto ftime = entry.last_write_time(ec);
            if (ec) continue;

            FileEntry fe;
            fe.path          = entry.path().string();
            fe.lastWriteTime = static_cast<u64>(ftime.time_since_epoch().count());
            currentFiles.push_back(std::move(fe));
        }
        if (ec) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // ── 2. Compare with the previous snapshot ──────────────────
        auto now = Clock::now();
        i64 nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now.time_since_epoch())
                        .count();

        std::lock_guard<std::mutex> lock(m_Mutex);

        for (const auto& cur : currentFiles) {
            auto prevIt = std::find_if(m_Files.begin(), m_Files.end(),
                [&](const FileEntry& f) { return f.path == cur.path; });

            if (prevIt == m_Files.end()) {
                // Brand-new file — track it but don't reload yet.
                FileEntry fe = cur;
                fe.debounceStart = -1;
                m_Files.push_back(std::move(fe));
                continue;
            }

            if (prevIt->lastWriteTime == cur.lastWriteTime) {
                if (prevIt->debounceStart >= 0) {
                    i64 elapsedUs = (nowNs - prevIt->debounceStart) / 1000;
                    if (static_cast<u64>(elapsedUs) >= static_cast<u64>(m_DebounceMs) * 1000) {
                        prevIt->debounceStart = -1;
                        if (std::find(m_PendingReloads.begin(),
                                      m_PendingReloads.end(),
                                      cur.path) == m_PendingReloads.end()) {
                            m_PendingReloads.push_back(cur.path);
                        }
                    }
                }
                continue;
            }

            // Timestamp differs → (re)start debounce.
            if (prevIt->debounceStart < 0) {
                prevIt->debounceStart = nowNs;
                prevIt->lastWriteTime = cur.lastWriteTime;
                continue;
            }

            // Debounce already running — check if enough time elapsed.
            i64 elapsedUs = (nowNs - prevIt->debounceStart) / 1000;
            if (static_cast<u64>(elapsedUs) >= static_cast<u64>(m_DebounceMs) * 1000) {
                // Stable change — queue for reload.
                prevIt->lastWriteTime = cur.lastWriteTime;
                prevIt->debounceStart = -1;

                if (std::find(m_PendingReloads.begin(),
                              m_PendingReloads.end(),
                              cur.path) == m_PendingReloads.end()) {
                    m_PendingReloads.push_back(cur.path);
                }
            }
        }

        m_Files.erase(
            std::remove_if(m_Files.begin(), m_Files.end(),
                [&](const FileEntry& f) {
                    return std::none_of(currentFiles.begin(), currentFiles.end(),
                        [&](const FileEntry& c) { return c.path == f.path; });
                }),
            m_Files.end());

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace Caffeine::Assets
