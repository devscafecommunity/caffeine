#include "engine/AssetLoader.hpp"
#include <chrono>

namespace Caffeine {

AssetLoader::AssetLoader() {
    m_workerThread = std::thread(&AssetLoader::workerLoop, this);
}

AssetHandle AssetLoader::loadAssetAsync(u64 assetId, AssetCallback onReady) {
    static u64 handleCounter = 0;
    AssetHandle handle = ++handleCounter;
    
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        LoadJob job{assetId, handle, onReady};
        m_pendingLoads.push(job);
    }
    
    return handle;
}

void AssetLoader::cancelLoad(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
}

void AssetLoader::workerLoop() {
    while (m_running) {
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            if (!m_pendingLoads.empty()) {
                auto job = m_pendingLoads.front();
                m_pendingLoads.pop();
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                std::vector<u8> dummyData;
                
                {
                    std::lock_guard<std::mutex> completedLock(m_completedMutex);
                    m_completedLoads.push_back({job.handle, job.callback, dummyData});
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void AssetLoader::update() {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    for (auto& completed : m_completedLoads) {
        if (completed.callback) {
            completed.callback(completed.data);
        }
    }
    m_completedLoads.clear();
}

AssetLoader::~AssetLoader() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

}
