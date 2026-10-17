# BuildSystem Implementation Plan

**Date:** May 16, 2026  
**Status:** In Progress  
**Milestone:** M4 — Advanced Tools & Polish  
**Issue:** #123  
**Reference Design:** `docs/plans/2026-05-16-buildsystem-design.md`

---

## Overview

This plan breaks down the BuildSystem Integration into 8 bite-sized implementation tasks, each designed to compile and integrate cleanly. Each task includes acceptance criteria and verification steps.

---

## Task Breakdown

### Task 1: Create BuildSystem Core Header (5 min)

**File:** `src/editor/BuildSystem.hpp`

**What to do:**
- Define namespace `Caffeine::Editor`
- Declare enums: `BuildPlatform`, `BuildStatus`
- Declare structs: `BuildSettings`, `BuildProgress`
- Declare `class BuildSystem` with public static methods:
  - `static void ExecuteBuild(const BuildSettings& settings);`
  - `static void CancelBuild();`
  - `static BuildProgress GetProgress();`
  - `static bool IsBuilding();`
- Private methods (forward declarations only):
  - `ValidateSettings()`, `PrepareOutput()`, `CompileScripts()`, `CookAssets()`, `LinkExecutable()`, `GenerateProject()`, `RunGameAndWait()`

**Key types to include:**
- BuildPlatform: `Windows_x64 = 0, Linux_x64 = 1`
- BuildStatus: `Idle, Validating, PreparingOutput, CompilingScripts, CookingAssets, LinkingExecutable, GeneratingProject, Success, Failed, Cancelled`
- BuildSettings with fields: `projectName`, `outputDir`, `platform`, `isDebug`, `incrementalBuild`, `runAfterBuild`, `scenesToInclude`, `executableName`, `icon`, `version`
- BuildProgress with fields: `progress`, `status`, `shouldCancel`, `currentTask`

**Acceptance Criteria:**
- Header compiles without errors
- All enums and structs defined exactly per design doc
- No implementation in header (only declarations)

**Verification:**
```bash
cd build && cmake .. && make BuildSystem.hpp.o 2>&1 | grep -i error
# Should have 0 errors
```

---

### Task 2: Create BuildSystem Core Implementation (10 min)

**File:** `src/editor/BuildSystem.cpp`

**What to do:**
- Implement all 7 pipeline stages as private static methods
- Use `progress` atomic variable to update UI thread-safely
- Each stage:
  1. Check `shouldCancel` flag at start
  2. Log "Starting [stage]"
  3. Update `progress` and `currentTask`
  4. Perform stage logic (stub for now, return true)
  5. Log "[stage] complete"
  6. On failure: call `CleanupOnFailure()` and return false
- Implement `ExecuteBuild()` to call stages sequentially
- Implement `GetProgress()` to return current progress safely
- Implement `IsBuilding()` to return status != Idle
- Implement `CancelBuild()` to set `shouldCancel = true`
- Implement `CleanupOnFailure(outputDir)` to remove entire directory

**Key implementation details:**
- Use `std::filesystem::remove_all()` for cleanup
- Progress increments: Validate(0%), Prepare(5%), Scripts(20%), Assets(65%), Link(85%), Generate(95%), Success(100%)
- Log to `ConsoleWindow` using existing log system
- On any failure, cleanup and set status = Failed

**Acceptance Criteria:**
- Compiles without type errors
- All pipeline stages callable
- Progress updates atomically
- Cleanup removes output directory on failure
- Cancellation works between stages

**Verification:**
```bash
cd build && cmake .. && make
# Should compile with 0 errors
lsp_diagnostics /home/pedro/repo/caffeine/src/editor/BuildSystem.cpp
# Should show 0 errors
```

---

### Task 3: Create BuildDialog Header (5 min)

**File:** `src/editor/BuildDialog.hpp`

**What to do:**
- Declare `class BuildDialog : public EditorPanel` (inherit from existing panel class)
- Declare member variables:
  - `BuildSettings m_settings;`
  - `BuildProgress& m_progress;` (reference to BuildSystem progress)
  - `std::string m_outputPath;`
  - `std::vector<std::string> m_scenesToInclude;`
  - `bool m_showBuildLog;`
- Declare public methods:
  - `void render()` - Main UI render
  - `BuildDialog();` constructor
- Declare private helper methods (stubs):
  - `renderConfigSection()` - Platform, paths, settings
  - `renderAdvancedSection()` - Icon, version, executable name
  - `renderProgressSection()` - Progress bar, status, task
  - `renderLogSection()` - Build log display
  - `onBuildClicked()` - Handle Build button
  - `onCancelClicked()` - Handle Cancel button

**Acceptance Criteria:**
- Header compiles
- Inherits from EditorPanel correctly
- All methods declared with correct signatures

**Verification:**
```bash
cd build && cmake .. && make
# Should compile
```

---

### Task 4: Create BuildDialog Implementation Part 1 (10 min)

**File:** `src/editor/BuildDialog.cpp` — Constructor and `render()`

**What to do:**
- Implement `BuildDialog()` constructor:
  - Initialize settings with defaults (project name, output dir = "./build", platform = Windows_x64)
  - Initialize `m_showBuildLog = true`
- Implement `render()` method as main ImGui window:
  - Create ImGui window titled "Build & Run"
  - Call `renderConfigSection()`, `renderAdvancedSection()`, `renderProgressSection()`, `renderLogSection()` in order
  - Render "Build & Run" and "Cancel" buttons at bottom
  - Handle button clicks via `onBuildClicked()` and `onCancelClicked()`
- Implement `onBuildClicked()`:
  - Validate settings (output path not empty, project name not empty)
  - Call `BuildSystem::ExecuteBuild(m_settings)` to start build
  - Log "Build started" to console
- Implement `onCancelClicked()`:
  - Call `BuildSystem::CancelBuild()`
  - Log "Build cancelled" to console

**Key implementation details:**
- Use ImGui::Begin/End for window
- Use ImGui::Button for buttons with click detection
- Log to existing ConsoleWindow

**Acceptance Criteria:**
- ImGui window renders
- Buttons are clickable and trigger callbacks
- No crashes on button click

**Verification:**
```bash
cd build && cmake .. && make
lsp_diagnostics /home/pedro/repo/caffeine/src/editor/BuildDialog.cpp
# Should compile, 0 errors
```

---

### Task 5: Create BuildDialog Implementation Part 2 (8 min)

**File:** `src/editor/BuildDialog.cpp` — UI Sections

**What to do:**
- Implement `renderConfigSection()`:
  - ImGui::Separator()
  - ImGui::Text("Configuration")
  - ImGui::InputText("Project Name", &m_settings.projectName)
  - ImGui::InputText("Output Directory", &m_outputPath) with folder icon
  - ImGui::Combo("Platform", platform selector: Windows_x64 / Linux_x64)
  - ImGui::Checkbox("Debug Build", &m_settings.isDebug)
  - ImGui::Checkbox("Incremental Build", &m_settings.incrementalBuild)
  - ImGui::Checkbox("Run After Build", &m_settings.runAfterBuild)
- Implement `renderAdvancedSection()`:
  - ImGui::Separator()
  - ImGui::Text("Advanced")
  - ImGui::InputText("Executable Name", &m_settings.executableName)
  - ImGui::InputText("Icon Path", &m_settings.icon)
  - ImGui::InputText("Version", &m_settings.version)
- Implement `renderProgressSection()`:
  - Only show if `BuildSystem::IsBuilding()`
  - ImGui::ProgressBar(m_progress.progress)
  - ImGui::Text("Status: %s", StatusToString(m_progress.status))
  - ImGui::Text("Task: %s", m_progress.currentTask.c_str())
- Implement `renderLogSection()`:
  - ImGui::BeginChild("Build Log")
  - Display build log from ConsoleWindow
  - ImGui::EndChild()

**Helper function:**
- Implement `StatusToString(BuildStatus)` to convert enum to string

**Acceptance Criteria:**
- All UI sections render without crashes
- Input fields are editable
- Progress bar appears during builds
- Status text updates

**Verification:**
```bash
cd build && cmake .. && make
# No compile errors expected
```

---

### Task 6: Create AssetCooker Header & Stubs (8 min)

**File:** `src/editor/AssetCooker.hpp` + `src/editor/AssetCooker.cpp`

**Header What to do:**
- Declare `class AssetCooker`
- Public static methods:
  - `static bool CookTextures(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress);`
  - `static bool CookShaders(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress);`
  - `static bool LoadBuildCache(const std::string& cacheFile);`
  - `static bool SaveBuildCache(const std::string& cacheFile);`
- Private:
  - Helper for cache checking: `shouldCookAsset(const std::string& assetPath)`

**Implementation What to do:**
- Implement all methods as stubs that:
  - Return `true` (success)
  - Update `progress.progress` incrementally
  - Log placeholder messages like "Cooking textures... [N assets]"
  - Skip actual texture/shader compilation for now

**Cache structure (stub for now):**
- Cache file format: JSON at `outputDir/.build_cache/build_cache.json`
- Load/save functions stub (return true, don't actually load/save yet)

**Acceptance Criteria:**
- Compiles without errors
- Methods callable from BuildSystem
- Returns bool success/failure

**Verification:**
```bash
cd build && cmake .. && make
lsp_diagnostics /home/pedro/repo/caffeine/src/editor/AssetCooker.cpp
# Should compile, 0 errors
```

---

### Task 7: Wire BuildDialog into Editor (5 min)

**File:** `apps/doppio/main.cpp` (the editor app)

**What to do:**
- Find where `ConsoleWindow`, `InspectorPanel`, etc. are created in the editor loop
- Add instantiation of `BuildDialog`:
  ```cpp
  static BuildDialog buildDialog;
  ```
- Call `buildDialog.render()` in the main editor ImGui render loop (alongside other panel renders)
- Ensure BuildDialog renders after ConsoleWindow so logs are visible

**Acceptance Criteria:**
- Editor compiles
- BuildDialog window appears in editor UI
- No crashes on startup

**Verification:**
```bash
cd build && cmake .. && make doppio
./bin/doppio
# Editor launches, "Build & Run" window visible (can resize/close like other panels)
```

---

### Task 8: Integration Test & Full Build (10 min)

**File:** Integration verification (no new files)

**What to do:**
1. Launch editor: `./bin/doppio`
2. Locate "Build & Run" window
3. Set output directory to `./build/test_game`
4. Click "Build & Run"
5. Verify:
   - Progress bar appears
   - Status changes: Validating → Preparing → Compiling → Cooking → Linking → Generating → Success
   - Console logs all stages
   - Dialog shows "Build complete" message
   - `./build/test_game/` directory exists with placeholder files
6. Test cancellation:
   - Click "Build & Run" again
   - Click "Cancel" mid-build
   - Verify `./build/test_game/` is cleaned up (removed)
   - Verify status shows "Cancelled"

**Acceptance Criteria:**
- Full build pipeline executes without crashes
- Progress updates visibly in UI
- Cancellation works and cleans up
- Log shows all 7 stages completed

**Verification:**
```bash
./bin/doppio
# Manual UI testing - no automated test yet
# Then run:
cd build && make test  # Ensure no regression in other tests
```

---

## Sequential Execution Order

1. **Task 1** → BuildSystem header (5 min)
2. **Task 2** → BuildSystem implementation (10 min)
3. **Task 3** → BuildDialog header (5 min)
4. **Task 4** → BuildDialog constructor + render (10 min)
5. **Task 5** → BuildDialog UI sections (8 min)
6. **Task 6** → AssetCooker stubs (8 min)
7. **Task 7** → Wire into editor main.cpp (5 min)
8. **Task 8** → Integration test (10 min)

**Total estimated time:** ~60 minutes

---

## Commit Strategy

After each task, commit atomically:

```bash
# After Task 1
git add src/editor/BuildSystem.hpp
git commit -m "feat: add BuildSystem header with enums and struct definitions"

# After Task 2
git add src/editor/BuildSystem.cpp
git commit -m "feat: implement BuildSystem 7-stage pipeline with progress tracking"

# After Tasks 3-5
git add src/editor/BuildDialog.hpp src/editor/BuildDialog.cpp
git commit -m "feat: implement BuildDialog ImGui panel with configuration and progress sections"

# After Task 6
git add src/editor/AssetCooker.hpp src/editor/AssetCooker.cpp
git commit -m "feat: add AssetCooker stub with texture and shader cooking interfaces"

# After Task 7
git add apps/doppio/main.cpp
git commit -m "feat: wire BuildDialog into editor UI rendering loop"

# After Task 8
git add -A  # If any changes
git commit -m "test: verify BuildSystem integration and full build pipeline execution"
```

---

## Verification Checklist

Before marking each task complete:

- [ ] File compiles: `cmake .. && make`
- [ ] No LSP diagnostics: `lsp_diagnostics /path/to/file`
- [ ] Methods are callable from dependent code
- [ ] No breaking changes to existing codebase

---

## Next Session (After This Plan)

- **Asset cooking implementations:** Actual texture/shader compilation using TextureCompiler and ShaderCompiler APIs
- **Build cache implementation:** Load/save JSON, mtime/hash checking
- **Game execution:** Platform-specific process launching (CreateProcess for Windows, fork/execv for Linux)
- **Integration with ProjectManager:** Load scenes and project settings from `project.caffeine`

---

**Plan created:** May 16, 2026  
**Plan approved by:** Implementation phase  
**Expected completion:** Same day (~1 hour total)
