# ProjectStartupDialog Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a blocking modal dialog that lets users create new projects or open existing ones before the Doppio editor starts.

**Architecture:** ProjectStartupDialog is a separate component with data layer (always compiled) and UI layer (CF_HAS_IMGUI guarded). It uses ProjectManager for all file operations (create, load, save). Dialog blocks main.cpp startup until user selects a project. SceneEditor::init() is updated to accept ProjectConfig instead of hardcoding "assets" path.

**Tech Stack:** C++20, ImGui (for UI), ProjectManager (existing), std::filesystem

---

## Task 1: Create ProjectStartupDialog header

**Files:**
- Create: `src/editor/ProjectStartupDialog.hpp`

**Step 1: Write the header file**

```cpp
#pragma once
#include "core/Types.hpp"
#include "editor/ProjectManager.hpp"
#include <optional>
#include <string>

namespace Caffeine::Editor {

// ============================================================================
// ProjectStartupDialog — Project selection/creation modal for editor startup
//
// Usage:
//   ProjectStartupDialog dialog;
//   dialog.init();
//   while (dialog.isOpen()) {
//       imgui.beginFrame();
//       if (auto config = dialog.render()) {
//           // User selected or created a project
//           SceneEditor.init(config.value());
//           break;
//       }
//       imgui.endFrame();
//   }
// ============================================================================
class ProjectStartupDialog {
public:
    ProjectStartupDialog();
    ~ProjectStartupDialog() = default;

    // Non-copyable
    ProjectStartupDialog(const ProjectStartupDialog&) = delete;
    ProjectStartupDialog& operator=(const ProjectStartupDialog&) = delete;

    // Initialize dialog (loads recent projects, prepares UI state)
    void init();

    // Render dialog each frame
    // Returns ProjectConfig if user selected/created a project
    // Returns std::nullopt if dialog still open
    // Modal blocks interaction with other windows
    std::optional<ProjectConfig> render();

    // Check if dialog is still open
    bool isOpen() const { return m_open; }

    // Close dialog without selecting project (user quit)
    void close() { m_open = false; }

private:
    // ── UI state ────────────────────────────────────────────────────────
    bool m_open = true;
    int m_activeTab = 0;  // 0=Create, 1=Recent, 2=Browse

    // ── Create tab state ────────────────────────────────────────────────
    char m_projectName[256] = {0};
    int m_templateIndex = 0;  // 0=Empty, 1=2D, 2=3D
    std::string m_selectedLocation;
    bool m_locationPicked = false;
    char m_errorMessage[512] = {0};
    bool m_showError = false;

    // ── Browse tab state ────────────────────────────────────────────────
    std::string m_browsePath;
    std::vector<std::filesystem::path> m_browseResults;

    // ── ProjectManager for file operations ────────────────────────────
    ProjectManager m_projectManager;

    // ── UI helpers ────────────────────────────────────────────────────
    #ifdef CF_HAS_IMGUI
    void renderCreateTab();
    void renderRecentTab();
    void renderBrowseTab();
    void renderErrorPopup();
    void setError(const char* message);
    #endif

    // ── Helper methods ────────────────────────────────────────────────
    std::optional<ProjectConfig> tryCreateProject();
    std::optional<ProjectConfig> tryOpenProject(const std::filesystem::path& path);
};

} // namespace Caffeine::Editor
```

**Step 2: Verify it compiles as a header-only stub**

The header is self-contained and doesn't require implementation yet. This sets up the interface.

---

## Task 2: Create ProjectStartupDialog implementation (Part 1: Data layer)

**Files:**
- Create: `src/editor/ProjectStartupDialog.cpp`

**Step 1: Implement constructor and init()**

```cpp
#include "editor/ProjectStartupDialog.hpp"
#include <filesystem>

namespace Caffeine::Editor {

ProjectStartupDialog::ProjectStartupDialog()
    : m_selectedLocation(std::filesystem::path(std::getenv("HOME")).string() + "/Documents/CaffeineProjects") {
    m_projectName[0] = '\0';
    m_errorMessage[0] = '\0';
}

void ProjectStartupDialog::init() {
    // ProjectManager ctor loads recent projects automatically
    // (via DefaultRecentPath)
    m_locationPicked = false;
}

std::optional<ProjectConfig> ProjectStartupDialog::tryCreateProject() {
    // Validate inputs
    if (std::string(m_projectName).empty()) {
        setError("Project name cannot be empty");
        return std::nullopt;
    }

    // Build project config
    ProjectConfig config;
    config.Name = m_projectName;
    config.RootPath = std::filesystem::path(m_selectedLocation) / m_projectName;
    config.TemplateType = (m_templateIndex == 0) ? "Empty" : (m_templateIndex == 1) ? "2D" : "3D";
    config.LastScene = "";

    // Create via ProjectManager
    if (!m_projectManager.CreateNewProject(config)) {
        setError("Failed to create project. Check permissions and path.");
        return std::nullopt;
    }

    return config;
}

std::optional<ProjectConfig> ProjectStartupDialog::tryOpenProject(const std::filesystem::path& path) {
    ProjectConfig config;
    if (!m_projectManager.OpenProject(path)) {
        setError("Failed to open project. Invalid project.caffeine file.");
        return std::nullopt;
    }
    return m_projectManager.GetCurrentProject();
}

void ProjectStartupDialog::setError(const char* message) {
    if (message) {
        std::strncpy(m_errorMessage, message, sizeof(m_errorMessage) - 1);
        m_errorMessage[sizeof(m_errorMessage) - 1] = '\0';
    }
    m_showError = true;
}

} // namespace Caffeine::Editor
```

**Step 2: Verify data layer compiles**

This part should compile without ImGui. Test by checking diagnostics.

---

## Task 3: Create ProjectStartupDialog implementation (Part 2: UI layer)

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (add UI render methods)

**Step 1: Add render() main entry point**

```cpp
#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

std::optional<ProjectConfig> ProjectStartupDialog::render() {
    #ifdef CF_HAS_IMGUI
    if (!m_open) return std::nullopt;

    ImGuiWindowFlags flags = ImGuiWindowFlags_Modal 
                            | ImGuiWindowFlags_AlwaysAutoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoResize;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("Doppio Project Manager", nullptr, flags)) {
        ImGui::Text("Welcome to Doppio — Select or Create a Project");
        ImGui::Separator();

        if (ImGui::BeginTabBar("ProjectTabs")) {
            if (ImGui::BeginTabItem("Create New")) {
                renderCreateTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Recent Projects")) {
                renderRecentTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Browse")) {
                renderBrowseTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Quit Doppio", ImVec2(120, 0))) {
            m_open = false;
            return std::nullopt;
        }

        renderErrorPopup();

        ImGui::End();
    }

    return std::nullopt;
    #else
    // Non-ImGui fallback
    m_open = false;
    return std::nullopt;
    #endif
}
```

**Step 2: Implement renderCreateTab()**

```cpp
#ifdef CF_HAS_IMGUI
void ProjectStartupDialog::renderCreateTab() {
    ImGui::InputText("Project Name", m_projectName, sizeof(m_projectName));
    ImGui::SameLine();
    ImGui::HelpMarker("Name for your new project");

    const char* templates[] = {"Empty", "2D", "3D"};
    ImGui::Combo("Template##CreateTab", &m_templateIndex, templates, IM_ARRAYSIZE(templates));
    ImGui::SameLine();
    ImGui::HelpMarker("Project template (affects initial folder structure)");

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create")) {
        // For now, use simple folder browser dialog
        // TODO: Implement native file picker or ImGui folder browser
        m_locationPicked = true;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        if (auto config = tryCreateProject()) {
            m_open = false;
            return;
        }
    }
}
#endif
```

Wait - need to handle the render return properly. Let me revise:

```cpp
#ifdef CF_HAS_IMGUI
void ProjectStartupDialog::renderCreateTab() {
    ImGui::InputText("Project Name", m_projectName, sizeof(m_projectName));
    ImGui::SameLine();
    ImGui::HelpMarker("Name for your new project");

    const char* templates[] = {"Empty", "2D", "3D"};
    ImGui::Combo("Template##CreateTab", &m_templateIndex, templates, IM_ARRAYSIZE(templates));

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create")) {
        m_locationPicked = true;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        if (auto config = tryCreateProject()) {
            m_open = false;
            // Note: actual return happens in render() after this frame
        }
    }
}
#endif
```

Actually, need to refactor render() to track selected project:

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    #ifdef CF_HAS_IMGUI
    if (!m_open) return std::nullopt;

    ImGuiWindowFlags flags = ImGuiWindowFlags_Modal 
                            | ImGuiWindowFlags_AlwaysAutoResize
                            | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    
    std::optional<ProjectConfig> result;

    if (ImGui::Begin("Doppio Project Manager", nullptr, flags)) {
        ImGui::Text("Welcome to Doppio — Select or Create a Project");
        ImGui::Separator();

        if (ImGui::BeginTabBar("ProjectTabs")) {
            if (ImGui::BeginTabItem("Create New")) {
                result = renderCreateTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Recent Projects")) {
                result = renderRecentTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Browse")) {
                result = renderBrowseTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Quit Doppio", ImVec2(120, 0))) {
            m_open = false;
        }

        renderErrorPopup();

        ImGui::End();
    }

    if (result) {
        m_open = false;
    }
    return result;
    #else
    return std::nullopt;
    #endif
}
```

And update tab render methods to return optional:

```cpp
#ifdef CF_HAS_IMGUI
std::optional<ProjectConfig> ProjectStartupDialog::renderCreateTab() {
    ImGui::InputText("Project Name", m_projectName, sizeof(m_projectName));
    ImGui::SameLine();
    ImGui::HelpMarker("Name for your new project");

    const char* templates[] = {"Empty", "2D", "3D"};
    ImGui::Combo("Template##CreateTab", &m_templateIndex, templates, IM_ARRAYSIZE(templates));

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create")) {
        m_locationPicked = true;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        return tryCreateProject();
    }
    
    return std::nullopt;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderRecentTab() {
    if (m_projectManager.GetRecentProjects().empty()) {
        ImGui::TextDisabled("No recent projects. Create a new one or browse.");
        return std::nullopt;
    }

    ImGui::BeginChild("RecentList", ImVec2(0, 300), true);
    for (const auto& recent : m_projectManager.GetRecentProjects()) {
        if (ImGui::Selectable(recent.string().c_str())) {
            return tryOpenProject(recent);
        }
    }
    ImGui::EndChild();

    return std::nullopt;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderBrowseTab() {
    static char browsePath[256] = {0};
    ImGui::InputText("Search Path", browsePath, sizeof(browsePath));
    
    if (ImGui::Button("Browse...")) {
        // TODO: Implement file picker
    }

    ImGui::Text("Browse for project.caffeine files");
    ImGui::BeginChild("BrowseList", ImVec2(0, 300), true);
    // TODO: List project files
    ImGui::EndChild();

    return std::nullopt;
}

void ProjectStartupDialog::renderErrorPopup() {
    if (m_showError) {
        ImGui::OpenPopup("ProjectError");
        m_showError = false;
    }

    if (ImGui::BeginPopupModal("ProjectError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_errorMessage);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
#endif
```

**Step 3: Update header to match return types**

Update the private method signatures to return optional:

```cpp
#ifdef CF_HAS_IMGUI
private:
    std::optional<ProjectConfig> renderCreateTab();
    std::optional<ProjectConfig> renderRecentTab();
    std::optional<ProjectConfig> renderBrowseTab();
    void renderErrorPopup();
    void setError(const char* message);
#endif
```

---

## Task 4: Update CMakeLists.txt to include new component

**Files:**
- Modify: `CMakeLists.txt` (editor sources section)

**Step 1: Add ProjectStartupDialog to editor sources**

Find the section that lists editor source files and add:

```cmake
# In src/CMakeLists.txt or main CMakeLists.txt editor section:
# Find existing lines like:
#   src/editor/ProjectManager.cpp
#   src/editor/SceneEditor.cpp
# Add after ProjectManager:
src/editor/ProjectStartupDialog.cpp
```

**Step 2: Verify CMake regeneration**

Run: `cmake ..` in build directory  
Expected: No "ProjectStartupDialog" not found errors

---

## Task 5: Update SceneEditor::init() signature

**Files:**
- Modify: `src/editor/SceneEditor.hpp` (init method)
- Modify: `src/editor/SceneEditor.cpp` (init implementation)

**Step 1: Update header**

Change from:
```cpp
#ifdef CF_HAS_SDL3
bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
          const char* assetsPath = "assets");
#endif
```

To:
```cpp
#ifdef CF_HAS_SDL3
bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
          const ProjectConfig& projectConfig);
#endif
```

**Step 2: Update implementation**

Change from:
```cpp
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                       const char* assetsPath) {
    if (!m_viewport.init(device)) return false;
    m_assetBrowser.init(assetsPath);  // ← Use projectConfig.AssetRawPath instead
    // ...
}
```

To:
```cpp
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                       const ProjectConfig& projectConfig) {
    if (!m_viewport.init(device)) return false;
    m_assetBrowser.init(projectConfig.AssetRawPath.string().c_str());
    m_currentProjectConfig = projectConfig;
    // ...
}
```

**Step 3: Add member variable to SceneEditor**

In `SceneEditor.hpp` private section:
```cpp
private:
    ProjectConfig m_currentProjectConfig;  // ← Add this
```

---

## Task 6: Update main.cpp to use ProjectStartupDialog

**Files:**
- Modify: `apps/doppio/main.cpp`

**Step 1: Add include**

Add after other editor includes:
```cpp
#include "editor/ProjectStartupDialog.hpp"
```

**Step 2: Show dialog before SceneEditor init**

Replace this section (lines 44-64):
```cpp
Caffeine::Assets::AssetManager assetManager(nullptr, "assets");
Caffeine::Render::Camera2D editorCamera;

Caffeine::Editor::ImGuiIntegration imgui;
if (!imgui.init(window, &device)) {
    // ...
}

Caffeine::Editor::SceneEditor editor;
if (!editor.init(&device, &assetManager)) {
    // ...
}
```

With:
```cpp
Caffeine::Assets::AssetManager assetManager(nullptr, "assets");
Caffeine::Render::Camera2D editorCamera;

Caffeine::Editor::ImGuiIntegration imgui;
if (!imgui.init(window, &device)) {
    std::fprintf(stderr, "ImGuiIntegration::init failed\n");
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}

// Show project startup dialog
Caffeine::Editor::ProjectStartupDialog projectDialog;
projectDialog.init();

Caffeine::Editor::ProjectConfig selectedProject;
bool projectSelected = false;

while (projectDialog.isOpen() && !projectSelected) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        imgui.processEvent(event);
        if (event.type == SDL_EVENT_QUIT) {
            imgui.shutdown();
            device.shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;  // User quit
        }
    }

    Caffeine::RHI::CommandBuffer* cmd = device.beginFrame();
    if (!cmd) continue;

    imgui.beginFrame();
    if (auto config = projectDialog.render()) {
        selectedProject = config.value();
        projectSelected = true;
    }
    imgui.prepareRender(cmd);

    Caffeine::RHI::RenderPassDesc passDesc;
    passDesc.clearColor[0] = 0.10f;
    passDesc.clearColor[1] = 0.10f;
    passDesc.clearColor[2] = 0.12f;
    passDesc.clearColor[3] = 1.00f;

    cmd->beginRenderPass(passDesc);
    imgui.endFrame(cmd);
    cmd->endRenderPass();

    device.endFrame(cmd);
}

if (!projectSelected) {
    imgui.shutdown();
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// Create asset manager with project's asset path
Caffeine::Assets::AssetManager assetManager(nullptr, selectedProject.AssetRawPath.string().c_str());

Caffeine::Editor::SceneEditor editor;
if (!editor.init(&device, &assetManager, selectedProject)) {
    std::fprintf(stderr, "SceneEditor::init failed\n");
    imgui.shutdown();
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}
```

---

## Task 7: Add #include in SceneEditor.hpp

**Files:**
- Modify: `src/editor/SceneEditor.hpp`

**Step 1: Add ProjectManager include**

Add after other editor includes:
```cpp
#include "editor/ProjectManager.hpp"
```

---

## Task 8: Verify compilation and test

**Files:**
- Build and test: `./caffeine-build`

**Step 1: Clean build**

```bash
cd build
rm -rf *
cmake ..
make -j8
```

Expected: All files compile, no linker errors, doppio executable created

**Step 2: Run doppio and verify dialog appears**

```bash
./build/apps/doppio/doppio
```

Expected: 
- Doppio window opens
- Project Manager dialog appears
- Can interact with tabs
- Cannot access editor until project selected

**Step 3: Test Create Project workflow**

- Click "Create New" tab
- Enter project name "TestProject"
- Keep default template "Empty"
- Click "Create & Open"
- Verify editor opens with project

---

## Task 9: Commit changes

**Step 1: Stage all files**

```bash
git add \
  src/editor/ProjectStartupDialog.hpp \
  src/editor/ProjectStartupDialog.cpp \
  src/editor/SceneEditor.hpp \
  src/editor/SceneEditor.cpp \
  apps/doppio/main.cpp \
  CMakeLists.txt
```

**Step 2: Commit**

```bash
git commit -m "feat: add ProjectStartupDialog for project lifecycle management

- New component: ProjectStartupDialog with 3 tabs (Create, Recent, Browse)
- Modal dialog blocks editor startup until project selected
- Integrates with existing ProjectManager for all file operations
- SceneEditor::init() now accepts ProjectConfig instead of hardcoded paths
- main.cpp shows project dialog before editor initialization
- Persists recent projects via ProjectManager

Fixes critical startup flow blocker where editor was hardcoded to 'Untitled'"
```

---

## Summary

This plan creates a complete project startup workflow in 9 tasks (2-5 minutes each):

1. **Header** - Define ProjectStartupDialog interface
2. **Data layer** - Implement create/open logic using ProjectManager
3. **UI layer** - Implement 3 tabs with ImGui
4. **CMake** - Register new component in build
5. **SceneEditor signature** - Accept ProjectConfig instead of path
6. **main.cpp integration** - Show dialog before editor init
7. **Include fix** - Add ProjectManager include
8. **Compilation test** - Verify build and runtime behavior
9. **Commit** - Save all changes

Total estimated time: 4-5 hours for complete implementation with testing.
