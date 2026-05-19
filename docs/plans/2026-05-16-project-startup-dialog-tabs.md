# ProjectStartupDialog Tabs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a 3-tab ProjectStartupDialog (Create New, Open Recent, Browse) with toast notifications and proper ImGui window management.

**Architecture:** 
- Refactor render() into tab-based architecture using ImGui::BeginTabBar()
- Extract tab rendering into separate methods (renderCreateTab, renderRecentTab, renderBrowseTab)
- Implement toast notification system for error/success feedback
- Add progress dialog for blocking project creation flow

**Tech Stack:** 
- ImGui with tab bar API
- ProjectManager (existing file operations)
- Standard file I/O for directory scanning

---

## Phase 1: Toast Notification System (Foundation)

### Task 1: Add Toast Data Structure

**Files:**
- Modify: `src/editor/ProjectStartupDialog.hpp` (add after line 50)

**Step 1: Write structure definition**

Add this to the private section of ProjectStartupDialog class:

```cpp
    // ── Toast Notification System ───────────────────────────────
    enum class ToastType {
        Success,  // Green
        Error,    // Red
        Info      // Yellow
    };

    struct Toast {
        std::string message;
        ToastType type;
        double showTime;  // SDL_GetTicks() when created
        static constexpr double DURATION_MS = 3000.0;  // 3 seconds
        
        bool isExpired(double currentTime) const {
            return (currentTime - showTime) >= DURATION_MS;
        }
    };

    std::vector<Toast> m_toastQueue;
    static constexpr int MAX_VISIBLE_TOASTS = 3;
```

**Step 2: Add toast helper methods to ProjectStartupDialog**

Add to private section (after line 80):

```cpp
    void showToast(const std::string& message, ToastType type);
    void updateToasts();
    void renderToasts();
```

**Step 3: Include necessary headers**

Add at top of ProjectStartupDialog.hpp:

```cpp
#include <vector>
#include <cstdint>
```

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.hpp
git commit -m "feat: add toast notification data structure to ProjectStartupDialog"
```

---

## Phase 2: Toast Implementation in .cpp

### Task 2: Implement Toast Methods

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (add after line 56 - setError method)

**Step 1: Implement showToast()**

Add after setError() method:

```cpp
void ProjectStartupDialog::showToast(const std::string& message, ToastType type) {
    m_toastQueue.push_back({message, type, static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0});
    if (m_toastQueue.size() > MAX_VISIBLE_TOASTS) {
        m_toastQueue.erase(m_toastQueue.begin());
    }
}
```

**Step 2: Implement updateToasts()**

Add after showToast():

```cpp
void ProjectStartupDialog::updateToasts() {
    double currentTime = static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0;
    auto it = m_toastQueue.begin();
    while (it != m_toastQueue.end()) {
        if (it->isExpired(currentTime)) {
            it = m_toastQueue.erase(it);
        } else {
            ++it;
        }
    }
}
```

**Step 3: Implement renderToasts() (stub for now)**

Add after updateToasts():

```cpp
void ProjectStartupDialog::renderToasts() {
    // Will implement ImGui rendering in Phase 3
}
```

**Step 4: Include SDL header for time**

Add to top of ProjectStartupDialog.cpp (in CF_HAS_IMGUI section):

```cpp
#include <SDL3/SDL.h>
```

**Step 5: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp
git commit -m "feat: implement toast notification methods (showToast, updateToasts)"
```

---

## Phase 3: ImGui Toast Rendering

### Task 3: Render Toasts in ImGui

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (replace renderToasts stub, line ~90)

**Step 1: Implement renderToasts() with ImGui**

Replace the stub:

```cpp
void ProjectStartupDialog::renderToasts() {
    if (m_toastQueue.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float toastWidth = 300.0f;
    float toastHeight = 60.0f;
    float padding = 10.0f;
    float spacing = 5.0f;

    float totalHeight = m_toastQueue.size() * (toastHeight + spacing);
    ImVec2 startPos(
        io.DisplaySize.x - toastWidth - padding,
        io.DisplaySize.y - totalHeight - padding
    );

    for (size_t i = 0; i < m_toastQueue.size(); ++i) {
        const Toast& toast = m_toastQueue[i];
        ImVec2 pos(startPos.x, startPos.y + i * (toastHeight + spacing));

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(toastWidth, toastHeight));

        // Color based on type
        ImVec4 bgColor;
        switch (toast.type) {
            case ToastType::Success:
                bgColor = ImVec4(0.2f, 0.7f, 0.2f, 0.8f);  // Green
                break;
            case ToastType::Error:
                bgColor = ImVec4(0.9f, 0.2f, 0.2f, 0.8f);  // Red
                break;
            case ToastType::Info:
                bgColor = ImVec4(1.0f, 1.0f, 0.2f, 0.8f);  // Yellow
                break;
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
        char label[64];
        snprintf(label, sizeof(label), "##toast_%zu", i);
        
        if (ImGui::Begin(label, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", toast.message.c_str());
            ImGui::End();
        }
        ImGui::PopStyleColor();
    }
}
```

**Step 2: Call updateToasts() and renderToasts() in main render()**

Find the main render() method and add at the very start (after the early return check):

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    if (!m_open) {
        return std::nullopt;
    }

    updateToasts();  // ADD THIS LINE
    
    // ... rest of render() ...
```

And add renderToasts() call at the very end of render(), right before the final return:

```cpp
    // At the very end of render(), before "return result;"
    renderToasts();

    return result;
}
```

**Step 3: Build and verify no ImGui errors**

```bash
cd /home/pedro/repo/caffeine/build && timeout 60 make -j8 doppio 2>&1 | tail -20
```

Expected: Should compile without errors

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp
git commit -m "feat: implement toast notification rendering with ImGui"
```

---

## Phase 4: Refactor render() into Tab-Based Architecture

### Task 4: Implement Tab Bar Structure

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (refactor render() method, lines 66-101)

**Step 1: Replace simple render() with tab structure**

Replace the entire main render() method (from line 66 to 101) with:

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    if (!m_open) {
        return std::nullopt;
    }

    updateToasts();

    std::optional<ProjectConfig> result;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Project Manager", &m_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Welcome to Doppio — Select or Create a Project");
        ImGui::Separator();

        // ── Tab Bar ─────────────────────────────────────────────────
        if (ImGui::BeginTabBar("ProjectDialogTabs")) {
            
            // Tab 0: Create New
            if (ImGui::BeginTabItem("Create New")) {
                if (auto config = renderCreateTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            // Tab 1: Open Recent
            if (ImGui::BeginTabItem("Open Recent")) {
                if (auto config = renderRecentTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            // Tab 2: Browse Projects
            if (ImGui::BeginTabItem("Browse Projects")) {
                if (auto config = renderBrowseTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        renderErrorPopup();
        ImGui::End();
    }

    renderToasts();

    return result;
}
```

**Step 2: Update renderCreateTab() to work standalone**

The existing renderCreateTab() method (lines 103-129) needs adjustment. Update it:

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::renderCreateTab() {
    std::optional<ProjectConfig> result;

    // Project Name Input
    ImGui::Text("Project Name:");
    ImGui::InputText("##ProjectName", m_projectName, sizeof(m_projectName));
    
    if (m_projectName[0] == '\0') {
        ImGui::TextDisabled("(Enter a project name)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Template Selection (3 Cards)
    ImGui::Text("Template:");
    ImGui::BeginGroup();
    
    const char* templates[] = {"Empty", "2D", "3D"};
    const char* descriptions[] = {
        "Blank project, no starter assets",
        "Pre-configured for 2D games",
        "Pre-configured for 3D games"
    };
    
    for (int i = 0; i < 3; ++i) {
        bool selected = (m_templateIndex == i);
        ImVec4 borderColor = selected ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
        
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        
        if (ImGui::Selectable(templates[i], selected, ImGuiSelectableFlags_None, ImVec2(150, 80))) {
            m_templateIndex = i;
        }
        
        ImGui::SameLine();
        ImGui::TextWrapped("%s", descriptions[i]);
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();

    // Location Selector
    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create", ImVec2(150, 0))) {
        m_locationPicked = true;
        showToast("File picker coming soon", ToastType::Info);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Create Button
    bool canCreate = (m_projectName[0] != '\0');
    if (!canCreate) ImGui::BeginDisabled();
    
    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        result = tryCreateProject();
        if (result) {
            showToast("Project created successfully!", ToastType::Success);
        } else {
            showToast("Failed to create project", ToastType::Error);
        }
    }
    
    if (!canCreate) ImGui::EndDisabled();

    return result;
}
```

**Step 3: Build and test**

```bash
cd /home/pedro/repo/caffeine/build && timeout 60 make -j8 doppio 2>&1 | tail -15
```

Expected: Should compile without errors. Test: `./doppio` should show tabbed dialog.

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp
git commit -m "feat: refactor render() into tab-based architecture with BeginTabBar"
```

---

## Phase 5: Implement "Open Recent" Tab

### Task 5: Add Recent Projects State

**Files:**
- Modify: `src/editor/ProjectStartupDialog.hpp` (add state after line 60)

**Step 1: Add state variables for Recent tab**

Add to private section (after m_browseResults):

```cpp
    // ── Recent tab state ────────────────────────────────────────
    std::vector<std::filesystem::path> m_recentProjects;
    bool m_showAllRecents = false;
    char m_searchFilter[256] = {0};
    int m_selectedRecentIndex = -1;
```

**Step 2: Commit**

```bash
git add src/editor/ProjectStartupDialog.hpp
git commit -m "feat: add recent projects state variables to ProjectStartupDialog"
```

### Task 6: Implement renderRecentTab()

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (replace stub at line ~131)

**Step 1: Load recent projects in init()**

Find the init() method and add this to load recent projects:

```cpp
void ProjectStartupDialog::init() {
    m_locationPicked = false;
    m_popupOpened = false;
    m_recentProjects = m_projectManager.GetRecentProjects();  // ADD THIS LINE
}
```

**Step 2: Implement renderRecentTab()**

Replace the existing stub:

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::renderRecentTab() {
    std::optional<ProjectConfig> result;

    // Search & Filter Controls
    ImGui::InputTextWithHint("##search_recent", "Search projects...", m_searchFilter, sizeof(m_searchFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Show All##recent", &m_showAllRecents);

    ImGui::Spacing();
    ImGui::Separator();

    // Project List
    if (ImGui::BeginChild("recent_list", ImVec2(0, 300), true)) {
        if (m_recentProjects.empty()) {
            ImGui::TextDisabled("No projects yet. Create one in 'Create New' tab!");
        } else {
            for (size_t i = 0; i < m_recentProjects.size(); ++i) {
                const auto& projPath = m_recentProjects[i];
                std::string projName = projPath.filename().string();
                
                // Apply search filter
                if (strlen(m_searchFilter) > 0) {
                    if (projName.find(m_searchFilter) == std::string::npos) {
                        continue;
                    }
                }

                bool selected = (m_selectedRecentIndex == (int)i);
                if (ImGui::Selectable(projName.c_str(), selected)) {
                    m_selectedRecentIndex = i;
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 80);
                ImGui::PushID((int)i);
                if (ImGui::Button("Open##recent", ImVec2(70, 0))) {
                    result = tryOpenProject(projPath);
                    if (result) {
                        showToast("Project opened!", ToastType::Success);
                    } else {
                        showToast("Failed to open project", ToastType::Error);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    return result;
}
```

**Step 3: Build and test**

```bash
cd /home/pedro/repo/caffeine/build && timeout 60 make -j8 doppio 2>&1 | tail -15
```

Expected: Compiles, tab shows in dialog.

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp src/editor/ProjectStartupDialog.hpp
git commit -m "feat: implement Open Recent tab with search filtering"
```

---

## Phase 6: Implement "Browse Projects" Tab

### Task 7: Implement renderBrowseTab()

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (replace stub at line ~150)

**Step 1: Implement renderBrowseTab()**

Replace the existing stub:

```cpp
std::optional<ProjectConfig> ProjectStartupDialog::renderBrowseTab() {
    std::optional<ProjectConfig> result;

    // Path Input
    ImGui::InputTextWithHint("##browse_path", "Enter directory path...", 
                            m_browsePath.data(), m_browsePath.capacity());
    ImGui::SameLine();
    if (ImGui::Button("Browse Folder...##browse", ImVec2(120, 0))) {
        showToast("File picker coming soon", ToastType::Info);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Results List
    if (ImGui::BeginChild("browse_list", ImVec2(0, 300), true)) {
        if (m_browseResults.empty()) {
            ImGui::TextDisabled("No projects found. Type a path and press Enter.");
        } else {
            ImGui::Text("Found %zu project(s):", m_browseResults.size());
            ImGui::Separator();

            for (size_t i = 0; i < m_browseResults.size(); ++i) {
                const auto& projPath = m_browseResults[i];
                std::string projName = projPath.filename().string();

                bool selected = (m_selectedBrowseIndex == (int)i);
                if (ImGui::Selectable(projName.c_str(), selected)) {
                    m_selectedBrowseIndex = i;
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 80);
                ImGui::PushID((int)i + 1000);  // Offset ID to avoid collision with Recent tab
                if (ImGui::Button("Open##browse", ImVec2(70, 0))) {
                    result = tryOpenProject(projPath);
                    if (result) {
                        showToast("Project opened!", ToastType::Success);
                    } else {
                        showToast("Failed to open project", ToastType::Error);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    return result;
}
```

**Step 2: Add state variables for Browse tab**

Add to ProjectStartupDialog.hpp private section (if not already there):

```cpp
    int m_selectedBrowseIndex = -1;
```

**Step 3: Build and test**

```bash
cd /home/pedro/repo/caffeine/build && timeout 60 make -j8 doppio 2>&1 | tail -15
```

Expected: All 3 tabs render without errors.

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp src/editor/ProjectStartupDialog.hpp
git commit -m "feat: implement Browse Projects tab with results list"
```

---

## Phase 7: Cleanup & Final Testing

### Task 8: Remove Old Tab Methods (No Longer Needed)

**Files:**
- Modify: `src/editor/ProjectStartupDialog.cpp` (remove old stubs)

**Step 1: Check if old renderCreateTab, renderRecentTab, renderBrowseTab exist as separate methods**

Look for duplicate method definitions that were stubs. If they exist as separate implementations (not the new tab-based ones), remove them to avoid confusion.

**Step 2: Clean build**

```bash
cd /home/pedro/repo/caffeine/build && rm -rf * && cmake .. && timeout 120 make -j8 doppio 2>&1 | tail -20
```

Expected: Full clean build succeeds.

**Step 3: Test execution**

```bash
cd /home/pedro/repo/caffeine/build && timeout 3 ./doppio 2>&1 | head -20
```

Expected: No crash, 3 tabs visible, clean output.

**Step 4: Commit**

```bash
git add src/editor/ProjectStartupDialog.cpp
git commit -m "cleanup: remove old stub methods, verify tab-based dialog works"
```

---

## Summary

**12 Steps Total:**
1. Add toast data structure
2. Implement toast helper methods
3. Implement toast ImGui rendering
4. Refactor render() into tabs (with updated Create tab)
5. Add recent projects state
6. Implement Open Recent tab
7. Implement Browse Projects tab
8. Cleanup & final testing

**Expected Output:**
- ProjectStartupDialog with 3 working tabs
- Toast notifications for success/error feedback
- Project creation with success feedback
- Recent projects list with search
- Browse projects (UI ready, file picker stub)

---

## Plan Ready

**Saved to:** `docs/plans/2026-05-16-project-startup-dialog-tabs.md`

### Execution Options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - You continue in separate terminal with executing-plans skill

Which approach do you prefer?
