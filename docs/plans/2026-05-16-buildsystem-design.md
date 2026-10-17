# BuildSystem Integration Design Document

**Date:** May 16, 2026  
**Status:** Approved  
**Milestone:** M4 — Advanced Tools & Polish  
**Issue:** #123  
**Namespace:** `Caffeine::Editor`

---

## Overview

The **BuildSystem Integration** transforms Caffeine Studio into a complete game development environment, enabling creators to go from design to execution with one click. This system coordinates the entire compilation pipeline: asset cooking, script validation, runtime linking, and game execution.

**Key Capabilities:**
- Full-featured build pipeline (Windows_x64 and Linux_x64)
- Incremental builds with asset caching
- Real-time progress tracking and logging
- Automatic game execution with process waiting
- Comprehensive error handling and cleanup

---

## Architecture

### Core Types

```cpp
namespace Caffeine::Editor {

enum class BuildPlatform : u8 {
    Windows_x64 = 0,
    Linux_x64   = 1
};

enum class BuildStatus : u8 {
    Idle,
    Validating,
    PreparingOutput,
    CompilingScripts,
    CookingAssets,
    LinkingExecutable,
    GeneratingProject,
    Success,
    Failed,
    Cancelled
};

struct BuildSettings {
    std::string projectName;
    std::string outputDir;
    BuildPlatform platform;
    bool isDebug = false;
    bool incrementalBuild = true;
    bool runAfterBuild = false;
    std::vector<std::string> scenesToInclude;
    std::string executableName;
    std::string icon;
    std::string version;
};

struct BuildProgress {
    std::atomic<f32> progress = 0.0f;              // 0.0 - 1.0
    std::atomic<BuildStatus> status = BuildStatus::Idle;
    std::atomic<bool> shouldCancel = false;
    std::string currentTask;
};

}
```

### BuildSystem Class

**Responsibility:** Coordinate pipeline execution in background thread  
**Key Methods:**
- `ExecuteBuild()` - Main entry point (runs in JobSystem thread)
- `RunGameAndWait()` - Launch and wait for game process
- `ValidateSettings()`, `PrepareOutputDirectory()`, `CompileScripts()`, etc. - Pipeline stages

**Design Pattern:** Static coordinator with atomic progress tracking for thread-safe UI updates

### BuildDialog Class

**Responsibility:** ImGui configuration panel and progress display  
**Key Methods:**
- `render()` - Draw UI sections (config, progress, logs, buttons)
- `renderConfigSection()` - Platform, paths, scene selection
- `renderAdvancedSection()` - Icon, version, executable name
- `renderProgressSection()` - Progress bar, status, current task
- `renderLogSection()` - Scrollable build log

**Design Pattern:** Inherits editor panel patterns; polls atomic progress variables for thread-safe updates

---

## Pipeline Execution Flow

### Stage-by-Stage Breakdown

1. **VALIDATE** (0%)
   - Check output path exists/writable
   - Verify project.caffeine readable
   - Validate platform is supported
   
2. **PREPARE OUTPUT** (5%)
   - Create output directory structure
   - Clear any existing build artifacts

3. **COMPILE SCRIPTS** (10% → 20%)
   - Use LuaValidator to check syntax
   - Report errors to console
   - Abort if validation fails

4. **COOK ASSETS** (20% → 65%)
   - Load build cache from `output_dir/.build_cache/`
   - For each texture: check mtime+hash, skip if cached, otherwise cook PNG/TGA → DDS
   - For each shader: check mtime+hash, skip if cached, otherwise compile GLSL → SPIR-V
   - Save updated cache with new mtime/hash/size

5. **LINK EXECUTABLE** (65% → 85%)
   - Copy CaffeineRuntime binary to output_dir
   - Copy assets/ and scripts/ to output_dir/data/
   - Set executable permissions on Linux

6. **GENERATE PROJECT** (85% → 95%)
   - Create `project.caffeine` JSON file
   - Include startup scenes, version, executable name
   - Write to output_dir root

7. **SUCCESS** (100%)
   - If `runAfterBuild == true`:
     - Launch game executable
     - Block and wait for process to close
   - Log "Build complete" to console

### Failure Handling

**On any stage error:**
- Log detailed error to console
- Call `CleanupOnFailure()` → Remove entire output_dir
- Set `status = BuildStatus::Failed`
- Allow user to fix issues and retry

---

## Incremental Build System

### Cache File Format

Location: `output_dir/.build_cache/build_cache.json`

```json
{
  "buildDate": "2026-05-16T14:30:00Z",
  "platform": "Windows_x64",
  "caffeineVersion": "0.2.0",
  "assets": {
    "textures/skybox_night.png": {
      "modifiedTime": 1715867405,
      "contentHash": "a3f5d8e2c1b4",
      "cookedSize": 2097152,
      "cooked": true
    }
  }
}
```

### Cache Checking Logic

Before cooking each asset:
1. Load cache from `output_dir/.build_cache/`
2. Get current file mtime
3. If cache entry exists:
   - Compare mtime: if same, compute content hash
   - Compare hash: if same, skip cooking
   - If different: re-cook and update cache
4. If no cache entry: cook and add to cache
5. After all assets cooked: save updated cache

---

## Asset Cooking Implementations

### Texture Cooking
- **Input:** PNG, TGA, JPG
- **Output:** DDS (BC1/BC3 compression based on alpha)
- **Uses:** Existing `Assets::TextureCompiler` API

### Shader Cooking
- **Input:** GLSL source (.vert, .frag, .comp)
- **Output:** SPIR-V binary format
- **Uses:** New `ShaderCompiler` class (integrate with existing RHI)

### Script Validation
- **Input:** Lua source files
- **Output:** Validation pass/fail + error messages
- **Uses:** Existing `Script::LuaValidator` or similar

---

## Threading & UI Integration

### Execution Model
- **Main Thread:** User clicks "Build & Run", schedules job
- **Job Thread:** Executes pipeline, updates atomic progress
- **UI Thread (Main):** Polls progress every frame, renders UI

### Progress Updates (Thread-Safe)
```cpp
BuildProgress progress;
progress.progress = 0.2f;           // Atomic update
progress.status = BuildStatus::CompilingScripts;
progress.currentTask = "Validating player.lua";
```

**Dialog polls each frame:**
```cpp
float pct = m_progress.progress * 100.0f;
ImGui::ProgressBar(m_progress.progress);
ImGui::Text("Status: %s", StatusToString(m_progress.status));
ImGui::Text("Task: %s", m_progress.currentTask.c_str());
```

### Cancellation
- User clicks "Cancel" button
- Sets `progress.shouldCancel = true`
- Job checks flag between stages
- On cancel: cleanup and exit gracefully

---

## Game Execution & Process Management

### Execution Flow (runAfterBuild = true)

1. After successful build, launch game:
   ```cpp
   std::string exePath = settings.outputDir + "/" + settings.executableName;
   return RunGameAndWait(exePath);
   ```

2. Platform-specific process launching:
   - **Windows:** `CreateProcess()` + `WaitForSingleObject()`
   - **Linux:** `fork()` + `waitpid()` or `std::system()`

3. UI shows "Game running..." during wait

4. When game closes, return to build dialog

---

## Platform-Specific Implementation

### Windows_x64
- **Runtime:** `bin/CaffeineRuntime.exe`
- **Output:** `.exe` + data folder
- **Asset Format:** DDS, SPIR-V
- **Process launch:** `CreateProcess()` from `<windows.h>`
- **Binary detection:** Check for `.exe` extension

### Linux_x64
- **Runtime:** `bin/CaffeineRuntime` (no extension)
- **Output:** ELF binary + data folder
- **Asset Format:** DDS, SPIR-V (same as Windows)
- **Process launch:** `fork()` + `execv()` from `<unistd.h>`
- **Permissions:** `chmod +x` on output binary

---

## Error Handling Strategy

### Validation Errors
- Missing/invalid paths
- Unsupported platform
- **Action:** Fail immediately in dialog, highlight field

### Script Compilation Errors
- Lua syntax errors
- **Action:** List all errors in console, don't proceed to asset cooking
- **Recovery:** User fixes script, clicks "Build & Run" again

### Asset Cooking Errors
- Texture not found
- Shader compilation failed
- **Action:** Log error, skip asset, continue build (allow partial builds)
- **Recovery:** Fix asset, rebuild

### Link/Package Errors
- Runtime not found
- Permission denied
- **Action:** Fail, cleanup output_dir completely

### Cleanup on Failure
```cpp
static bool CleanupOnFailure(const std::string& outputDir) {
    std::filesystem::remove_all(outputDir);
    // Ensures no partial builds remain
    return true;
}
```

---

## Acceptance Criteria

- ✅ Dialog allows configurable build settings (platform, debug/release, output path)
- ✅ "Build & Run" executes full pipeline and auto-launches game
- ✅ Console displays real-time build progress with detailed logs
- ✅ Incremental builds skip unchanged assets (faster iterations)
- ✅ Build failure always cleans up output_dir
- ✅ Game process waits for close before returning to editor
- ✅ project.caffeine generated with startup scenes
- ✅ Supports Windows_x64 and Linux_x64 platforms

---

## Files to Create

1. `src/editor/BuildSystem.hpp` - Core coordinator class
2. `src/editor/BuildSystem.cpp` - Pipeline implementation
3. `src/editor/BuildDialog.hpp` - ImGui panel
4. `src/editor/BuildDialog.cpp` - Panel rendering
5. `src/editor/AssetCooker.hpp` - Texture/shader cooking
6. `src/editor/AssetCooker.cpp` - Asset processing

---

## Dependencies

- **Upstream:** `src/editor/ProjectManager.hpp` (project config)
- **Upstream:** `src/assets/TextureCompiler.hpp` (texture cooking)
- **Upstream:** `src/threading/JobSystem.hpp` (background execution)
- **Upstream:** `src/editor/ConsoleWindow.hpp` (log display)
- **New:** `src/render/ShaderCompiler.hpp` (shader cooking)
- **New:** `src/script/LuaValidator.hpp` (script validation)

---

**Design approved by:** User  
**Approval date:** May 16, 2026
