# ProjectStartupDialog - Tab-Based Project Management Design

**Date**: 2026-05-16  
**Status**: Approved  
**Author**: Sisyphus

## Overview

Implement a tab-based ProjectStartupDialog with 3 independent tabs for project creation, recent projects, and project browsing. Replaces simplified single-screen approach with a complete project management workflow.

## Architecture & State Management

The dialog uses an **ImGui tab bar** with 3 independent tabs. Tab state is managed via `m_activeTab` (0=Create, 1=Recent, 2=Browse).

### Shared State
- `m_projectManager`: ProjectManager instance for file operations
- `m_errorMessage[512]`: Current error message buffer
- `m_toastQueue`: Queue of toast notifications (new)
- Template metadata with preview images

### Tab-Specific State

**Tab 0 (Create New Project):**
- `m_projectName[256]`: User input for project name
- `m_templateIndex`: Selected template (0=Empty, 1=2D, 2=3D)
- `m_selectedLocation`: Chosen project location path
- `m_locationPicked`: Whether location picker was used
- `m_showTemplateOnStartup`: Per-template checkbox state

**Tab 1 (Open Recent):**
- `m_recentProjects`: Cached list of recent projects
- `m_showAllRecents`: Toggle between "recent only" vs "all projects"
- `m_searchFilter[256]`: Search/filter text
- `m_selectedRecentIndex`: Currently highlighted project

**Tab 2 (Browse Projects):**
- `m_browsePath[512]`: Current search directory
- `m_browseResults`: Vector of found project paths
- `m_selectedBrowseIndex`: Currently highlighted result

## UI Layout

### Main Window Structure
```
┌─ Project Manager ────────────────────────────────┐
│  [Create New] [Open Recent] [Browse Projects]    │
│                                                   │
│  ┌─ Tab Content (dynamic) ─────────────────────┐ │
│  │                                              │ │
│  │  (Tab 0, 1, or 2 rendered here)             │ │
│  │                                              │ │
│  └──────────────────────────────────────────────┘ │
│                                                   │
│  [Toast notifications - bottom right]            │
└───────────────────────────────────────────────────┘
```

### Tab 0: Create New Project

**Components (top to bottom):**

1. **Project Name Input**
   - Label: "Project Name"
   - Input field with validation (empty check)
   - Placeholder: "MyAwesomeGame"

2. **Template Selection (Visual Cards)**
   - 3 cards displayed horizontally
   - Card layout: `[Icon] Name\n Description`
   - Template options:
     - **Empty**: Blank project, no starter assets
     - **2D**: Pre-configured for 2D games (sprites, tilemaps)
     - **3D**: Pre-configured for 3D games (models, lighting)
   - Selected card: highlighted border/background
   - Checkbox below each: "Show on startup"

3. **Location Selector**
   - Label: "Project Location"
   - Display field (read-only path)
   - Button: "Browse..." → opens file picker dialog
   - Default: `~/Documents/CaffeineProjects`

4. **Action Button**
   - "Create & Open" button (disabled if name empty)
   - On click → show progress dialog
   - Progress dialog shows spinner + "Creating project..."
   - After creation → return ProjectConfig and switch to SceneEditor

### Tab 1: Open Recent Projects

**Components (top to bottom):**

1. **Search & Filter Controls**
   - Input: "Search projects" (filters by name)
   - Toggle button: "Show All" (shows all projects vs recent only)
   - Status label: "X projects | Last 5 recent"

2. **Projects List (scrollable)**
   - Each row: `[Name] | [Path] | [Last Modified] | [Template Type] | [Open Button]`
   - Hover effect: shows project thumbnail (if available)
   - Single-click to select (highlight row)
   - "Open" button → ProjectManager::OpenProject()
   - Sorting: by most recently opened first

3. **Empty State**
   - If no projects: "No projects yet. Create one in 'Create New' tab!"

### Tab 2: Browse Projects

**Components (top to bottom):**

1. **Path Input & Browse**
   - Label: "Search Path"
   - Input field: current search directory
   - Button: "Browse Folder..." → native file picker (future implementation)

2. **Results List (scrollable)**
   - Each row: `[Name] | [Path] | [Template Type] | [Open Button]`
   - Hover: shows thumbnail preview
   - Single-click to select
   - "Open" button → ProjectManager::OpenProject()

3. **Status Bar**
   - "X projects found" or "No projects found in this directory"
   - Loading state while scanning directory

## Error Handling & User Feedback

### Toast Notification System (New)

**Purpose**: Non-intrusive error/success feedback

**Characteristics:**
- Appears in bottom-right corner of dialog
- Auto-dismisses after 3 seconds
- User can click to close immediately
- Color-coded:
  - Red (#FF4444): Error messages
  - Green (#44FF44): Success messages
  - Yellow (#FFFF44): Info/warning messages
- Max 3 toasts visible at once (queue overflow scrolls)

**Toast Messages:**
- ✓ "Project created successfully!"
- ✗ "Project name already exists"
- ✗ "Invalid project path"
- ✗ "Failed to open project: {reason}"
- ✓ "Project opened successfully"
- ℹ "No projects found in directory"

### Validation

**Create Tab:**
- Empty project name → disable "Create & Open" button + toast on click
- Invalid path → toast error
- Path exists → toast error "Project already exists at this location"

**Recent/Browse Tabs:**
- Project file missing → toast "Project no longer exists"
- Permission denied → toast "Cannot open project (permission denied)"

## Data Flow

```
┌─────────────────────────────────────────────────────────┐
│ User Interaction                                        │
└─────────────────────────┬───────────────────────────────┘
                          │
         ┌────────────────┼────────────────┐
         │                │                │
    [Tab 0]          [Tab 1]           [Tab 2]
    Create            Recent            Browse
         │                │                │
         ├─→ Input name ──┤─→ Select proj ├─→ Type path
         │    + template  │    + click    │    + click
         │    + location  │    "Open"     │    "Open"
         │                │                │
         └────────────────┼────────────────┘
                          │
                 ProjectManager
                          │
         ┌────────────────┴────────────────┐
         │                                 │
   CreateNewProject()              OpenProject()
         │                                 │
    Validate path       Check project.caffeine exists
    Create folders      Parse config file
    Write config        Load project metadata
         │                                 │
         └────────────────┬────────────────┘
                          │
                  Success? / Error?
                          │
         ┌────────────────┴────────────────┐
         │                                 │
       Toast              Toast
    "Success!"         "Error: {msg}"
       Return                 │
    ProjectConfig        Show toast
         │                    │
         └────────────────┬───┘
                          │
                    Stay in dialog
```

## Implementation Notes

1. **Progress Dialog**: Use ImGui::OpenPopup() for blocking modal during project creation
2. **File Browser Stub**: "Browse..." buttons are placeholders for future native file picker integration
3. **Thumbnail Preview**: Currently stub; future implementation will load .png from project folder
4. **Recent Projects Cache**: Loaded from ProjectManager on init()
5. **Template Cards**: Use inline button logic to handle selection (no separate component needed)
6. **Toast Queue**: Implement simple FIFO queue with timestamp-based auto-dismiss

## Success Criteria

- [x] All 3 tabs render correctly with ImGui::BeginTabBar()
- [ ] Create New: name input + template cards + location picker work
- [ ] Create New: "Create & Open" shows progress dialog and returns ProjectConfig
- [ ] Open Recent: lists all recent projects, search filters by name
- [ ] Open Recent: "Show All" toggle shows all projects vs recent only
- [ ] Browse: accepts directory path, finds project.caffeine files
- [ ] Browse: "Open" button opens selected project
- [ ] Error handling: toast notifications appear and auto-dismiss
- [ ] No IMGui errors (Missing End(), etc.)
- [ ] Code matches existing editor patterns (ProjectManager usage, error handling)

## Files to Modify

- `src/editor/ProjectStartupDialog.hpp`: Add state variables, toast system
- `src/editor/ProjectStartupDialog.cpp`: Implement 3 tab renderers + toast logic
- `src/editor/ProjectManager.hpp`: Ensure GetRecentProjects(), OpenProject() exist
- `src/editor/ProjectManager.cpp`: May need adjustments for error reporting

## Open Questions / Future Work

1. Should project creation be async or blocking?
2. How to handle large directory scans in Browse tab (10k+ projects)?
3. Thumbnail generation and caching strategy?
4. Should template metadata be data-driven (JSON) or hardcoded?
