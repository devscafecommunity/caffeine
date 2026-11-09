#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <vector>

namespace Caffeine::IO {

class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    bool start(const std::filesystem::path& directory, bool recursive);
    void stop();

    // Returns files changed since last call. Thread-safe.
    std::vector<std::filesystem::path> poll();

    bool isRunning() const { return m_Running; }

private:
    struct Impl;
    Impl* m_Impl   = nullptr;
    bool  m_Running = false;
};

} // namespace Caffeine::IO
