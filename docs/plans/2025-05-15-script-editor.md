# Script Editor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a native Text Script Editor tab to the Caffeine Studio editor for editing .lua scripts directly within the IDE, integrated with the existing ScriptEngine.

**Architecture:** Create a new ScriptEditorWindow panel similar to ConsoleWindow that can open, edit, and save .lua files. Uses ImGui::InputTextMultiline for text editing with basic toolbar for Save/Run actions. Integrates with AssetBrowser to open files and ScriptEngine to execute scripts.

**Tech Stack:** C++, ImGui (InputTextMultiline), Caffeine::Script::ScriptEngine

---

### Task 1: Create ScriptEditorWindow Header

**Files:**
- Create: `src/editor/ScriptEditorWindow.hpp`

**Step 1: Create the header file with class declaration**

Create `src/editor/ScriptEditorWindow.hpp`:

```cpp
#pragma once
#include "core/Types.hpp"
#include "containers/FixedString.hpp"
#include <string>
#include <vector>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class ScriptEditorWindow {
public:
    ScriptEditorWindow() = default;

    struct OpenFile {
        std::string path;
        std::string content;
        std::string originalContent;
        bool isDirty = false;
    };

    // File operations
    bool openFile(const std::filesystem::path& path);
    bool saveFile(int index);
    bool saveFileAs(int index, const std::filesystem::path& path);
    void closeFile(int index);
    
    // Accessors
    int openFileCount() const { return static_cast<int>(m_openFiles.size()); }
    const OpenFile& file(int index) const { return m_openFiles[index]; }
    int activeFileIndex() const { return m_activeFileIndex; }
    void setActiveFile(int index) { m_activeFileIndex = index; }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

#ifdef CF_HAS_IMGUI
    void render();
#endif

private:
    std::vector<OpenFile> m_openFiles;
    int m_activeFileIndex = -1;
    bool m_open = true;
};

} // namespace Caffeine::Editor
```

**Step 2: Commit**

```bash
git add src/editor/ScriptEditorWindow.hpp
git commit -m "feat(editor): add ScriptEditorWindow header"
```

---

### Task 2: Create ScriptEditorWindow Implementation

**Files:**
- Create: `src/editor/ScriptEditorWindow.cpp`

**Step 1: Create implementation with basic text editing**

Create `src/editor/ScriptEditorWindow.cpp`:

```cpp
#include "editor/ScriptEditorWindow.hpp"
#include "core/io/FileSystem.hpp"
#include <fstream>
#include <cstring>

namespace Caffeine::Editor {

// ============================================================================
// File Operations (no ImGui)
// ============================================================================

bool ScriptEditorWindow::openFile(const std::filesystem::path& path) {
    // Check if already open
    for (const auto& f : m_openFiles) {
        if (f.path == path.string()) {
            return true; // Already open
        }
    }

    // Read file content
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    OpenFile f;
    f.path = path.string();
    f.content = std::move(content);
    f.originalContent = f.content;
    f.isDirty = false;

    m_openFiles.push_back(std::move(f));
    m_activeFileIndex = static_cast<int>(m_openFiles.size()) - 1;
    
    return true;
}

bool ScriptEditorWindow::saveFile(int index) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return false;
    }

    auto& file = m_openFiles[index];
    std::ofstream out(file.path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(file.content.data(), file.content.size());
    out.close();

    file.originalContent = file.content;
    file.isDirty = false;
    
    return true;
}

bool ScriptEditorWindow::saveFileAs(int index, const std::filesystem::path& path) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return false;
    }

    auto& file = m_openFiles[index];
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(file.content.data(), file.content.size());
    out.close();

    file.path = path.string();
    file.originalContent = file.content;
    file.isDirty = false;
    
    return true;
}

void ScriptEditorWindow::closeFile(int index) {
    if (index < 0 || index >= static_cast<int>(m_openFiles.size())) {
        return;
    }

    m_openFiles.erase(m_openFiles.begin() + index);
    
    if (m_activeFileIndex >= static_cast<int>(m_openFiles.size())) {
        m_activeFileIndex = m_openFiles.empty() ? -1 : 
            static_cast<int>(m_openFiles.size()) - 1;
    }
}

} // namespace Caffeine::Editor


// ============================================================================
// UI Layer (requires ImGui)
// ============================================================================

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void ScriptEditorWindow::render() {
    if (!m_open) return;
    
    if (ImGui::Begin("Script Editor", &m_open)) {
        // Toolbar
        if (m_activeFileIndex >= 0 && m_activeFileIndex < static_cast<int>(m_openFiles.size())) {
            auto& file = m_openFiles[m_activeFileIndex];
            
            // Save button
            if (ImGui::Button("Save")) {
                saveFile(m_activeFileIndex);
            }
            ImGui::SameLine();
            
            // Dirty indicator
            if (file.isDirty) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "*");
                ImGui::SameLine();
            }
            
            // Filename
            std::string filename = file.path.substr(file.path.find_last_of("/\\") + 1);
            ImGui::Text("%s", filename.c_str());
            
            ImGui::Separator();
            
            // Text editor using InputTextMultiline
            // Reserve a large buffer for the content
            static char textBuffer[65536] = {};
            
            // Copy content to buffer (truncate if too large)
            size_t copyLen = std::min(file.content.size(), sizeof(textBuffer) - 1);
            if (copyLen > 0) {
                std::memcpy(textBuffer, file.content.c_str(), copyLen);
            }
            textBuffer[copyLen] = '\0';
            
            ImGui::InputTextMultiline(
                "##script_content",
                textBuffer,
                sizeof(textBuffer),
                ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() - 40),
                ImGuiInputTextFlags_AllowTabInput
            );
            
            // Check if content changed
            if (std::strcmp(textBuffer, file.content.c_str()) != 0) {
                file.content = textBuffer;
                file.isDirty = true;
            }
        } else {
            ImGui::Text("No file open. Double-click a .lua file in Asset Browser to open.");
        }
    }
    ImGui::End();
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
```

**Step 2: Commit**

```bash
git add src/editor/ScriptEditorWindow.cpp
git commit -m "feat(editor): add ScriptEditorWindow implementation"
```

---

### Task 3: Integrate ScriptEditorWindow into SceneEditor

**Files:**
- Modify: `src/editor/SceneEditor.hpp`
- Modify: `src/editor/SceneEditor.cpp`

**Step 1: Add include and member to SceneEditor.hpp**

In `src/editor/SceneEditor.hpp`, add after line 10:
```cpp
#include "editor/ScriptEditorWindow.hpp"
```

Add to the member variables (around line 93):
```cpp
ScriptEditorWindow  m_scriptEditor;
```

Add accessor method (around line 68):
```cpp
ScriptEditorWindow& scriptEditor() { return m_scriptEditor; }
```

**Step 2: Add render call in SceneEditor.cpp**

In `src/editor/SceneEditor.cpp`, find where other panels are rendered and add the script editor render. Look for where `m_console.render()` is called and add near it:

```cpp
m_scriptEditor.render();
```

**Step 3: Commit**

```bash
git add src/editor/SceneEditor.hpp src/editor/SceneEditor.cpp
git commit -m "feat(editor): integrate ScriptEditorWindow into SceneEditor"
```

---

### Task 4: Add .lua file handling to AssetBrowser

**Files:**
- Modify: `src/editor/AssetBrowser.hpp`
- Modify: `src/editor/AssetBrowser.cpp`

**Step 1: Add Script asset type**

In `src/editor/AssetBrowser.hpp`, add to the AssetType enum:
```cpp
Script,  // .lua files
```

**Step 2: Update icon and handling**

In `AssetBrowser.cpp`, update `iconForType`:
```cpp
case AssetType::Script:    return "[L]";
```

And in the file type detection (around line 32-37):
```cpp
else if (ext == ".lua")                      e.type = AssetType::Script;
```

**Step 3: Add double-click to open in script editor**

In `renderGridView` or `renderListView`, add double-click handling for .lua files. The SceneEditor needs to be accessible - this will require passing a callback or using a global/event system.

**Step 4: Commit**

```bash
git add src/editor/AssetBrowser.hpp src/editor/AssetBrowser.cpp
git commit -m "feat(editor): add .lua file type to AssetBrowser"
```

---

### Task 5: Build and verify

**Step 1: Build the project**

```bash
cd build && make caffeine-core -j4
```

**Step 2: Check for any compilation errors**

Fix any issues that arise.

**Step 3: Commit any fixes**

```bash
git add . && git commit -m "fix(editor): resolve build issues"
```

---

### Task 6: Create GitHub issue for this work

**Step 1: Create an issue for M3.1.1 Script Editor**

Using the planning document format, create an issue that describes this feature as part of the M3 milestone.

```bash
gh issue create --title "M3.1.1 Script Editor" --body "$(cat docs/plans/2025-05-15-script-editor.md)"
```

---

## Summary

This plan adds a native script editor to Caffeine Studio:

1. **ScriptEditorWindow** - New panel class for editing .lua files
2. **Integration** - Added to SceneEditor alongside other panels  
3. **AssetBrowser** - .lua files now recognized and can trigger editor

The MVP is a basic text editor - syntax highlighting can be added in a future task (M3.1.2) using ImGui syntax highlighting patterns or a library like TextEditor.