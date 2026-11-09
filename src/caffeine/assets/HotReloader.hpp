#pragma once

#include "core/Types.hpp"
#include "assets/AssetTypes.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Caffeine::Assets {

class AssetManager;

// ============================================================================
struct HotReloadedAsset {
    u32         assetId = ~0u;
    std::string name;
    AssetType   type = AssetType::Unknown;
};

// ============================================================================
// @brief  Monitors processado .caf directory for changes and triggers
//         in-place reloads in AssetManager via a background poll thread.
//
//  Flow:
//    1. Background thread scans processado/ for .caf file timestamp changes.
//    2. After a debounce delay the timestamp remains different, the file
//       path is queued for reload.
//    3. Update() (called from the main thread) pops the queue, calls
//       AssetManager::reloadAsset(), then fires the optional notification
//       callback (used by the editor for toast messages).
//
//  Threading:
//    - Poll thread writes to m_PendingReloads (under mutex).
//    - Update() reads from m_PendingReloads (under mutex), does the actual
//      reload, then fires the notification on the caller's thread.
// ============================================================================
class HotReloader {
public:
    using NotificationCallback = std::function<void(const HotReloadedAsset&)>;

    HotReloader() = default;
    ~HotReloader();

    HotReloader(const HotReloader&) = delete;
    HotReloader& operator=(const HotReloader&) = delete;

    // Start the background poll thread.
    //  manager      — AssetManager whose loaded assets should be reloaded.
    //  processedDir — path to the directory containing .caf files (normally
    //                 projectRoot/assets/processado from AssetPipeline).
    //  debounceMs   — minimum time (ms) to wait after the last file write
    //                 before triggering a reload (default 100).
    void Start(AssetManager* manager,
               const std::filesystem::path& processedDir,
               u32 debounceMs = 100);

    void Stop();

    // Must be called from the main thread each frame.
    void Update();

    void SetNotificationCallback(NotificationCallback cb) {
        m_NotificationCallback = std::move(cb);
    }

    bool IsRunning() const { return m_Running; }

private:
    struct FileEntry {
        std::string path;
        u64 lastWriteTime  = 0;
        i64 debounceStart  = -1;  // steady_clock time when change was first seen
    };

    void PollThreadMain();
    void ReloadOne(const std::string& cafPath);

    AssetManager* m_Manager = nullptr;
    std::filesystem::path m_ProcessedDir;
    u32 m_DebounceMs = 100;

    std::vector<FileEntry> m_Files;
    std::mutex             m_Mutex;
    std::vector<std::string> m_PendingReloads;

    std::thread             m_Thread;
    std::atomic<bool>       m_StopRequested{false};
    std::atomic<bool>       m_Running{false};

    NotificationCallback m_NotificationCallback;
};

} // namespace Caffeine::Assets
