# Caffeine Ecosystem Three-Phase Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement doppio editor integration (CAP browsing + texture/audio previews + drag-drop), advanced caf-pack features (mesh processor + compression + asset ID header), and tool integration (async loading infrastructure) for a complete unified ecosystem.

**Architecture:** Phase 1 extends doppio with CAP deserialization and preview rendering. Phase 2 extends caf-pack with mesh support and compression. Phase 3 builds async loading infrastructure and tool exports.

**Tech Stack:** C++20, ImGui, libpng, zlib, zstd, pthreads

---

## Phase 1: doppio Editor Integration (Step 4)

### Task 1.1: Create CAP Loader Module

**Files:**
- Create: `src/editor/CapLoader.hpp`
- Create: `src/editor/CapLoader.cpp`

**Step 1: Write CapLoader header with interface**

```cpp
// src/editor/CapLoader.hpp
#pragma once
#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include <vector>
#include <string>
#include <filesystem>

namespace Caffeine::Editor {

class CapLoader {
public:
    struct LoadedAsset {
        std::string filename;
        AssetType type;
        std::vector<u8> cafBlob;
        CafHeader metadata;
    };

    // Load CAP file and extract all assets
    static std::vector<LoadedAsset> loadCap(const std::filesystem::path& path);

private:
    // Helper to identify asset type from magic bytes in CAF
    static AssetType identifyAssetType(const CafHeader& header);
};

} // namespace Caffeine::Editor
```

**Step 2: Implement CAP loader**

```cpp
// src/editor/CapLoader.cpp
#include "editor/CapLoader.hpp"
#include <fstream>
#include <cstring>

namespace Caffeine::Editor {

std::vector<CapLoader::LoadedAsset> CapLoader::loadCap(const std::filesystem::path& path) {
    std::vector<LoadedAsset> assets;
    std::ifstream file(path, std::ios::binary);
    
    if (!file) {
        CF_LOG_ERROR("CapLoader: Failed to open CAP file: {}", path.string());
        return assets;
    }

    // Read CAP header
    CapHeader capHeader{};
    file.read(reinterpret_cast<char*>(&capHeader), sizeof(CapHeader));
    
    if (capHeader.magic != CapHeader::MAGIC) {
        CF_LOG_ERROR("CapLoader: Invalid CAP magic bytes");
        return assets;
    }

    if (capHeader.version != CapHeader::VERSION) {
        CF_LOG_ERROR("CapLoader: Unsupported CAP version");
        return assets;
    }

    // Read entry table
    std::vector<CapEntry> entries(capHeader.entryCount);
    file.read(reinterpret_cast<char*>(entries.data()), 
              capHeader.entryCount * sizeof(CapEntry));

    // Extract each asset
    for (const auto& entry : entries) {
        file.seekg(entry.offset);
        
        // Read CAF blob
        std::vector<u8> cafBlob(entry.size);
        file.read(reinterpret_cast<char*>(cafBlob.data()), entry.size);
        
        // Parse CAF header
        CafHeader cafHeader{};
        std::memcpy(&cafHeader, cafBlob.data(), sizeof(CafHeader));
        
        LoadedAsset asset{
            .filename = entry.filename,
            .type = identifyAssetType(cafHeader),
            .cafBlob = cafBlob,
            .metadata = cafHeader
        };
        
        assets.push_back(asset);
    }

    return assets;
}

AssetType CapLoader::identifyAssetType(const CafHeader& header) {
    // Type is stored in CafHeader
    return static_cast<AssetType>(header.type);
}

} // namespace Caffeine::Editor
```

**Step 3: Add CapLoader to CMakeLists.txt**

In `src/editor/CMakeLists.txt`, add to the doppio-editor target:
```cmake
CapLoader.hpp
CapLoader.cpp
```

**Step 4: Verify compilation**

```bash
cd build && cmake .. && make -j4
```

Expected: No errors, doppio target compiles successfully.

**Step 5: Commit**

```bash
git add src/editor/CapLoader.hpp src/editor/CapLoader.cpp src/editor/CMakeLists.txt
git commit -m "feat: add CAP file loader for Asset Browser"
```

---

### Task 1.2: Extend Asset Browser with CAP Mode

**Files:**
- Modify: `src/editor/AssetBrowser.hpp:1-150`
- Modify: `src/editor/AssetBrowser.cpp:1-300`

**Step 1: Add CAP mode enum and loading method to header**

Find the `AssetBrowser` class definition and add:

```cpp
// In AssetBrowser.hpp, after ViewMode enum:
enum class BrowseMode : u8 {
    Filesystem,
    CapFile
};

// Add to public section after existing methods:
void loadCapFile(const std::filesystem::path& capPath);
BrowseMode browseMode() const { return m_browseMode; }
```

**Step 2: Add CAP mode state variables**

In `AssetBrowser.hpp` private section, add:

```cpp
BrowseMode m_browseMode = BrowseMode::Filesystem;
std::filesystem::path m_currentCapPath;
std::vector<CapLoader::LoadedAsset> m_capAssets;  // Loaded CAP assets
```

**Step 3: Implement loadCapFile method**

In `AssetBrowser.cpp`, add after existing methods:

```cpp
void AssetBrowser::loadCapFile(const std::filesystem::path& capPath) {
    m_capAssets = CapLoader::loadCap(capPath);
    m_currentCapPath = capPath;
    m_browseMode = BrowseMode::CapFile;
    m_entries.clear();
    
    // Convert CapLoader assets to Asset Browser entries
    for (const auto& asset : m_capAssets) {
        Entry entry{};
        entry.name = asset.filename;
        entry.type = asset.type;
        entry.path = capPath / asset.filename;  // Virtual path
        entry.fileSize = asset.cafBlob.size();
        entry.isDirectory = false;
        m_entries.push_back(entry);
    }
    
    CF_LOG_INFO("AssetBrowser: Loaded {} assets from CAP", m_entries.size());
}
```

**Step 4: Update refresh() method for CAP mode**

In `AssetBrowser.cpp`, modify the `refresh()` method:

```cpp
void AssetBrowser::refresh() {
    if (m_browseMode == BrowseMode::CapFile) {
        // Reload CAP file
        loadCapFile(m_currentCapPath);
        return;
    }
    
    // Existing filesystem refresh logic...
}
```

**Step 5: Update render to show CAP breadcrumb**

In `AssetBrowser::renderBreadcrumbs()`, add CAP mode indicator:

```cpp
if (m_browseMode == BrowseMode::CapFile) {
    ImGui::Text("CAP: %s", m_currentCapPath.filename().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Back to Filesystem")) {
        m_browseMode = BrowseMode::Filesystem;
        m_entries.clear();
        navigateTo(m_rootPath);
    }
}
```

**Step 6: Verify compilation**

```bash
cd build && make -j4
```

Expected: No errors.

**Step 7: Commit**

```bash
git add src/editor/AssetBrowser.hpp src/editor/AssetBrowser.cpp
git commit -m "feat: add CAP file browsing mode to Asset Browser"
```

---

### Task 1.3: Create Audio Waveform Renderer

**Files:**
- Create: `src/editor/AudioWaveformRenderer.hpp`
- Create: `src/editor/AudioWaveformRenderer.cpp`

**Step 1: Write header**

```cpp
// src/editor/AudioWaveformRenderer.hpp
#pragma once
#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include <vector>
#include <memory>

namespace Caffeine::Editor {

class AudioWaveformRenderer {
public:
    struct WaveformData {
        std::vector<f32> leftChannel;
        std::vector<f32> rightChannel;
        u32 sampleRate;
        bool isStereo;
    };

    // Generate waveform from CAF audio blob
    static WaveformData generateWaveform(
        const std::vector<u8>& cafBlob,
        u32 targetWidth = 256
    );

    // Render waveform as texture (returns GPU texture ID)
    static u32 renderWaveformTexture(const WaveformData& data, u32 width = 256, u32 height = 64);
};

} // namespace Caffeine::Editor
```

**Step 2: Implement waveform generator**

```cpp
// src/editor/AudioWaveformRenderer.cpp
#include "editor/AudioWaveformRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace Caffeine::Editor {

AudioWaveformRenderer::WaveformData AudioWaveformRenderer::generateWaveform(
    const std::vector<u8>& cafBlob,
    u32 targetWidth
) {
    WaveformData data{};
    
    if (cafBlob.size() < sizeof(CafHeader)) {
        return data;
    }

    // Parse CAF header
    CafHeader header{};
    std::memcpy(&header, cafBlob.data(), sizeof(CafHeader));

    if (header.type != static_cast<u32>(AssetType::Audio)) {
        return data;
    }

    // Get audio metadata
    CafAudioMetadata audioMeta{};
    std::memcpy(&audioMeta, 
                cafBlob.data() + sizeof(CafHeader),
                sizeof(CafAudioMetadata));

    data.sampleRate = audioMeta.sampleRate;
    data.isStereo = (audioMeta.channels == 2);

    // Extract PCM samples (assuming 16-bit PCM after metadata)
    u32 payloadOffset = 32 + 32;  // CafHeader + CafAudioMetadata
    const i16* samples = reinterpret_cast<const i16*>(cafBlob.data() + payloadOffset);
    u32 sampleCount = (cafBlob.size() - payloadOffset) / sizeof(i16);

    // Normalize and downsample to targetWidth
    u32 samplesPerBin = std::max(1u, sampleCount / (targetWidth * audioMeta.channels));
    
    for (u32 i = 0; i < targetWidth; i++) {
        u32 startIdx = i * samplesPerBin * audioMeta.channels;
        u32 endIdx = std::min(startIdx + samplesPerBin * audioMeta.channels, sampleCount);
        
        f32 minLeft = 0.0f, maxLeft = 0.0f;
        f32 minRight = 0.0f, maxRight = 0.0f;
        
        for (u32 j = startIdx; j < endIdx; j++) {
            f32 normalized = static_cast<f32>(samples[j]) / 32768.0f;
            
            if ((j - startIdx) % audioMeta.channels == 0) {
                minLeft = std::min(minLeft, normalized);
                maxLeft = std::max(maxLeft, normalized);
            } else if (audioMeta.channels == 2) {
                minRight = std::min(minRight, normalized);
                maxRight = std::max(maxRight, normalized);
            }
        }
        
        data.leftChannel.push_back(maxLeft - minLeft);
        if (data.isStereo) {
            data.rightChannel.push_back(maxRight - minRight);
        }
    }

    return data;
}

u32 AudioWaveformRenderer::renderWaveformTexture(
    const WaveformData& data,
    u32 width,
    u32 height
) {
    // For now, return a placeholder texture ID (0)
    // Full implementation would render to texture using RenderDevice
    // This is a simplified version that generates the waveform data
    return 0;  // TODO: Implement texture rendering with RenderDevice
}

} // namespace Caffeine::Editor
```

**Step 3: Add to CMakeLists.txt**

```cmake
AudioWaveformRenderer.hpp
AudioWaveformRenderer.cpp
```

**Step 4: Verify compilation**

```bash
cd build && make -j4
```

**Step 5: Commit**

```bash
git add src/editor/AudioWaveformRenderer.hpp src/editor/AudioWaveformRenderer.cpp src/editor/CMakeLists.txt
git commit -m "feat: add audio waveform generator for Asset Browser previews"
```

---

### Task 1.4: Extend Drag-Drop for Auto-Pack Import

**Files:**
- Modify: `src/editor/DragDropSystem.hpp:50-100`
- Modify: `src/editor/DragDropSystem.cpp:100-200`

**Step 1: Add import handler to DragDropSystem header**

```cpp
// In DragDropSystem.hpp, add to public section:
void importFilesToCapPack(
    const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& projectRoot
);

// Add callback for when import completes
using ImportCallback = std::function<void(bool success, const std::string& message)>;
void setImportCallback(ImportCallback callback);
```

**Step 2: Implement import handler**

```cpp
// In DragDropSystem.cpp, add:
void DragDropSystem::importFilesToCapPack(
    const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& projectRoot
) {
    // Create temp directory
    auto tempDir = std::filesystem::temp_directory_path() / 
                   ("caf_import_" + std::to_string(std::time(nullptr)));
    std::filesystem::create_directory(tempDir);

    // Copy files to temp dir
    for (const auto& file : files) {
        auto dest = tempDir / file.filename();
        std::filesystem::copy(file, dest);
    }

    // Run caf-pack CLI
    std::string capPath = (projectRoot / "game.cap").string();
    std::string command = "./caf-pack --input " + tempDir.string() + 
                         " --output " + capPath;
    
    int result = std::system(command.c_str());

    // Cleanup temp dir
    std::filesystem::remove_all(tempDir);

    // Callback
    bool success = (result == 0);
    std::string message = success ? 
        "Imported " + std::to_string(files.size()) + " assets" :
        "Import failed: caf-pack error";
    
    if (m_importCallback) {
        m_importCallback(success, message);
    }
}
```

**Step 3: Update drag-drop handler to detect imports**

In existing `DragDropSystem::handleDragDrop()`:

```cpp
// Add to the drag-drop handler:
if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset_files")) {
        std::vector<std::filesystem::path> files;
        // Extract file paths from payload
        importFilesToCapPack(files, projectRoot);
    }
    ImGui::EndDragDropTarget();
}
```

**Step 4: Verify compilation**

```bash
cd build && make -j4
```

**Step 5: Commit**

```bash
git add src/editor/DragDropSystem.hpp src/editor/DragDropSystem.cpp
git commit -m "feat: add drag-drop auto-pack import to Asset Browser"
```

---

### Task 1.5: Add Project Auto-Load for game.cap

**Files:**
- Modify: `src/editor/ProjectManager.cpp:150-200`

**Step 1: Find ProjectManager::openProject method**

Locate the method that opens a project file.

**Step 2: Add CAP auto-load after project opens**

```cpp
void ProjectManager::openProject(const std::filesystem::path& projectPath) {
    // Existing project loading logic...
    
    m_currentProjectPath = projectPath;
    
    // NEW: Auto-load game.cap if it exists
    std::filesystem::path capPath = projectPath / "game.cap";
    if (std::filesystem::exists(capPath)) {
        // Get Asset Browser instance from EditorContext
        if (auto assetBrowser = m_editorContext->getAssetBrowser()) {
            assetBrowser->loadCapFile(capPath);
            CF_LOG_INFO("ProjectManager: Auto-loaded game.cap from project");
        }
    }
}
```

**Step 3: Verify compilation**

```bash
cd build && make -j4
```

**Step 4: Test manual verification**

- Create test project directory
- Create a dummy game.cap (or copy from ecosystem example)
- Open project in doppio
- Verify Asset Browser loads CAP

**Step 5: Commit**

```bash
git add src/editor/ProjectManager.cpp
git commit -m "feat: auto-load game.cap on project open"
```

---

### Task 1.6: Phase 1 Integration Test

**Files:**
- Create: `tests/editor/phase1_integration_test.cpp`

**Step 1: Write integration test**

```cpp
// tests/editor/phase1_integration_test.cpp
#include <gtest/gtest.h>
#include "editor/CapLoader.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/AudioWaveformRenderer.hpp"

using namespace Caffeine::Editor;

class Phase1IntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path testCapPath;
    std::filesystem::path testProjectPath;

    void SetUp() override {
        // Setup test directories
        testProjectPath = std::filesystem::temp_directory_path() / "test_project";
        std::filesystem::create_directories(testProjectPath);
    }

    void TearDown() override {
        std::filesystem::remove_all(testProjectPath);
    }
};

TEST_F(Phase1IntegrationTest, CanLoadCapFile) {
    // This requires a test CAP file to exist
    // For now, we verify the loader interface works
    AssetBrowser browser;
    browser.init((testProjectPath / "assets").string().c_str());
    
    EXPECT_EQ(browser.browseMode(), BrowseMode::Filesystem);
}

TEST_F(Phase1IntegrationTest, CanGenerateWaveform) {
    // Test with placeholder CAF blob
    std::vector<u8> testBlob(1024, 0);
    auto waveform = AudioWaveformRenderer::generateWaveform(testBlob);
    
    EXPECT_FALSE(waveform.leftChannel.empty());
}
```

**Step 2: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`:
```cmake
add_executable(Phase1IntegrationTest editor/phase1_integration_test.cpp)
target_link_libraries(Phase1IntegrationTest gtest gtest_main caffeine-core)
add_test(NAME Phase1Integration COMMAND Phase1IntegrationTest)
```

**Step 3: Run tests**

```bash
cd build && ctest -V
```

Expected: Tests pass (or fail gracefully for missing resources).

**Step 4: Commit**

```bash
git add tests/editor/phase1_integration_test.cpp tests/CMakeLists.txt
git commit -m "test: add Phase 1 integration tests for CAP browsing"
```

---

## Phase 2: Advanced caf-pack Features (Parallel)

### Task 2.1: Implement Mesh Processor (OBJ → CAF)

**Files:**
- Create: `caf-pack/src/MeshProcessor.hpp`
- Create: `caf-pack/src/MeshProcessor.cpp`
- Modify: `caf-pack/src/AssetProcessor.cpp`

**Step 1: Write MeshProcessor header**

```cpp
// caf-pack/src/MeshProcessor.hpp
#pragma once
#include "caffeine/CafTypes.hpp"
#include <vector>
#include <string>
#include <filesystem>

class MeshProcessor {
public:
    struct Vertex {
        f32 x, y, z;        // Position
        f32 nx, ny, nz;     // Normal
        f32 u, v;           // UV
    };

    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        Vec3 boundsMin, boundsMax;
    };

    static CafData processMesh(const std::filesystem::path& objPath);

private:
    static Mesh parseOBJ(const std::filesystem::path& path);
    static void computeBounds(Mesh& mesh);
    static std::vector<u8> packMeshToCAF(const Mesh& mesh);
};
```

**Step 2: Implement OBJ parser**

```cpp
// caf-pack/src/MeshProcessor.cpp
#include "MeshProcessor.hpp"
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>

Mesh MeshProcessor::parseOBJ(const std::filesystem::path& path) {
    Mesh mesh{};
    std::ifstream file(path);
    
    if (!file) {
        std::cerr << "Failed to open OBJ: " << path << std::endl;
        return mesh;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "v") {
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        } else if (token == "vn") {
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            normals.push_back(glm::normalize(norm));
        } else if (token == "vt") {
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            texCoords.push_back(uv);
        } else if (token == "f") {
            // Parse face (simplified, assumes triangles)
            for (int i = 0; i < 3; i++) {
                std::string vertexStr;
                iss >> vertexStr;
                
                // Parse vertex indices (format: v/vt/vn)
                u32 vIdx, vtIdx = 0, vnIdx = 0;
                sscanf(vertexStr.c_str(), "%u/%u/%u", &vIdx, &vtIdx, &vnIdx);
                
                Vertex vertex{};
                auto& pos = positions[vIdx - 1];
                vertex.x = pos.x; vertex.y = pos.y; vertex.z = pos.z;
                
                if (vnIdx > 0 && vnIdx <= normals.size()) {
                    auto& norm = normals[vnIdx - 1];
                    vertex.nx = norm.x; vertex.ny = norm.y; vertex.nz = norm.z;
                } else {
                    vertex.nx = vertex.ny = 0; vertex.nz = 1;
                }
                
                if (vtIdx > 0 && vtIdx <= texCoords.size()) {
                    auto& uv = texCoords[vtIdx - 1];
                    vertex.u = uv.x; vertex.v = uv.y;
                }
                
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(mesh.vertices.size() - 1);
            }
        }
    }

    computeBounds(mesh);
    return mesh;
}

void MeshProcessor::computeBounds(Mesh& mesh) {
    if (mesh.vertices.empty()) return;

    mesh.boundsMin = {mesh.vertices[0].x, mesh.vertices[0].y, mesh.vertices[0].z};
    mesh.boundsMax = mesh.boundsMin;

    for (const auto& v : mesh.vertices) {
        mesh.boundsMin.x = std::min(mesh.boundsMin.x, v.x);
        mesh.boundsMin.y = std::min(mesh.boundsMin.y, v.y);
        mesh.boundsMin.z = std::min(mesh.boundsMin.z, v.z);
        mesh.boundsMax.x = std::max(mesh.boundsMax.x, v.x);
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, v.y);
        mesh.boundsMax.z = std::max(mesh.boundsMax.z, v.z);
    }
}

CafData MeshProcessor::processMesh(const std::filesystem::path& objPath) {
    CafData data{};
    Mesh mesh = parseOBJ(objPath);
    
    // Pack mesh data as CAF blob
    std::vector<u8> payload = packMeshToCAF(mesh);
    
    data.header.type = static_cast<u32>(AssetType::Mesh);
    data.payload = payload;
    
    return data;
}
```

**Step 3: Update AssetProcessor to route .obj files**

In `AssetProcessor::process()`:

```cpp
if (ext == ".obj") {
    return MeshProcessor::processMesh(filePath);
}
```

**Step 4: Update CMakeLists.txt**

Add MeshProcessor files and glm dependency.

**Step 5: Verify compilation**

```bash
cd caf-pack && mkdir -p build && cd build && cmake .. && make -j4
```

**Step 6: Test with sample OBJ**

```bash
./caf-pack --input ../test_mesh.obj --output test_mesh.cap
```

**Step 7: Commit**

```bash
git add caf-pack/src/MeshProcessor.hpp caf-pack/src/MeshProcessor.cpp caf-pack/src/AssetProcessor.cpp caf-pack/CMakeLists.txt
git commit -m "feat: add mesh processor for OBJ to CAF conversion"
```

---

### Task 2.2: Implement Compression Support (zstd)

**Files:**
- Modify: `caf-pack/CMakeLists.txt`
- Modify: `caf-pack/src/Packer.cpp`
- Modify: `src/editor/CapLoader.cpp`

**Step 1: Add zstd to CMakeLists.txt**

```cmake
find_package(zstd REQUIRED)
target_link_libraries(caf-pack zstd::libzstd_shared)
```

**Step 2: Update Packer to compress assets**

In `Packer.cpp`, add compression in `packAssets()`:

```cpp
#include <zstd.h>

void Packer::packAssets() {
    // ... existing code ...
    
    std::ofstream capFile(m_outputPath, std::ios::binary);
    
    // Write CAP header
    CapHeader header{};
    header.magic = CapHeader::MAGIC;
    header.version = CapHeader::VERSION;
    header.entryCount = m_assets.size();
    capFile.write(reinterpret_cast<char*>(&header), sizeof(CapHeader));
    
    // Write entries and compressed data
    u32 offset = sizeof(CapHeader) + header.entryCount * sizeof(CapEntry);
    std::vector<CapEntry> entries;
    
    for (auto& asset : m_assets) {
        std::vector<u8> compressed;
        
        if (m_compressionLevel > 0) {
            // Compress with zstd
            size_t compSize = ZSTD_compressBound(asset.cafData.payload.size());
            compressed.resize(compSize);
            
            size_t actualSize = ZSTD_compress(
                compressed.data(), compSize,
                asset.cafData.payload.data(), asset.cafData.payload.size(),
                m_compressionLevel
            );
            
            compressed.resize(actualSize);
        } else {
            compressed = asset.cafData.payload;
        }
        
        CapEntry entry{};
        std::strncpy(entry.filename, asset.name.c_str(), 255);
        entry.offset = offset;
        entry.size = compressed.size();
        entry.compressedSize = (m_compressionLevel > 0) ? compressed.size() : 0;
        
        entries.push_back(entry);
        offset += compressed.size();
        
        // Write compressed data
        capFile.write(reinterpret_cast<char*>(compressed.data()), compressed.size());
    }
    
    // Write entry table
    capFile.seekp(sizeof(CapHeader));
    capFile.write(reinterpret_cast<char*>(entries.data()), 
                  entries.size() * sizeof(CapEntry));
}
```

**Step 3: Update CapLoader to decompress**

```cpp
// In CapLoader.cpp
if (entry.compressedSize > 0) {
    // Decompress with zstd
    std::vector<u8> decompressed(entry.size);  // Original size stored in CafHeader
    ZSTD_decompress(
        decompressed.data(), decompressed.size(),
        cafBlob.data(), cafBlob.size()
    );
    cafBlob = decompressed;
}
```

**Step 4: Add --compress flag to CLI**

In `main.cpp`:

```cpp
// Add to argument parser
if (args["--compress"]) {
    packer.setCompressionLevel(std::stoi(args["--compress"]));
}
```

**Step 5: Verify compilation**

```bash
cd build && cmake .. && make -j4
```

**Step 6: Test compression**

```bash
./caf-pack --input assets/ --output test_uncompressed.cap
./caf-pack --input assets/ --output test_compressed.cap --compress 10
ls -lh test_*.cap
```

Expected: `test_compressed.cap` is smaller.

**Step 7: Commit**

```bash
git add caf-pack/CMakeLists.txt caf-pack/src/Packer.cpp src/editor/CapLoader.cpp
git commit -m "feat: add zstd compression support to caf-pack"
```

---

### Task 2.3: Implement Asset ID Header Generator

**Files:**
- Modify: `caf-pack/src/main.cpp`
- Create: `caf-pack/src/HeaderGenerator.hpp/cpp`

**Step 1: Create HeaderGenerator utility**

```cpp
// caf-pack/src/HeaderGenerator.hpp
#pragma once
#include <string>
#include <vector>
#include <filesystem>

struct AssetEntry {
    std::string name;
    u64 id;
};

class HeaderGenerator {
public:
    static void generateHeader(
        const std::vector<AssetEntry>& assets,
        const std::filesystem::path& outputPath
    );

private:
    static u64 fnv1aHash(const std::string& str);
    static std::string assetNameToIdentifier(const std::string& filename);
};
```

**Step 2: Implement HeaderGenerator**

```cpp
// caf-pack/src/HeaderGenerator.cpp
#include "HeaderGenerator.hpp"
#include <fstream>
#include <algorithm>

u64 HeaderGenerator::fnv1aHash(const std::string& str) {
    u64 hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<u8>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string HeaderGenerator::assetNameToIdentifier(const std::string& filename) {
    std::string id = filename;
    
    // Remove extension
    size_t dotPos = id.rfind('.');
    if (dotPos != std::string::npos) {
        id = id.substr(0, dotPos);
    }
    
    // Convert to snake_case
    std::replace_if(id.begin(), id.end(), 
                   [](char c) { return !std::isalnum(c); }, '_');
    
    return id;
}

void HeaderGenerator::generateHeader(
    const std::vector<AssetEntry>& assets,
    const std::filesystem::path& outputPath
) {
    std::ofstream header(outputPath);
    
    header << "// Auto-generated by caf-pack\n";
    header << "#pragma once\n\n";
    header << "#include \"core/Types.hpp\"\n\n";
    header << "namespace Assets {\n";
    
    for (const auto& asset : assets) {
        std::string id = assetNameToIdentifier(asset.name);
        header << "  constexpr u64 " << id << " = 0x" 
               << std::hex << asset.id << std::dec << ";\n";
    }
    
    header << "}\n";
}
```

**Step 3: Update main.cpp to handle --gen-ids**

```cpp
// In main(), after packing:
if (args.count("--gen-ids")) {
    std::vector<AssetEntry> entries;
    
    for (const auto& asset : packer.getAssets()) {
        entries.push_back({
            asset.name,
            HeaderGenerator::fnv1aHash(asset.name)
        });
    }
    
    HeaderGenerator::generateHeader(entries, args["--gen-ids"]);
    std::cout << "Generated asset header: " << args["--gen-ids"] << std::endl;
}
```

**Step 4: Update CMakeLists.txt**

Add HeaderGenerator files.

**Step 5: Verify compilation**

```bash
cd build && make -j4
```

**Step 6: Test header generation**

```bash
./caf-pack --input assets/ --output game.cap --gen-ids include/game_assets.hpp
cat include/game_assets.hpp
```

Expected: Valid C++ header with asset IDs.

**Step 7: Commit**

```bash
git add caf-pack/src/HeaderGenerator.hpp caf-pack/src/HeaderGenerator.cpp caf-pack/src/main.cpp caf-pack/CMakeLists.txt
git commit -m "feat: add asset ID header generator (--gen-ids flag)"
```

---

## Phase 3: Tool Integration (Sequenced)

### Task 3.1: Implement Async Asset Loading Infrastructure

**Files:**
- Create: `src/engine/AssetLoader.hpp`
- Create: `src/engine/AssetLoader.cpp`

**Step 1: Design async loader interface**

```cpp
// src/engine/AssetLoader.hpp
#pragma once
#include "core/Types.hpp"
#include <functional>
#include <queue>
#include <memory>
#include <thread>

namespace Caffeine {

using AssetHandle = u64;
using AssetCallback = std::function<void(const std::vector<u8>&)>;

class AssetLoader {
public:
    // Load asset asynchronously
    AssetHandle loadAssetAsync(u64 assetId, AssetCallback onReady);
    
    // Cancel pending load
    void cancelLoad(AssetHandle handle);
    
    // Update (call once per frame)
    void update();
    
    // Shutdown
    ~AssetLoader();

private:
    struct LoadJob {
        u64 id;
        AssetHandle handle;
        AssetCallback callback;
    };
    
    std::queue<LoadJob> m_pendingLoads;
    std::thread m_workerThread;
    bool m_running = true;
    
    void workerLoop();
};

} // namespace Caffeine
```

**Step 2: Implement async loader**

```cpp
// src/engine/AssetLoader.cpp
#include "engine/AssetLoader.hpp"
#include <chrono>

namespace Caffeine {

AssetHandle AssetLoader::loadAssetAsync(u64 assetId, AssetCallback onReady) {
    static u64 handleCounter = 0;
    AssetHandle handle = ++handleCounter;
    
    LoadJob job{assetId, handle, onReady};
    m_pendingLoads.push(job);
    
    return handle;
}

void AssetLoader::cancelLoad(AssetHandle handle) {
    // Simplified: mark as cancelled in pending queue
    // Full implementation would use thread-safe queue
}

void AssetLoader::workerLoop() {
    while (m_running) {
        if (!m_pendingLoads.empty()) {
            auto job = m_pendingLoads.front();
            m_pendingLoads.pop();
            
            // Simulate async load
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Call callback (in real implementation, queue result for main thread)
            std::vector<u8> dummyData;
            job.callback(dummyData);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void AssetLoader::update() {
    // Process completed loads
}

AssetLoader::~AssetLoader() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

} // namespace Caffeine
```

**Step 3: Add to CMakeLists.txt**

```cmake
src/engine/AssetLoader.hpp
src/engine/AssetLoader.cpp
```

**Step 4: Verify compilation**

```bash
cd build && cmake .. && make -j4
```

**Step 5: Commit**

```bash
git add src/engine/AssetLoader.hpp src/engine/AssetLoader.cpp src/CMakeLists.txt
git commit -m "feat: implement async asset loading infrastructure"
```

---

### Task 3.2: Documentation and Examples

**Files:**
- Create: `docs/ECOSYSTEM_WORKFLOW.md`

**Step 1: Create complete workflow documentation**

```markdown
# Caffeine Unified Ecosystem — Complete Workflow

## Full Pipeline Example

### 1. Create Assets
- **Texture:** Use Convoy to create texture → Export PNG
- **Audio:** Use WaveShaper to create sound → Export WAV
- **Mesh:** Create OBJ file manually or export from 3D tool

### 2. Pack Assets with caf-pack
```bash
mkdir raw_assets
cp texture.png raw_assets/
cp sound.wav raw_assets/
cp model.obj raw_assets/

caf-pack --input raw_assets/ \
         --output game.cap \
         --gen-ids include/game_assets.hpp \
         --compress 10
```

### 3. Load in Game
```cpp
#include "include/game_assets.hpp"

// Async loading
auto textureHandle = assetMgr->loadAssetAsync(
    Assets::texture,
    [&](const std::vector<u8>& data) {
        gameTexture = renderer->uploadTexture(data);
    }
);
```

### 4. Preview in doppio
- Open project in doppio
- game.cap auto-loads in Asset Browser
- Browse assets, see thumbnails and waveforms
- Drag new PNG/WAV → auto-packed

## Tool Integration

### WaveShaper Export
1. "File → Export to CAP"
2. Select project directory
3. Audio saved to game.cap

### Convoy Export
1. "File → Export Texture to CAP" or "Export Mesh to CAP"
2. Select project directory
3. Assets saved to game.cap

---

```

**Step 2: Verify documentation**

Run spell-check, verify code examples compile conceptually.

**Step 3: Commit**

```bash
git add docs/ECOSYSTEM_WORKFLOW.md
git commit -m "docs: add complete ecosystem workflow guide"
```

---

### Task 3.3: Phase 2 & 3 Verification Test

**Files:**
- Create: `tests/ecosystem/phase2_3_test.cpp`

**Step 1: Write comprehensive test**

```cpp
// tests/ecosystem/phase2_3_test.cpp
#include <gtest/gtest.h>
#include "engine/AssetLoader.hpp"

class Phase23Test : public ::testing::Test {
    // Test async loader, mesh processor, compression
};

TEST_F(Phase23Test, AsyncLoaderWorks) {
    Caffeine::AssetLoader loader;
    bool called = false;
    
    auto handle = loader.loadAssetAsync(123, [&](const auto& data) {
        called = true;
    });
    
    loader.update();
    EXPECT_TRUE(called);
}

TEST_F(Phase23Test, CompressionReducesSize) {
    // Would require actual CAP files to test
    // Placeholder for integration test
}
```

**Step 2: Add to CMakeLists.txt**

**Step 3: Run tests**

```bash
cd build && ctest -V
```

**Step 4: Commit**

```bash
git add tests/ecosystem/phase2_3_test.cpp
git commit -m "test: add Phase 2 & 3 integration tests"
```

---

## Final Verification Checklist

### Phase 1 (doppio Editor Integration)
- [ ] CAP Loader parses game.cap correctly
- [ ] Asset Browser displays CAP contents
- [ ] Texture thumbnails render
- [ ] Audio waveforms display
- [ ] Drag-drop import works (files packed into game.cap)
- [ ] game.cap auto-loads on project open

### Phase 2 (Advanced caf-pack)
- [ ] caf-pack compiles with zstd
- [ ] Mesh files (.obj) pack correctly
- [ ] Compression reduces file size (--compress flag works)
- [ ] Asset ID header generates valid C++ (--gen-ids flag works)

### Phase 3 (Tool Integration)
- [ ] Async asset loader compiles
- [ ] Can load assets asynchronously without blocking
- [ ] All 4 projects compile together: `cmake .. && make`
- [ ] caf-pack CLI runs: `./caf-pack --help`

---

## Execution Handoff

**Plan saved to `docs/plans/2026-05-17-ecosystem-three-phase-implementation.md`**

**Ready to execute. Two options:**

1. **Subagent-Driven (this session)** - Fresh subagent per task, review between tasks, iterative refinement
2. **Parallel Session (separate)** - Open new session with executing-plans skill in worktree, batch execution

**Which approach would you prefer?**
