# E2E Testing Infrastructure Design

Date: 2026-05-16
Author: Sisyphus
Status: Design — approved

## Overview

Establish automated E2E testing infrastructure for the Caffeine game engine,
covering both the **Doppio editor UI** (ImGui panels) and **runtime gameplay**
(ECS, audio, physics, game loop). The target is "full coverage" — equivalent
to what Playwright provides for web apps, but adapted for a native SDL3/ImGui
desktop application.

## Current State

- 714 Catch2 unit tests covering core, math, containers, ECS, serialization
- **Zero** ImGui/editor panel tests — panels are never instantiated with a live
  ImGui context in tests
- CI runs `CaffeineTest` only — `doppio` is never executed
- Two build modes: `caffeine-core` (headless, no SDL) and `doppio` (ImGui + SDL_GPU)

## Architecture

### Test Targets

| Target | What It Tests | Depends On | CI |
|--------|--------------|------------|----|
| `CaffeineTest` (existing) | Runtime: ECS, serialization, audio, physics, math, containers | caffeine-core | ✅ |
| `DoppioTest` (new) | Editor UI: panels, widgets, viewport, gizmos, asset browser | caffeine-core + ImGui + imgui_test_engine | ✅ (offscreen) |

### DoppioTest — Editor UI Testing

Uses [imgui_test_engine](https://github.com/ocornut/imgui_test_engine) by
ocornut (same author as ImGui). This is the closest equivalent to Playwright
for ImGui-based applications.

**Key capabilities provided by imgui_test_engine:**
- Click buttons, sliders, checkboxes by **widget label/ID** — not screen coordinates
- Set values on inputs: `ctx.SetValue("##Volume", 0.75f)`
- Query widget state: `ctx.Check("ButtonLabel")` asserts existence and visibility
- Wait for conditions: `ctx.Wait("//WindowName")` waits for window to appear
- Window management: open, close, dock panels
- Input simulation: keyboard, mouse at widget level
- ImGui frame lifecycle integration

**How it works:**
1. Each test creates a hidden SDL window (or `SDL_VIDEO_DRIVER=offscreen` in CI)
2. Initializes ImGui + minimal SDL3 backend (enough for widget context, no GPU needed)
3. Sets up SceneEditor with test world data
4. Runs the ImGui frame lifecycle (`NewFrame` → panels render → `EndFrame`)
5. Interacts with widgets via `ImGuiTestContext`
6. Asserts on both widget state and ECS component data

### CaffeineTest — Runtime Testing (Extended)

Extend the existing pattern:
- ECS system integration tests (simulate multiple ticks, verify component changes)
- Audio system tests (spatial audio, clip playback state)
- Game loop integration (frame timing, fixed-step updates)
- Physics simulation (step physics, check collision results)

### Offscreen/Headless Strategy

| Environment | Approach |
|-------------|----------|
| Ubuntu CI (GitHub Actions) | `SDL_VIDEO_DRIVER=offscreen` — software-rendered surface, no X11/Wayland needed |
| Windows CI | `SDL_WINDOW_HIDDEN` — window exists but is invisible |
| Local dev | Normal visible window — watch tests run interactively |

No GPU rendering required for widget-level tests — only ImGui context + frame
lifecycle. Screenshot-based visual regression is deferred to a future phase.

### Test Organization

```
tests/
├── CMakeLists.txt              # Add DoppioTest target
├── Catch2/                     # Test framework (existing)
├── test_editor_ui/             # NEW: editor UI panel tests
│   ├── test_inspector.cpp
│   ├── test_hierarchy.cpp
│   ├── test_viewport.cpp
│   ├── test_assetbrowser.cpp
│   ├── test_audioview.cpp
│   └── test_materialeditor.cpp
├── test_*.cpp                  # Existing runtime tests (extended)
└── ...
```

## Test Coverage Plan

### Editor Panels

| Panel | Test Scenarios |
|-------|---------------|
| **InspectorPanel** | Modify NameComponent text → verify entity name changes; adjust AudioEmitter volume slider → verify `emitter.volume` updated; toggle spatial checkbox → verify `emitter.spatial` flag; drag numeric fields |
| **HierarchyPanel** | Click entity name → verify `EditorContext.selectedEntity` updates; right-click context menu → create/delete entity |
| **SceneViewport** | Drop audio asset → verify entity with AudioEmitter created; gizmo visibility matches EditorContext gizmo mode; entity hover updates `EditorContext.hoveredEntity` |
| **AssetBrowser** | Navigate directories → verify entry list updates; search filter → verify filtered results; view mode toggle (Grid/List) |
| **AudioPreviewPanel** | Click play → verify playback state; volume/pitch sliders → verify internal state; stop button → verify stopped |
| **MaterialEditorPanel** | Add node via menu → verify node appears in graph; connect pins → verify connection in `ShaderGraph`; toggle text mode → verify mode switch; compile → verify output code |
| **AnimationTimeline** | Add keyframe → verify keyframe count; scrub playhead → verify current time; play/pause toggle |
| **ConsoleWindow** | Add log entries programmatically → verify display; filter by level → verify visible entries; clear → verify empty |
| **Docking/Layout** | Open/close panels → verify visibility; panel focus → verify interaction routing |

### Runtime

| System | Test Scenarios |
|--------|---------------|
| **ECS World** | Entity creation/deletion, component add/get/remove, queries, command buffer flush |
| **SceneSerializer** | Round-trip all component types including AudioEmitter, Sprite, Health |
| **AudioSystem** | Play clip, spatial audio volume/pan calculations, source position updates |
| **Physics** | Step simulation, collision queries, body creation/destruction |
| **GameLoop** | Fixed timestep accumulation, frame rate limiting, update/render separation |
| **Scripting** | Lua script execution, ECS mutation from script (when enabled) |

## CMake Integration

```cmake
# --- In tests/CMakeLists.txt or root CMakeLists.txt ---

if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
    # Fetch imgui_test_engine
    FetchContent_Declare(imgui_test_engine
        GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
        GIT_TAG        v1.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui_test_engine)

    add_executable(DoppioTest
        tests/test_editor_ui/test_inspector.cpp
        tests/test_editor_ui/test_hierarchy.cpp
        tests/test_editor_ui/test_viewport.cpp
        tests/test_editor_ui/test_assetbrowser.cpp
        tests/test_editor_ui/test_audioview.cpp
        tests/test_editor_ui/test_materialeditor.cpp
        # Editor sources needed for testing
        src/editor/InspectorPanel.cpp
        src/editor/SceneViewport.cpp
        src/editor/HierarchyPanel.cpp
        src/editor/AssetBrowser.cpp
        src/editor/SceneEditor.cpp
        src/editor/AudioPreviewPanel.cpp
        src/editor/MaterialEditorPanel.cpp
        src/editor/SceneSerializer.cpp
        src/editor/DragDropSystem.cpp
        src/editor/SceneTabManager.cpp
        src/editor/AnimationTimeline.cpp
        src/editor/TilemapEditor.cpp
        src/editor/CommandPalette.cpp
        src/editor/EditorContext.cpp
        src/editor/ProjectManager.cpp
    )
    target_link_libraries(DoppioTest PRIVATE
        caffeine-core ImGui ImNodes imgui_test_engine
    )
    target_include_directories(DoppioTest PRIVATE src)
    target_compile_definitions(DoppioTest PRIVATE CF_HAS_IMGUI=1)

    add_test(NAME DoppioTest COMMAND DoppioTest)
endif()
```

## CI Integration

Add to `.github/workflows/test.yml`:

```yaml
- name: Build DoppioTest
  run: cd build && cmake --build . --target DoppioTest

- name: Run Editor UI Tests
  env:
    SDL_VIDEO_DRIVER: offscreen
  run: cd build && ./tests/DoppioTest
```

## Future Phases

| Phase | Feature | Depends On |
|-------|---------|------------|
| 1 (now) | Widget-level E2E tests with imgui_test_engine | This design |
| 2 | Runtime integration tests (physics, audio, game loop) | Existing infra |
| 3 | Screenshot-based visual regression tests | Phase 1 + offscreen GPU |
| 4 | Stress/performance tests (frame time budgets) | Phase 2 |
| 5 | CI matrix (Ubuntu + Windows + macOS) | Phases 1-2 |
