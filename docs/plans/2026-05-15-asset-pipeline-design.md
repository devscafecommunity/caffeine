# Asset Pipeline Integration — Design

**Date:** 2026-05-15
**Issue:** #92
**Milestone:** M2 — Visual Editing & Assets
**Namespace:** `Caffeine::Assets`

## Overview

Automated transformation of raw source assets (PNG, JPG, WAV) into optimized `.caf` format for the Caffeine engine. The pipeline watches the `assets/raw/` directory, compiles new/changed files in the background, and maintains a global manifest.

## Architecture

```
[ assets/raw/ ]  ←── FileWatcher (ReadDirectoryChangesW)
      │
      ▼
 AssetPipeline::Update()
      │
      ├── dequeue detected files
      ├── find matching IAssetCompiler by extension
      ├── compile (synchronous, per-file)
      ├── collect logs
      └── update manifest.caf
      │
      ▼
[ assets/processed/*.caf ]
[ assets/manifest.caf ]
```

### FileWatcher (`Caffeine::IO`)

Native Windows directory monitoring via `ReadDirectoryChangesW` on a dedicated thread. Poll-based interface: `poll()` returns a vector of changed file paths since the last call. Starts watching at construction (`start()`) and can be stopped (`stop()`). Reusable utility, zero coupling to the asset pipeline.

### AssetImportContext

```cpp
struct AssetImportContext {
    std::filesystem::path SourcePath;      // raw/foo.png
    std::filesystem::path DestinationPath; // processed/foo.caf
    std::vector<std::string> Logs;         // compiler messages
    bool Success = false;
};
```

### IAssetCompiler

Abstract interface. Each compiler registers with the pipeline for one or more file extensions:

```cpp
class IAssetCompiler {
    virtual ~IAssetCompiler() = default;
    virtual bool Compile(AssetImportContext& ctx) = 0;
    virtual std::vector<std::string> GetSupportedExtensions() = 0;
};
```

### AssetPipeline

Owns the import queue, compiler registry, progress state, and manifest. Called once per frame from the editor loop:

- `Initialize(projectRoot)` — creates `assets/raw/` and `assets/processed/` directories, sets up the FileWatcher, loads or creates `manifest.caf`
- `Update()` — polls FileWatcher, queues new files, processes one file per call (spread across frames to avoid blocking)
- `RegisterCompiler(ptr)` — registers a compiler for its extensions
- `Reimport(path)` — manually re-import a single asset (called from AssetBrowser context menu)
- `GetProgress()` / `IsProcessing()` — for editor UI progress bar

### Manifest (`manifest.caf`)

A binary index file written as a valid `.caf` with a custom metadata block. Maps each asset's relative path to its compiled `.caf` path and type. Updated atomically after each successful import.

**Format:** Custom metadata: `u32 entryCount` followed by `{ AssetType type; u64 pathLen; char path[]; }` entries. The payload section is reserved for future use (could hold a hash table for O(1) lookups).

### TextureCompiler

First concrete compiler. Decodes PNG/JPG with `stb_image.h` (compile as C, header-only), writes RGBA8 pixels via `CafWriter` with `TextureMetadata`. Mipmap generation and BC7/ASTC compression deferred to a future optimization pass.

## Files

| File | Status |
|------|--------|
| `src/core/io/FileWatcher.hpp` | New |
| `src/core/io/FileWatcher.cpp` | New |
| `src/assets/AssetPipeline.hpp` | New |
| `src/assets/AssetPipeline.cpp` | New |
| `src/assets/TextureCompiler.hpp` | New |
| `src/assets/TextureCompiler.cpp` | New |
| `CMakeLists.txt` | Edit — add sources to caffeine-core |
| `tests/CMakeLists.txt` | Edit — add test_pipeline.cpp |
| `tests/test_pipeline.cpp` | New — pipeline + compiler tests |
| `src/Caffeine.hpp` | Edit — add AssetPipeline include |

## Out of Scope (v1)

- AudioCompiler (WAV → .caf)
- MeshCompiler (glTF/FBX → .caf)
- GPU texture compression (BC7, ASTC)
- Mipmap generation
- Multi-threaded compilation
- Hot-reload of already-loaded assets

## Acceptance Criteria

- Auto-detection of PNG/JPG in `assets/raw/`
- Conversion to `.caf` with correct header and metadata
- Progress queryable via `GetProgress()` / `IsProcessing()`
- Compiler logs collected in `AssetImportContext::Logs`
- `Reimport(path)` for manual re-import
- `manifest.caf` updated after each successful import
- Test coverage for: pipeline init, compiler registration, texture compilation, manifest read/write
