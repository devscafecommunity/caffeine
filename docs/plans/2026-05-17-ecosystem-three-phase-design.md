# Caffeine Unified Ecosystem — Three-Phase Design
**Date:** 2026-05-17  
**Status:** Approved  
**Scope:** doppio Editor Integration + Advanced caf-pack + Tool Integration

---

## Overview

Extend the unified Caffeine ecosystem with three sequential phases:
1. **Step 4: doppio Editor Integration** — CAP file browsing, asset previews, drag-and-drop import
2. **Phase 2: Advanced caf-pack Features** — Mesh processor, compression, asset ID header generation
3. **Phase 3: Tool Integration** — Network asset streaming, WaveShaper/Convoy exports, async loading

---

## Phase 1: doppio Editor Integration

### Goals
- Asset Browser can browse `.cap` files (in addition to filesystem)
- Auto-load `game.cap` on project open
- Texture preview thumbnails + audio waveform visualization
- Drag-and-drop PNG/WAV files → auto-pack into project's `game.cap`

### Architecture

#### 1.1 CAP File Deserialization (`CapLoader.hpp/cpp`)
**Purpose:** Parse CAP container format and extract assets.

**Responsibilities:**
- Read CAP header (magic, version, entry count)
- Parse CapEntry table (asset filename, type, offset, size)
- Extract individual CAF blobs from container
- Deserialize CAF metadata (texture dims, audio sample rate, etc.)

**Interface:**
```cpp
class CapLoader {
  struct LoadedAsset {
    std::string filename;
    AssetType type;
    std::vector<u8> cafBlob;
    CafHeader metadata;
  };
  
  std::vector<LoadedAsset> loadCap(const std::filesystem::path& path);
};
```

**Location:** `src/editor/CapLoader.hpp/cpp`

---

#### 1.2 Asset Browser Extensions
**Purpose:** Support both filesystem browsing and CAP file browsing.

**Changes to AssetBrowser:**
- Add mode: `enum class BrowseMode { Filesystem, CapFile }`
- New method: `loadCapFile(const std::filesystem::path& capPath)`
- Toggle in breadcrumb/toolbar to switch modes
- CAP entries show original filename + uncompressed size

**UI Indicators:**
- Breadcrumb shows: "FileSystem > path" or "CAP > game.cap > contents"
- CAP mode shows "CAP file" badge in toolbar

**Location:** Extend `src/editor/AssetBrowser.hpp/cpp`

---

#### 1.3 Texture Thumbnail Renderer
**Purpose:** Display texture previews in Asset Browser grid.

**Implementation:**
- Extend existing `PreviewRenderer` to handle CAF textures
- CAF texture → upload to GPU (using engine's renderer)
- Render quad with thumbnail in grid cell
- Fallback icon if texture is invalid (corrupted, wrong format)

**Cache:** Store GPU texture handles per asset (cleared on mode switch)

**Location:** Extend `src/editor/PreviewRenderer.hpp/cpp`

---

#### 1.4 Audio Waveform Visualizer (`AudioWaveformRenderer.hpp/cpp`)
**Purpose:** Display stereo waveform for audio assets.

**Implementation:**
- CAF audio → decode PCM samples in memory
- Compute waveform texture: stereo channels (left/right) stacked, sample min/max bars
- Render as quad in grid cell (or detail panel)
- Cache waveform texture to avoid re-computation

**Performance:** Compute waveform once on load, store as GPU texture

**Location:** `src/editor/AudioWaveformRenderer.hpp/cpp`

---

#### 1.5 Drag-and-Drop Importer (extends DragDropSystem)
**Purpose:** Accept PNG/WAV files from OS and auto-pack into game.cap.

**Workflow:**
1. User drags PNG/WAV files onto Asset Browser
2. Detect drop in `DragDropSystem::handleDragDrop()`
3. Copy files to temp directory: `/tmp/caf_import_XXXXX/`
4. Invoke `caf-pack` CLI: 
   ```bash
   ./caf-pack --input /tmp/caf_import_XXXXX/ --output <project>/game.cap
   ```
5. On success, reload game.cap in Asset Browser
6. Show toast: "Imported X assets"

**Error Handling:** Show dialog on CLI failure (missing caf-pack, pack error, etc.)

**Cleanup:** Delete temp directory after import

**Location:** Extend `src/editor/DragDropSystem.hpp/cpp`

---

#### 1.6 Project Auto-Load
**Purpose:** Automatically load game.cap when project opens.

**Implementation:**
- In `ProjectManager::openProject()`, after project path is set
- Check: `<project_root>/game.cap` exists
- If yes, call `AssetBrowser::loadCapFile(game_cap_path)`
- If no, stay in filesystem mode

**Location:** Modify `src/editor/ProjectManager.cpp`

---

### Testing
- Create test project with pre-built game.cap (texture + audio)
- Verify: browse CAP, see thumbnails, waveform renders correctly
- Drag PNG file to Asset Browser, verify auto-pack succeeds
- Close/reopen project, verify game.cap auto-loads

---

## Phase 2: Advanced caf-pack Features

### Goals
- Parse and pack 3D mesh files (OBJ → CAF format)
- Compress assets with zstd (reduce file size)
- Generate C++ header with asset IDs (--gen-ids flag)

### Architecture

#### 2.1 Mesh Processor (OBJ → CAF)
**Purpose:** Convert OBJ mesh files to CAF format.

**File:** `caf-pack/src/MeshProcessor.hpp/cpp`

**Implementation:**
- Parse OBJ file (vertices, normals, UVs, faces)
- Validate geometry (closed mesh, proper normals)
- Pack into CAF format with CafMeshMetadata:
  ```cpp
  struct CafMeshMetadata {
    u32 vertexCount;
    u32 faceCount;
    Vec3 boundMin, boundMax;  // AABB
    u32 vertexFlags;  // has normals? has UVs?
  };
  ```
- Store: vertex buffer, index buffer, normals, UVs

**Route in Packer:** Add case in `AssetProcessor::process()` for `.obj` files

**Location:** `caf-pack/src/MeshProcessor.hpp/cpp`

---

#### 2.2 Compression Support (zstd)
**Purpose:** Compress CAF payloads to reduce game.cap size.

**Implementation:**
- Link zstd library in CMakeLists.txt
- Add `--compress [level]` flag to caf-pack CLI (default: no compression)
- In `Packer::packAssets()`, compress each CAF payload before writing to container
- Store compressed size in CapEntry header (original size still in CafHeader)
- CapLoader decompresses on load (transparent to caller)

**CLI Example:** `caf-pack --input assets/ --output game.cap --compress 10`

**Location:** Modify `caf-pack/src/Packer.cpp` + `src/editor/CapLoader.cpp`

---

#### 2.3 Asset ID Header Generator (--gen-ids)
**Purpose:** Generate C++ header with compile-time asset IDs.

**Implementation:**
- New mode: `caf-pack --input assets/ --output game.cap --gen-ids include/game_assets.hpp`
- Scan assets in CAP (or input directory)
- For each asset, compute FNV-1a hash of filename
- Write C++ header:
  ```cpp
  // Auto-generated by caf-pack
  #pragma once
  #include "core/Types.hpp"
  
  namespace Assets {
    constexpr u64 player_model = 0x1a2b3c4d5e6f7g8h;
    constexpr u64 gold_texture = 0x9i8j7k6l5m4n3o2p;
    constexpr u64 background_music = 0xdeadbeefcafebabe;
  }
  ```
- Filename → asset ID mapping logged for reference

**Location:** `caf-pack/src/main.cpp` (new --gen-ids handler)

---

### Testing
- Create test OBJ mesh, verify pack succeeds (mesh appears in CAP)
- Pack with `--compress 10`, verify file size reduction
- Generate header with `--gen-ids`, verify C++ syntax valid, IDs consistent across runs

---

## Phase 3: Tool Integration

### Goals
- Implement network asset streaming (async loading infrastructure)
- WaveShaper export to `.cap`
- Convoy export to `.cap` (textures/meshes)
- Async asset loading in engine

### Architecture

#### 3.1 Network Asset Streaming Infrastructure
**Purpose:** Foundation for async asset loading (used by WaveShaper/Convoy).

**File:** `src/engine/AssetLoader.hpp/cpp`

**Implementation:**
- Define async asset loading interface:
  ```cpp
  using AssetHandle = u64;
  using AssetCallback = std::function<void(const AssetData&)>;
  
  AssetHandle loadAssetAsync(u64 assetId, AssetCallback onReady);
  void cancelLoad(AssetHandle handle);
  ```
- Thread pool for background loading
- Asset cache (LRU eviction)
- Network support (http GET for remote CAP files — future)

**Shader Hot-Reload:** Detect file change on disk, reload shader without restart

**Location:** `src/engine/AssetLoader.hpp/cpp`

---

#### 3.2 WaveShaper Integration
**Purpose:** Enable WaveShaper to export audio directly to game.cap.

**Workflow:**
1. User selects "Export to CAP" in WaveShaper
2. WaveShaper saves audio to temp WAV file
3. WaveShaper invokes: `caf-pack --input <temp_dir> --output <project>/game.cap`
4. doppio detects new game.cap, reloads Asset Browser
5. Audio immediately available in Asset Browser + engine

**Location:** WaveShaper export menu (outside Caffeine repo, but documented in ECOSYSTEM_INTEGRATION.md)

---

#### 3.3 Convoy Integration
**Purpose:** Enable Convoy to export textures/meshes to game.cap.

**Workflow:**
1. User selects "Export Texture to CAP" or "Export Mesh to CAP" in Convoy
2. Convoy saves PNG/OBJ to temp directory
3. Convoy invokes: `caf-pack --input <temp_dir> --output <project>/game.cap`
4. doppio detects new game.cap, reloads
5. Assets immediately available

**Location:** Convoy export menu (outside Caffeine repo, but documented)

---

#### 3.4 Async Asset Loading in Engine
**Purpose:** Non-blocking asset load for game code.

**Interface:**
```cpp
// Game code
auto handle = assetMgr->loadAsync(Assets::player_model);
assetMgr->onReady(handle, [](const MeshData& mesh) {
  player.setMesh(mesh);
  player.activate();
});
```

**Implementation:**
- Store pending loads in queue
- Worker thread reads from CAP asynchronously
- Call callback on completion
- No frame stutter from asset I/O

**Location:** Modify `src/engine/AssetLoader.hpp/cpp` + engine integration

---

### Testing
- Create asset in WaveShaper, export to CAP, verify loads in doppio
- Create asset in Convoy, export to CAP, verify loads in doppio
- Load large asset asynchronously in game, verify no frame stutter

---

## Dependencies

### Phase 1 Dependencies
- libpng (already present)
- zlib (already present)
- ImGui (already present in doppio)

### Phase 2 Dependencies
- zstd library (new — install via pkg-config or static link)

### Phase 3 Dependencies
- pthreads (async threading)
- Optionally: libcurl (network asset loading — Phase 3+)

---

## Verification Checklist

- [ ] Phase 1: CAP browsing + waveform rendering + drag-drop auto-pack works
- [ ] Phase 2: Mesh + compression + --gen-ids all verified
- [ ] Phase 3: Async loader + tool exports verified
- [ ] All 4 projects compile together: `cmake .. && make`
- [ ] caf-pack CLI runs: `./caf-pack --help`
- [ ] doppio loads test project with game.cap
- [ ] WaveShaper/Convoy export to CAP (manual verification)

---

## Implementation Order

1. **Phase 1 (Step 4)** — Implement in doppio editor (4-5 tasks)
2. **Phase 2 (Parallel)** — Mesh processor + compression parallel (2 tasks), then --gen-ids (1 task)
3. **Phase 3 (Sequenced)** — Infrastructure first (async loader), then WaveShaper, then Convoy, then engine integration

---

## Success Criteria

✅ **Phase 1 Complete:** Asset Browser browses game.cap, textures preview, waveforms render, drag-drop import works  
✅ **Phase 2 Complete:** caf-pack packs meshes, compresses, generates asset ID header  
✅ **Phase 3 Complete:** Engine loads assets asynchronously, WaveShaper/Convoy export to CAP  

**Final Acceptance:** All three projects (doppio, caf-pack, engine) integrated, single `cmake .. && make` build, full asset pipeline end-to-end
