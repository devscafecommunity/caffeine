# Project Manager — Design

**Date:** 2026-05-15
**Issue:** #94
**Milestone:** M2 — Visual Editing & Assets
**Namespace:** `Caffeine::Editor`

## Overview

Entry point of the Caffeine Studio IDE. Allows users to create new projects from templates, open existing projects, and manage the list of recent projects. A "Project" is defined by a `project.caffeine` (JSON) file containing engine metadata, asset paths, and build settings.

## Architecture

```
Caffeine Studio launch
       │
       ├── CLI arg "--project path/to/project.caffeine" → OpenProject()
       │
       └── no CLI arg → Show Project Manager UI (recent projects list)
              │
              ├── [New Project]  → CreateNewProject(config)
              ├── [Open Project] → OpenProject(file dialog)
              └── [Recent]       → OpenProject(recent path)
```

### Classes

```
ProjectConfig
├── Name              — display name
├── Version           — engine version (default "0.2.0")
├── RootPath          — absolute path to project root
├── AssetRawPath      — "assets/raw"
├── AssetProcessedPath— "assets/processed"
├── ScriptsPath       — "scripts"
├── TemplateType      — "2D", "3D", "Empty"
└── LastScene         — relative path to last opened scene

ProjectManager
├── CreateNewProject(config)    — creates dirs + project.caffeine
├── OpenProject(path)           — loads .caffeine file, validates
├── GetCurrentProject()         — returns active ProjectConfig
├── GetRecentProjects()         — returns recent paths list
└── SetRecentProjectsPath()     — override for testing
```

### Directory Structure Created

```
<RootPath>/
├── project.caffeine
├── assets/
│   ├── raw/
│   └── processed/
├── scripts/
└── build/
```

### JSON Serialization

Minimal hand-written parser (no external dependency). The `project.caffeine` format:

```json
{
  "project_name": "MyGame",
  "engine_version": "0.2.0",
  "paths": {
    "assets_raw": "assets/raw",
    "assets_processed": "assets/processed",
    "scripts": "scripts"
  },
  "last_scene": "scenes/main.scene"
}
```

Parser handles: quoted strings, nested objects (one level), comma/colon separators. Writer outputs with 2-space indentation.

### Recent Projects

- Stored as a text file, one absolute path per line
- Max 10 entries, most recent first
- Default location: platform config directory (`%APPDATA%/Caffeine/recent.txt` on Windows, `~/.config/caffeine/recent.txt` on Unix)

### Error Handling

- All operations return `bool` — `true` on success, `false` on failure
- No partial state: if directory creation fails mid-way, the caller handles cleanup
- No exceptions — follows engine convention

### Dependencies

- **Upstream:** `Caffeine::Core` (filesystem, string)
- **Downstream:** `Caffeine::Editor::EditorContext` (holds a ProjectManager instance)
