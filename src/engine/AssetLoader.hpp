#pragma once

#include "core/Types.hpp"
#include <functional>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>

namespace Caffeine {

using AssetHandle = u64;
using AssetCallback = std::function<void(const std::vector<u8>&)>;

class AssetLoader {
public:
    AssetLoader();
    
    AssetHandle loadAssetAsync(u64 assetId, AssetCallback onReady);
    
    void cancelLoad(AssetHandle handle);
    
    void update();
    
    ~AssetLoader();

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

private:
    struct LoadJob {
        u64 id;
        AssetHandle handle;
        AssetCallback callback;
    };

    struct CompletedLoad {
        AssetHandle handle;
        AssetCallback callback;
        std::vector<u8> data;
    };
    
    std::queue<LoadJob> m_pendingLoads;
    std::vector<CompletedLoad> m_completedLoads;
    std::thread m_workerThread;
    std::mutex m_pendingMutex;
    std::mutex m_completedMutex;
    bool m_running = true;
    
    void workerLoop();
};

}
