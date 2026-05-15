#include "core/io/FileWatcher.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <queue>
#include <mutex>
#include <thread>
#endif

namespace Caffeine::IO {

// ============================================================================
// Windows implementation — ReadDirectoryChangesW
// ============================================================================
#ifdef _WIN32

struct FileWatcher::Impl {
    std::filesystem::path              directory;
    HANDLE                             hDir = INVALID_HANDLE_VALUE;
    OVERLAPPED                         overlapped{};
    std::mutex                         mutex;
    std::queue<std::filesystem::path>  changes;
    std::thread                        worker;
    bool                               stopRequested = false;

    alignas(FILE_NOTIFY_INFORMATION) u8 buffer[64 * 1024];
};

FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::start(const std::filesystem::path& directory, bool recursive) {
    if (m_Running) stop();

    auto impl            = std::make_unique<Impl>();
    impl->directory      = directory;

    impl->hDir = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (impl->hDir == INVALID_HANDLE_VALUE) return false;

    m_Impl   = impl.release();
    m_Running = true;

    auto* pImpl = m_Impl;
    pImpl->worker = std::thread([pImpl, recursive]() {
        while (!pImpl->stopRequested) {
            DWORD bytesReturned = 0;
            BOOL ok = ReadDirectoryChangesW(
                pImpl->hDir,
                pImpl->buffer,
                sizeof(pImpl->buffer),
                recursive ? TRUE : FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytesReturned,
                &pImpl->overlapped,
                nullptr
            );

            if (!ok) break;

            DWORD wait = WaitForSingleObject(pImpl->hDir, 100);
            if (wait == WAIT_OBJECT_0) {
                DWORD transferred = 0;
                GetOverlappedResult(pImpl->hDir, &pImpl->overlapped, &transferred, FALSE);

                if (transferred > 0) {
                    FILE_NOTIFY_INFORMATION* fni =
                        reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pImpl->buffer);
                    for (;;) {
                        std::wstring name(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
                        std::filesystem::path full = pImpl->directory / name;

                        {
                            std::lock_guard<std::mutex> lock(pImpl->mutex);
                            pImpl->changes.push(full);
                        }

                        if (fni->NextEntryOffset == 0) break;
                        fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                            reinterpret_cast<u8*>(fni) + fni->NextEntryOffset);
                    }
                }
            }
        }
    });

    return true;
}

void FileWatcher::stop() {
    if (!m_Running || !m_Impl) return;
    m_Impl->stopRequested = true;
    if (m_Impl->worker.joinable()) m_Impl->worker.join();
    if (m_Impl->hDir != INVALID_HANDLE_VALUE) CloseHandle(m_Impl->hDir);
    delete m_Impl;
    m_Impl   = nullptr;
    m_Running = false;
}

std::vector<std::filesystem::path> FileWatcher::poll() {
    std::vector<std::filesystem::path> result;
    if (!m_Impl) return result;
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    while (!m_Impl->changes.empty()) {
        result.push_back(m_Impl->changes.front());
        m_Impl->changes.pop();
    }
    return result;
}

// ============================================================================
// Stub implementation for non-Windows platforms
// ============================================================================
#else

struct FileWatcher::Impl {
    std::filesystem::path directory;
};

FileWatcher::~FileWatcher() = default;

bool FileWatcher::start(const std::filesystem::path& directory, bool) {
    m_Impl   = new Impl{ directory };
    m_Running = true;
    return true;
}

void FileWatcher::stop() {
    if (!m_Running || !m_Impl) return;
    delete m_Impl;
    m_Impl   = nullptr;
    m_Running = false;
}

std::vector<std::filesystem::path> FileWatcher::poll() {
    return {};
}

#endif

} // namespace Caffeine::IO
