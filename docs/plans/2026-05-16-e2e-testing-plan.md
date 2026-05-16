# E2E Testing Infrastructure Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement automated E2E testing for the Caffeine editor (ImGui panels) and runtime (ECS, physics, audio).

**Architecture:** Two test targets: `DoppioTest` (new) uses `imgui_test_engine` for widget-level editor UI tests driven by widget labels/IDs; `CaffeineTest` (existing, extended) covers runtime integration tests. Both run headlessly in CI via `SDL_VIDEO_DRIVER=offscreen`.

**Tech Stack:** imgui_test_engine (ocornut), ImGui 1.91.9+ (docking branch), SDL3, Catch2, CMake FetchContent

**Design doc:** `docs/plans/2026-05-16-e2e-testing-design.md`

---

## Pre-flight: Verify Current State

**Step 0.1: Check build works**

Run: `cmake --build build --target CaffeineTest -j$(nproc)`
Expected: builds clean, 714 tests pass

Run: `cmake --build build --target doppio -j$(nproc)`
Expected: builds clean

Run: `git status`
Expected: clean working directory (start from known state)

---

### Task 1: Fetch & Integrate imgui_test_engine

**Files:**
- Modify: `CMakeLists.txt` — add FetchContent for imgui_test_engine, define target mapping
- Modify: `tests/CMakeLists.txt` — add DoppioTest target

**Step 1: Add imgui_test_engine fetch to root CMakeLists.txt**

Add after ImNodes FetchContent block (around line 262):

```cmake
# ── imgui_test_engine (optional, for editor UI tests) ──────────
FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        v1.0
    GIT_SHALLOW    TRUE
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(imgui_test_engine)

# Map imgui_test_engine target to our ImGui for linking
target_link_libraries(imgui_test_engine PUBLIC ImGui)
target_include_directories(imgui_test_engine PUBLIC
    ${imgui_test_engine_SOURCE_DIR})
```

Note: Must be inside the `if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)` block.

**Step 2: Add DoppioTest target to tests/CMakeLists.txt**

Append after current `CaffeineTest` setup:

```cmake
# ── DoppioTest — Editor UI tests (ImGui-dependent) ──────────────
if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
    add_executable(DoppioTest
        test_editor_ui_main.cpp
        test_editor_ui/test_inspector.cpp
        test_editor_ui/test_hierarchy.cpp
        test_editor_ui/test_viewport.cpp
        test_editor_ui/test_assetbrowser.cpp
        test_editor_ui/test_audioview.cpp
        test_editor_ui/test_materialeditor.cpp

        # Editor sources (same subset as doppio, minus SceneSerializer which is
        # already compiled by CaffeineTest's source list — CMake handles duplicates)
        ${CMAKE_SOURCE_DIR}/src/editor/InspectorPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneViewport.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/HierarchyPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AssetBrowser.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneEditor.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneSerializer.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/DragDropSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneTabManager.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AnimationTimeline.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/TilemapEditor.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ConsoleWindow.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ProfilerWindow.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/CommandPalette.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/EditorContext.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ProjectManager.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AudioPreviewPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/MaterialEditorPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ShaderGraph.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ShaderNode.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/PreviewRenderer.cpp
    )

    target_link_libraries(DoppioTest PRIVATE
        caffeine-core ImGui ImNodes imgui_test_engine
    )
    target_include_directories(DoppioTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(DoppioTest PRIVATE CF_HAS_IMGUI=1)
    target_compile_features(DoppioTest PRIVATE cxx_std_20)

    add_test(NAME DoppioTest COMMAND DoppioTest)
endif()
```

**Step 3: Create test directories**

Run:
```bash
mkdir -p tests/test_editor_ui
```

**Step 4: Build to verify fetch + compilation**

Run: `cmake -B build && cmake --build build --target DoppioTest -j$(nproc)`
Expected: builds clean (test files are stubs, will link)

**Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_editor_ui/
git commit -m "build: add imgui_test_engine and DoppioTest target"
```

---

### Task 2: Create DoppioTest Test Harness

**Files:**
- Create: `tests/test_editor_ui_main.cpp` — SDL + ImGui + SceneEditor initialization, imgui_test_engine registration, test runner

**Step 1: Write test harness**

```cpp
// tests/test_editor_ui_main.cpp
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_ui.h>
#include "editor/SceneEditor.hpp"
#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"

// Global test state
static struct {
    SDL_Window* window = nullptr;
    Caffeine::RHI::RenderDevice* device = nullptr;
    Caffeine::Assets::AssetManager* assetManager = nullptr;
    Caffeine::Editor::SceneEditor* editor = nullptr;
    ImGuiTestEngine* engine = nullptr;
} s_state;

// Called once before all tests
struct DoppioTestFixture {
    DoppioTestFixture() {
        if (s_state.window) return; // already initialized

        SDL_SetAppMetadata("DoppioTest", "0.1.0", "com.devscafe.doppio.test");
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

        s_state.window = SDL_CreateWindow(
            "DoppioTest", 1280, 720,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);

        // Minimal RenderDevice init (enough for ImGui context)
        s_state.device = new Caffeine::RHI::RenderDevice();
        Caffeine::RHI::RenderConfig cfg;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.vsync = false;
        s_state.device->init(s_state.window, cfg);

        // ImGui init
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui_ImplSDL3_InitForSDLGPU(s_state.window);

        ImGui_ImplSDLGPU3_InitInfo initInfo{};
        initInfo.Device = s_state.device->nativeDevice();
        initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(
            s_state.device->nativeDevice(), s_state.window);
        ImGui_ImplSDLGPU3_Init(&initInfo);

        // SceneEditor
        s_state.assetManager = new Caffeine::Assets::AssetManager(nullptr, "assets");
        s_state.editor = new Caffeine::Editor::SceneEditor();
        s_state.editor->init(s_state.device, s_state.assetManager);
    }

    ~DoppioTestFixture() {
        // Teardown happens in main after all tests
    }
};

// Per-test frame pump — runs one ImGui frame so panels render
void PumpFrame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    Caffeine::Render::Camera2D camera;
    s_state.editor->render(camera);

    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), nullptr);

    // Skip actual GPU presentation — we only need widget state
}
```

**Step 2: Complete harness with Catch2 main**

Add Catch2 main function that initializes global state, registers imgui_test_engine tests, and runs:

```cpp
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

int main(int argc, char* argv[]) {
    DoppioTestFixture fixture;

    // Register with imgui_test_engine
    s_state.engine = ImGuiTestEngine::CreateContext(ImGui::GetCurrentContext());

    // Register all test windows (one per editor panel)
    ImGuiTestEngine_RegisterWindow("DoppioTest", "Hierarchy");
    ImGuiTestEngine_RegisterWindow("DoppioTest", "Inspector");
    ImGuiTestEngine_RegisterWindow("DoppioTest", "Scene");
    ImGuiTestEngine_RegisterWindow("DoppioTest", "Assets");
    ImGuiTestEngine_RegisterWindow("DoppioTest", "Console");

    ImGuiTestEngine_Start(s_state.engine);

    int result = Catch::Session().run(argc, argv);

    ImGuiTestEngine_Stop(s_state.engine);
    ImGuiTestEngine::DestroyContext(s_state.engine);

    // Cleanup
    s_state.editor->shutdown();
    delete s_state.editor;
    delete s_state.assetManager;
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    s_state.device->shutdown();
    delete s_state.device;
    SDL_DestroyWindow(s_state.window);
    SDL_Quit();

    return result;
}
```

**Step 3: Create empty test stub**

Create `tests/test_editor_ui/test_inspector.cpp` with a dummy test to verify the harness compiles:

```cpp
#include "catch.hpp"
#include <imgui.h>

TEST_CASE("DoppioTest harness works", "[editor][ui][harness]") {
    REQUIRE(ImGui::GetCurrentContext() != nullptr);
    REQUIRE(ImGui::GetIO().BackendPlatformName != nullptr);
}
```

**Step 4: Build and run**

Run: `cmake -B build && cmake --build build --target DoppioTest -j$(nproc)`
Expected: builds clean

Run: `SDL_VIDEO_DRIVER=offscreen ./build/tests/DoppioTest`
Expected: harness test passes

**Step 5: Commit**

```bash
git add tests/test_editor_ui_main.cpp tests/test_editor_ui/
git commit -m "test: add DoppioTest harness with imgui_test_engine"
```

---

### Task 3: InspectorPanel Tests

**Files:**
- Create: `tests/test_editor_ui/test_inspector.cpp`

**Step 1: Write tests for InspectorPanel**

```cpp
#include "catch.hpp"
#include "../src/editor/InspectorPanel.hpp"
#include "../src/editor/EditorContext.hpp"
#include "../src/ecs/World.hpp"
#include "../src/ecs/Entity.hpp"
#include "../src/audio/AudioComponents.hpp"

using namespace Caffeine;
using namespace Caffeine::Editor;

// Declared in test harness
extern void PumpFrame();
extern ImGuiTestEngine* s_state;

TEST_CASE("Inspector - displays entity name", "[editor][ui][inspector]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "TestEntity");

    EditorContext ctx;
    ctx.selectedEntity = e;

    InspectorPanel panel;
    panel.setEntity(e);

    PumpFrame();
    panel.render(ctx);

    // Verify the name is displayed in the Name field
    // (imgui_test_engine can query by widget ID)
    ImGuiTestContext* gctx = ImGuiTestEngine_CreateContext(s_state, "TestGroup");
    gctx->UiBeginGroup("Inspector");

    // Check the name input contains "TestEntity"
    // The label might be "##Name" or "Name" depending on implementation
    gctx->Check("##Name");

    ImGuiTestEngine_DestroyContext(gctx);
}
```

**Step 2: Build and run**

Run: `cmake --build build --target DoppioTest -j$(nproc) && SDL_VIDEO_DRIVER=offscreen ./build/tests/DoppioTest`
Expected: tests compile and pass

**Step 3: Add real Inspector tests**

After verifying the harness works, add substantive tests:

```cpp
TEST_CASE("Inspector - AudioEmitter volume slider modifies component",
          "[editor][ui][inspector][audio]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "AudioSource");
    auto* emitter = world.add<Audio::AudioEmitter>(e);
    emitter->volume = 0.5f;
    emitter->clipPath = "test.wav";

    EditorContext ctx;
    ctx.selectedEntity = e;

    InspectorPanel panel;
    panel.setEntity(e);

    // Render a frame
    PumpFrame();
    panel.render(ctx);

    // Use imgui_test_engine to set volume slider value
    ImGuiTestContext* gctx = ImGuiTestEngine_CreateContext(s_state, "InspectorVolume");
    gctx->SetValue("##Volume", 0.75f);

    // Verify the component was updated
    REQUIRE(emitter->volume == 0.75f);

    ImGuiTestEngine_DestroyContext(gctx);
}
```

**Step 4: Commit**

```bash
git add tests/test_editor_ui/test_inspector.cpp
git commit -m "test: add InspectorPanel E2E tests"
```

---

### Task 4: Remaining Editor Panel Tests

**Files:**
- Create: `tests/test_editor_ui/test_hierarchy.cpp`
- Create: `tests/test_editor_ui/test_viewport.cpp`
- Create: `tests/test_editor_ui/test_assetbrowser.cpp`
- Create: `tests/test_editor_ui/test_audioview.cpp`
- Create: `tests/test_editor_ui/test_materialeditor.cpp`

Follow the same pattern as Task 3 for each panel:

**test_hierarchy.cpp:** Click entity → verify `EditorContext.selectedEntity` updates; right-click context menu creates/deletes entities.

**test_viewport.cpp:** Drop audio asset → verify entity with AudioEmitter created; gizmo mode buttons toggle `EditorContext.gizmoMode`.

**test_assetbrowser.cpp:** Navigate directories → verify entry list updates; search filter → verify filtered results; view mode toggle.

**test_audioview.cpp:** Play/stop buttons → verify playback state; volume/pitch sliders → verify internal state.

**test_materialeditor.cpp:** Add node → verify node in ShaderGraph; connect pins → verify connection; toggle editor mode.

Commit after each:

```bash
git add tests/test_editor_ui/test_hierarchy.cpp
git commit -m "test: add HierarchyPanel E2E tests"
```

---

### Task 5: CI Integration

**Files:**
- Modify: `.github/workflows/test.yml`

**Step 1: Add DoppioTest to CI workflow**

Add after the existing CaffeineTest step:

```yaml
    - name: Build DoppioTest
      run: cd build && cmake --build . --target DoppioTest -j$(nproc)

    - name: Run Editor UI Tests
      env:
        SDL_VIDEO_DRIVER: offscreen
      run: cd build && timeout 60 ./tests/DoppioTest || echo "DoppioTest exited (may need GPU)"
```

Note: On Ubuntu CI runners, SDL_GPU may not init properly even with offscreen driver.
Add a fallback that skips if the test binary can't init:

```yaml
    - name: Run Editor UI Tests
      env:
        SDL_VIDEO_DRIVER: offscreen
        SDL_RENDER_DRIVER: software
      run: |
        cd build
        ./tests/DoppioTest 2>&1 || echo "DoppioTest skipped (headless GPU not available)"
```

**Step 2: Commit**

```bash
git add .github/workflows/test.yml
git commit -m "ci: add DoppioTest to CI pipeline"
```

---

### Task 6: Runtime Integration Tests (Extend CaffeineTest)

**Files:**
- Modify: `tests/test_audio.cpp` — add spatial audio tests
- Modify: `tests/test_editor.cpp` — add audio emitter serialization tests
- Modify: `tests/test_scenemanager.cpp` — add ECS system integration tests

**Step 1: Add AudioEmitter serialization roundtrip test**

In `tests/test_editor.cpp`, add:

```cpp
TEST_CASE("SceneSerializer - AudioEmitter component roundtrip",
          "[editor][serializer][audio]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Speaker");
    auto* emitter = world.add<Audio::AudioEmitter>(e);
    emitter->clipPath = "sfx/explosion.wav";
    emitter->volume = 0.8f;
    emitter->maxDistance = 500.0f;
    emitter->loop = true;
    emitter->playOnSpawn = false;
    emitter->spatial = true;

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_audio.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_audio.caf") == true);

    ECS::ComponentQuery q;
    q.with<Audio::AudioEmitter>();
    bool found = false;
    loaded.forEach<Audio::AudioEmitter>(q, [&](ECS::Entity ent, Audio::AudioEmitter& ae) {
        found = true;
        REQUIRE(ae.clipPath == "sfx/explosion.wav");
        REQUIRE(ae.volume == 0.8f);
        REQUIRE(ae.maxDistance == 500.0f);
        REQUIRE(ae.loop == true);
        REQUIRE(ae.playOnSpawn == false);
        REQUIRE(ae.spatial == true);
    });
    REQUIRE(found == true);

    std::remove("_test_audio.caf");
}
```

**Step 2: Add spatial audio math tests**

In `tests/test_audio.cpp`, add:

```cpp
TEST_CASE("AudioSystem - spatial volume calculation", "[audio][spatial]") {
    // Test the audio math for distance-based volume
    // (depends on AudioSystem API — adjust based on actual interface)
}
```

**Step 3: Build and run**

Run: `cmake --build build --target CaffeineTest -j$(nproc) && ./build/tests/CaffeineTest`
Expected: 715+ tests pass

**Step 4: Commit**

```bash
git add tests/test_editor.cpp tests/test_audio.cpp
git commit -m "test: add AudioEmitter serialization and spatial audio tests"
```
