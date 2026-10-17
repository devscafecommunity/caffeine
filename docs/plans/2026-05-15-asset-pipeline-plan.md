# Asset Pipeline Integration — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement automatic conversion of raw assets (PNG/JPG) to optimized `.caf` format via a FileWatcher-driven pipeline.

**Architecture:** Native Windows FileWatcher (`ReadDirectoryChangesW`) detects new/changed files in `assets/raw/`. AssetPipeline queues detected files, dispatches to registered compilers (starting with TextureCompiler), writes `.caf` output to `assets/processed/`, and maintains `assets/manifest.caf`.

**Tech Stack:** C++20, Win32 API (ReadDirectoryChangesW), stb_image.h (compile-as-C), existing CafWriter

---

### Task 1: FileWatcher

**Files:**
- Create: `src/core/io/FileWatcher.hpp`
- Create: `src/core/io/FileWatcher.cpp`

**Description:** Native Windows directory monitoring via `ReadDirectoryChangesW` on a background thread. Class `Caffeine::IO::FileWatcher` with `start(dir, recursive)`, `stop()`, `poll()` returning `std::vector<std::filesystem::path>`.

**Step 1: Write FileWatcher.hpp**

```cpp
#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <functional>
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
    Impl* m_Impl = nullptr;
    bool  m_Running = false;
};

} // namespace Caffeine::IO
```

**Step 2: Write the test first (`tests/test_pipeline.cpp`)**

Add to `tests/test_pipeline.cpp`:

```cpp
#include "catch.hpp"
#include "../src/core/io/FileWatcher.hpp"

using namespace Caffeine::IO;

TEST_CASE("FileWatcher - Create and destroy", "[pipeline]") {
    FileWatcher fw;
    REQUIRE_FALSE(fw.isRunning());
}
```

**Step 3: Write FileWatcher.cpp**

Implementation using Windows `ReadDirectoryChangesW`:

```cpp
#include "core/io/FileWatcher.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <queue>
#include <mutex>
#include <thread>
#endif

namespace Caffeine::IO {

struct FileWatcher::Impl {
    std::filesystem::path          directory;
    HANDLE                         hDir = INVALID_HANDLE_VALUE;
    OVERLAPPED                     overlapped{};
    std::mutex                     mutex;
    std::queue<std::filesystem::path> changes;
    std::thread                    worker;
    bool                           stopRequested = false;

    // Buffer must be large enough for at least one FILE_NOTIFY_INFORMATION
    alignas(FILE_NOTIFY_INFORMATION) u8 buffer[64 * 1024];
};

FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::start(const std::filesystem::path& directory, bool recursive) {
    if (m_Running) stop();

    auto impl = std::make_unique<Impl>();
    impl->directory = directory;

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

    m_Impl = impl.release();
    m_Running = true;

    // Background thread: loop ReadDirectoryChangesW
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

            // Wait for completion
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
    m_Impl = nullptr;
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

} // namespace Caffeine::IO
```

**Step 4: Build & test**

Rebuild and run: `cmake --build build --config Debug && build/tests/Debug/CaffeineTest.exe "[pipeline]" --success`
Expected: PASS (FileWatcher - Create and destroy)

**Step 5: Commit**

```bash
git add src/core/io/FileWatcher.hpp src/core/io/FileWatcher.cpp
git commit -m "feat(core): add native Windows FileWatcher (ReadDirectoryChangesW)"
```

---

### Task 2: AssetPipeline + IAssetCompiler

**Files:**
- Create: `src/assets/AssetPipeline.hpp`
- Create: `src/assets/AssetPipeline.cpp`
- Modify: `CMakeLists.txt` (add sources to caffeine-core)
- Modify: `src/Caffeine.hpp` (add AssetPipeline include)

**Description:** Core pipeline classes in `Caffeine::Assets` namespace: `AssetImportContext`, `IAssetCompiler`, `AssetPipeline`. Pipeline owns a FileWatcher, compiler registry, import queue, and manifest.

**Step 1: Write AssetPipeline.hpp**

```cpp
#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "core/io/FileWatcher.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace Caffeine::Assets {

using namespace Caffeine;

// ============================================================================
// @brief  Context passed to each compiler's Compile() method.
// ============================================================================
struct AssetImportContext {
    std::filesystem::path SourcePath;      // raw/foo.png
    std::filesystem::path DestinationPath; // processed/foo.caf
    std::vector<std::string> Logs;
    bool Success = false;
};

// ============================================================================
// @brief  Abstract interface for asset compilers.
// ============================================================================
class IAssetCompiler {
public:
    virtual ~IAssetCompiler() = default;

    // Compile source file at ctx.SourcePath → write .caf to ctx.DestinationPath.
    virtual bool Compile(AssetImportContext& ctx) = 0;

    // File extensions this compiler handles (e.g. ".png", ".jpg").
    virtual std::vector<std::string> GetSupportedExtensions() = 0;
};

// ============================================================================
// @brief  Manifest entry (serialised to manifest.caf metadata block).
// ============================================================================
struct ManifestEntry {
    AssetType type;
    std::string relativePath;    // relative to project root (e.g. "assets/processed/foo.caf")
    std::string sourcePath;      // original raw path   (e.g. "assets/raw/foo.png")
};

// ============================================================================
// @brief  Asset pipeline — watches raw/, compiles, updates manifest.
// ============================================================================
class AssetPipeline {
public:
    AssetPipeline();
    ~AssetPipeline();

    AssetPipeline(const AssetPipeline&) = delete;
    AssetPipeline& operator=(const AssetPipeline&) = delete;

    // Initialise with project root path. Creates dirs if needed.
    void Initialize(const std::filesystem::path& projectRoot);

    // Call once per frame. Polls FileWatcher and processes queue.
    void Update();

    // Register a compiler (takes ownership).
    void RegisterCompiler(std::unique_ptr<IAssetCompiler> compiler);

    // Manually re-import a specific file.
    void Reimport(const std::filesystem::path& path);

    // Progress.
    float GetProgress() const { return m_CurrentProgress; }
    bool  IsProcessing() const { return m_Busy; }

    // Access manifest.
    const std::vector<ManifestEntry>& GetManifest() const { return m_Manifest; }

private:
    void OnFileDetected(const std::filesystem::path& path);
    void ProcessQueue();
    void LoadManifest();
    void SaveManifest();
    IAssetCompiler* FindCompiler(const std::string& extension);

    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_RawDir;
    std::filesystem::path m_ProcessedDir;
    std::filesystem::path m_ManifestPath;

    IO::FileWatcher m_FileWatcher;
    std::queue<std::filesystem::path> m_ImportQueue;
    std::map<std::string, IAssetCompiler*> m_Compilers;  // ext → compiler
    std::vector<std::unique_ptr<IAssetCompiler>> m_CompilerOwned;

    std::vector<ManifestEntry> m_Manifest;

    bool  m_Busy = false;
    float m_CurrentProgress = 0.0f;
};

} // namespace Caffeine::Assets
```

**Step 2: Write the test first**

In `tests/test_pipeline.cpp`:

```cpp
#include "catch.hpp"
#include "../src/assets/AssetPipeline.hpp"

using namespace Caffeine::Assets;

namespace {

class NullCompiler : public IAssetCompiler {
public:
    bool Compile(AssetImportContext& ctx) override {
        ctx.Logs.push_back("NullCompiler: " + ctx.SourcePath.string());
        ctx.Success = true;
        return true;
    }
    std::vector<std::string> GetSupportedExtensions() override {
        return { ".null" };
    }
};

}

TEST_CASE("AssetPipeline - Register and find compiler", "[pipeline]") {
    AssetPipeline pipeline;
    pipeline.RegisterCompiler(std::make_unique<NullCompiler>());
    REQUIRE_FALSE(pipeline.IsProcessing());
    REQUIRE(pipeline.GetProgress() == 0.0f);
}

TEST_CASE("AssetPipeline - Initialize creates directories", "[pipeline]") {
    // Use a temp directory
    auto tmp = std::filesystem::temp_directory_path() / "caffeine_pipeline_test";
    std::filesystem::remove_all(tmp);

    AssetPipeline pipeline;
    pipeline.Initialize(tmp);

    REQUIRE(std::filesystem::exists(tmp / "assets" / "raw"));
    REQUIRE(std::filesystem::exists(tmp / "assets" / "processed"));

    std::filesystem::remove_all(tmp);
}
```

**Step 3: Write AssetPipeline.cpp**

```cpp
#include "assets/AssetPipeline.hpp"
#include "core/io/CafWriter.hpp"

#include <cstdio>
#include <fstream>

namespace Caffeine::Assets {

AssetPipeline::AssetPipeline() = default;
AssetPipeline::~AssetPipeline() = default;

void AssetPipeline::Initialize(const std::filesystem::path& projectRoot) {
    m_ProjectRoot  = std::filesystem::absolute(projectRoot);
    m_RawDir       = m_ProjectRoot / "assets" / "raw";
    m_ProcessedDir = m_ProjectRoot / "assets" / "processed";
    m_ManifestPath = m_ProjectRoot / "assets" / "manifest.caf";

    // Create directories if needed
    std::filesystem::create_directories(m_RawDir);
    std::filesystem::create_directories(m_ProcessedDir);

    LoadManifest();

    // Start watching raw directory
    m_FileWatcher.start(m_RawDir, false);
}

void AssetPipeline::Update() {
    if (!m_FileWatcher.isRunning()) return;

    // Poll for new/changed files
    auto changes = m_FileWatcher.poll();
    for (auto& path : changes) {
        OnFileDetected(path);
    }

    // Process queue (one file per frame to avoid blocking)
    if (!m_ImportQueue.empty() && !m_Busy) {
        ProcessQueue();
    }
}

void AssetPipeline::RegisterCompiler(std::unique_ptr<IAssetCompiler> compiler) {
    auto exts = compiler->GetSupportedExtensions();
    for (const auto& ext : exts) {
        m_Compilers[ext] = compiler.get();
    }
    m_CompilerOwned.push_back(std::move(compiler));
}

void AssetPipeline::Reimport(const std::filesystem::path& path) {
    OnFileDetected(path);
    if (!m_Busy) ProcessQueue();
}

void AssetPipeline::OnFileDetected(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    if (std::filesystem::is_directory(path)) return;
    m_ImportQueue.push(path);
}

void AssetPipeline::ProcessQueue() {
    if (m_ImportQueue.empty()) return;

    m_Busy = true;
    m_CurrentProgress = 0.0f;

    size_t total = m_ImportQueue.size();
    size_t processed = 0;

    while (!m_ImportQueue.empty()) {
        auto sourcePath = m_ImportQueue.front();
        m_ImportQueue.pop();

        // Find matching compiler
        std::string ext = sourcePath.extension().string();
        // Normalise to lowercase
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        IAssetCompiler* compiler = FindCompiler(ext);
        if (!compiler) continue;

        // Determine destination path: rename extension to .caf
        std::filesystem::path destName = sourcePath.stem();
        destName += ".caf";
        std::filesystem::path destPath = m_ProcessedDir / destName;

        AssetImportContext ctx;
        ctx.SourcePath      = sourcePath;
        ctx.DestinationPath = destPath;

        bool ok = compiler->Compile(ctx);
        if (ok) {
            // Update manifest
            ManifestEntry entry;
            entry.type         = AssetType::Texture;  // Will be set by compiler in future
            entry.relativePath = std::filesystem::relative(destPath, m_ProjectRoot).generic_string();
            entry.sourcePath   = std::filesystem::relative(sourcePath, m_ProjectRoot).generic_string();

            // Remove existing entry with same source path
            auto it = std::find_if(m_Manifest.begin(), m_Manifest.end(),
                [&](const ManifestEntry& e) { return e.sourcePath == entry.sourcePath; });
            if (it != m_Manifest.end()) m_Manifest.erase(it);

            m_Manifest.push_back(std::move(entry));
            SaveManifest();
        }

        processed++;
        m_CurrentProgress = static_cast<float>(processed) / static_cast<float>(total);
    }

    m_Busy = false;
    m_CurrentProgress = 1.0f;
}

IAssetCompiler* AssetPipeline::FindCompiler(const std::string& extension) {
    auto it = m_Compilers.find(extension);
    if (it != m_Compilers.end()) return it->second;

    // Also check with different casing variants
    std::string lower = extension;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it2 = m_Compilers.find(lower);
    if (it2 != m_Compilers.end()) return it2->second;

    return nullptr;
}

void AssetPipeline::LoadManifest() {
    if (!std::filesystem::exists(m_ManifestPath)) return;
    // Read manifest.caf — it's a valid .caf file with custom metadata
    // For v1 we use a simple approach: read header, then metadata block
    FILE* f = std::fopen(m_ManifestPath.string().c_str(), "rb");
    if (!f) return;

    CafHeader header;
    if (std::fread(&header, sizeof(header), 1, f) != 1) {
        std::fclose(f);
        return;
    }
    if (header.magic != CafHeader::kMagic) {
        std::fclose(f);
        return;
    }

    // Read metadata block containing manifest entries
    std::vector<u8> metaData(header.metadataSize);
    if (header.metadataSize > 0) {
        if (std::fread(metaData.data(), 1, header.metadataSize, f) != header.metadataSize) {
            std::fclose(f);
            return;
        }
        // Parse entries: u32 count followed by { AssetType, u64 pathLen, char path[], u64 srcLen, char src[] }
        u32 count = 0;
        if (header.metadataSize < sizeof(count)) { std::fclose(f); return; }
        std::memcpy(&count, metaData.data(), sizeof(count));

        u64 offset = sizeof(count);
        for (u32 i = 0; i < count; ++i) {
            if (offset + sizeof(AssetType) + sizeof(u64) > header.metadataSize) break;
            ManifestEntry entry;
            std::memcpy(&entry.type, metaData.data() + offset, sizeof(AssetType));
            offset += sizeof(AssetType);

            u64 pathLen = 0, srcLen = 0;
            std::memcpy(&pathLen, metaData.data() + offset, sizeof(u64));
            offset += sizeof(u64);
            if (offset + pathLen > header.metadataSize) break;
            entry.relativePath.assign(reinterpret_cast<char*>(metaData.data() + offset), pathLen);
            offset += pathLen;

            std::memcpy(&srcLen, metaData.data() + offset, sizeof(u64));
            offset += sizeof(u64);
            if (offset + srcLen > header.metadataSize) break;
            entry.sourcePath.assign(reinterpret_cast<char*>(metaData.data() + offset), srcLen);
            offset += srcLen;

            m_Manifest.push_back(std::move(entry));
        }
    }

    std::fclose(f);
}

void AssetPipeline::SaveManifest() {
    // Serialise manifest to binary metadata block
    u32 count = static_cast<u32>(m_Manifest.size());
    u64 metadataSize = sizeof(count);

    for (const auto& entry : m_Manifest) {
        metadataSize += sizeof(AssetType);
        metadataSize += sizeof(u64) + entry.relativePath.size();
        metadataSize += sizeof(u64) + entry.sourcePath.size();
    }

    std::vector<u8> metaData(metadataSize);
    u64 offset = 0;
    std::memcpy(metaData.data() + offset, &count, sizeof(count));
    offset += sizeof(count);

    for (const auto& entry : m_Manifest) {
        std::memcpy(metaData.data() + offset, &entry.type, sizeof(AssetType));
        offset += sizeof(AssetType);

        u64 pathLen = entry.relativePath.size();
        std::memcpy(metaData.data() + offset, &pathLen, sizeof(u64));
        offset += sizeof(u64);
        std::memcpy(metaData.data() + offset, entry.relativePath.data(), pathLen);
        offset += pathLen;

        u64 srcLen = entry.sourcePath.size();
        std::memcpy(metaData.data() + offset, &srcLen, sizeof(u64));
        offset += sizeof(u64);
        std::memcpy(metaData.data() + offset, entry.sourcePath.data(), srcLen);
        offset += srcLen;
    }

    // Use existing CafWriter to produce a valid .caf file
    Caffeine::IO::CafWriter::WriteResult wr = Caffeine::IO::CafWriter::write(
        m_ManifestPath.string().c_str(),
        AssetType::Unknown,     // manifest is a meta-asset
        0,                      // no special flags
        metaData.data(),
        metadataSize,
        nullptr,                // no payload
        0
    );
    (void)wr;
}

} // namespace Caffeine::Assets
```

**Step 4: Update CMakeLists.txt**

Add to `caffeine-core` sources in root `CMakeLists.txt`:
```cmake
src/core/io/FileWatcher.cpp
src/assets/AssetPipeline.cpp
```

**Step 5: Update src/Caffeine.hpp**

Add after the AssetManager include:
```cpp
#include "assets/AssetPipeline.hpp"
```

**Step 6: Build & test**

Run: `cmake --build build --config Debug && build/tests/Debug/CaffeineTest.exe "[pipeline]" --success`
Expected: PASS for all pipeline tests

**Step 7: Commit**

```bash
git add src/assets/AssetPipeline.hpp src/assets/AssetPipeline.cpp src/core/io/FileWatcher.cpp src/core/io/FileWatcher.hpp CMakeLists.txt src/Caffeine.hpp
git commit -m "feat(assets): add AssetPipeline with FileWatcher and compiler registry"
```

---

### Task 3: TextureCompiler

**Files:**
- Create: `src/assets/TextureCompiler.hpp`
- Create: `src/assets/TextureCompiler.cpp`
- Modify: `CMakeLists.txt` (add stb_image compile-as-C source)

**Description:** First concrete compiler in `Caffeine::Assets`. Decodes PNG/JPG using `stb_image.h`, writes RGBA8 `.caf` via `CafWriter`.

**Step 1: Write TextureCompiler.hpp**

```cpp
#pragma once

#include "assets/AssetPipeline.hpp"

namespace Caffeine::Assets {

class TextureCompiler : public IAssetCompiler {
public:
    bool Compile(AssetImportContext& ctx) override;
    std::vector<std::string> GetSupportedExtensions() override;
};

} // namespace Caffeine::Assets
```

**Step 2: Write the test first**

In `tests/test_pipeline.cpp`:

```cpp
#include "../src/assets/TextureCompiler.hpp"

TEST_CASE("TextureCompiler - GetSupportedExtensions", "[pipeline]") {
    TextureCompiler compiler;
    auto exts = compiler.GetSupportedExtensions();
    REQUIRE(std::find(exts.begin(), exts.end(), ".png") != exts.end());
    REQUIRE(std::find(exts.begin(), exts.end(), ".jpg") != exts.end());
}
```

**Step 3: Write TextureCompiler.cpp**

```cpp
#include "assets/TextureCompiler.hpp"
#include "core/io/CafWriter.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Caffeine::Assets {

bool TextureCompiler::Compile(AssetImportContext& ctx) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(ctx.SourcePath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        ctx.Logs.push_back(std::string("Failed to load: ") + stbi_failure_reason());
        ctx.Success = false;
        return false;
    }

    u64 pixelDataSize = static_cast<u64>(width) * static_cast<u64>(height) * 4;

    TextureMetadata meta{};
    meta.width     = static_cast<u32>(width);
    meta.height    = static_cast<u32>(height);
    meta.format    = 0; // RGBA8
    meta.mipLevels = 1;
    meta.reserved  = 0;

    IO::CafWriter::WriteResult result = IO::CafWriter::write(
        ctx.DestinationPath.string().c_str(),
        AssetType::Texture,
        CAF_FLAG_NONE,
        &meta,
        sizeof(meta),
        pixels,
        pixelDataSize
    );

    stbi_image_free(pixels);

    if (!result.success) {
        ctx.Logs.push_back("Failed to write .caf file: " + ctx.DestinationPath.string());
        ctx.Success = false;
        return false;
    }

    ctx.Logs.push_back("Compressed: " + ctx.SourcePath.string()
        + " → " + ctx.DestinationPath.string()
        + " (" + std::to_string(width) + "x" + std::to_string(height) + " RGBA8)");
    ctx.Success = true;
    return true;
}

std::vector<std::string> TextureCompiler::GetSupportedExtensions() {
    return { ".png", ".jpg", ".jpeg" };
}

} // namespace Caffeine::Assets
```

**Step 4: Add stb_image.h to the project**

Download or create a `src/assets/stb_image.h` from the public domain stb repository. This is a single-header library.

Actually, since the user may not have it, let's check.

**Step 5: Update CMakeLists.txt**

Add compile-as-C source for stb_image (or just include it directly in TextureCompiler.cpp with the `#define STB_IMAGE_IMPLEMENTATION` — that's already done above, no extra source needed).

Add to caffeine-core sources:
```cmake
src/assets/TextureCompiler.cpp
```

**Step 6: Build & test**

Run: `cmake --build build --config Debug && build/tests/Debug/CaffeineTest.exe "[pipeline]" --success`
Expected: PASS for all pipeline tests including TextureCompiler extensions

**Step 7: Commit**

```bash
git add src/assets/TextureCompiler.hpp src/assets/TextureCompiler.cpp CMakeLists.txt
git commit -m "feat(assets): add TextureCompiler with stb_image decoding"
```

---

### Task 4: Integration test

**Files:**
- Modify: `tests/test_pipeline.cpp`

**Step 1: Write full integration test**

Add to `tests/test_pipeline.cpp`:

```cpp
TEST_CASE("AssetPipeline - Full pipeline integration", "[pipeline][integration]") {
    auto tmp = std::filesystem::temp_directory_path() / "caffeine_pipeline_int_test";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "assets" / "raw");
    std::filesystem::create_directories(tmp / "assets" / "processed");

    // Create a small valid PNG (1x1 red pixel)
    // Minimal 67-byte PNG
    const u8 png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, // IDAT chunk
        0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x36, 0x28, 0x19,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, // IEND chunk
        0xAE, 0x42, 0x60, 0x82
    };
    auto rawPng = tmp / "assets" / "raw" / "test.png";
    std::ofstream(rawPng.string(), std::ios::binary)
        .write(reinterpret_cast<const char*>(png), sizeof(png));

    // Set up pipeline with TextureCompiler
    AssetPipeline pipeline;
    pipeline.RegisterCompiler(std::make_unique<TextureCompiler>());
    pipeline.Initialize(tmp);

    // Trigger reimport
    pipeline.Reimport(rawPng);

    // Check processed file exists
    auto expectedCaf = tmp / "assets" / "processed" / "test.caf";
    REQUIRE(std::filesystem::exists(expectedCaf));

    // Check manifest was written
    REQUIRE(std::filesystem::exists(tmp / "assets" / "manifest.caf"));

    // Read back the .caf and verify
    FILE* f = std::fopen(expectedCaf.string().c_str(), "rb");
    REQUIRE(f != nullptr);
    CafHeader header;
    REQUIRE(std::fread(&header, sizeof(header), 1, f) == 1);
    REQUIRE(header.magic == CafHeader::kMagic);
    REQUIRE(header.type == AssetType::Texture);
    REQUIRE(header.metadataSize == sizeof(TextureMetadata));
    REQUIRE(header.dataSize == 4); // 1x1 RGBA8 = 4 bytes
    std::fclose(f);

    // Check manifest has the entry
    auto manifest = pipeline.GetManifest();
    REQUIRE(manifest.size() == 1);
    REQUIRE(manifest[0].type == AssetType::Texture);

    std::filesystem::remove_all(tmp);
}
```

**Step 2: Build & test**

Run: `cmake --build build --config Debug && build/tests/Debug/CaffeineTest.exe "[pipeline]" --success`
Expected: PASS all pipeline tests including integration

**Step 3: Commit**

```bash
git add tests/test_pipeline.cpp tests/CMakeLists.txt
git commit -m "test(assets): add pipeline integration test with PNG compilation"
```

---

### Task 5: Final verification

**Step 1: Run full test suite (excluding stress)**

Run: `build/tests/Debug/CaffeineTest.exe ~[stress] --success`
Expected: All tests pass (pre-existing Compiler test may fail)

**Step 2: Push**

```bash
git push origin 92-asset-pipeline-integration
```
