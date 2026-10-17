# Audio Preview & Spatial Placement — Design Document

**Date:** 2026-05-16  
**Issue:** #121  
**Milestone:** M4 — Advanced Tools & Polish  
**Status:** Approved

---

## Overview

The Audio Preview & Spatial Placement system enables sound management and configuration directly in the Caffeine Studio editor. It has two main parts:

1. **Audio Preview** — preview audio assets in the editor with playback controls and waveform visualization
2. **Spatial Placement** — visual gizmos in the viewport showing AudioEmitter range/falloff, plus Inspector integration for editing audio source parameters

---

## Architecture

### Data Model

```
SceneEditor
├── AudioPreviewPanel m_audioPreview      (new)
│   ├── AudioSystem* m_previewSystem       (owned AudioSystem for preview)
│   ├── Assets::AudioClip* m_currentAsset  (loaded audio asset)
│   ├── AudioSource* m_previewSource       (active preview source)
│   ├── f32 m_volume, m_pitch
│   ├── bool m_looping
│   └── PeakData[] m_waveformCache         (pre-computed amplitude peaks)
├── SceneViewport
│   └── drawGizmo() ─── adds AudioSource circles
└── InspectorPanel
    └── drawAudioSource() ─── AudioEmitter field editor
```

### Component Breakdown

#### 1. AudioPreviewPanel (`src/editor/AudioPreviewPanel.hpp/.cpp`)

Standalone panel (no base class, following AnimationTimelinePanel/TilemapEditorPanel pattern).

**State:**
- Pointer to `Audio::AudioSystem*` (borrowed from editor, not owned)
- `Assets::AudioClip*` for the currently loaded asset
- `Audio::AudioSource*` for the active preview playback
- Volume (0.0–1.0), pitch (0.1–4.0), looping toggle
- Pre-computed waveform peak data buffer

**UI Layout (ImGui):**
```
┌─────────────────────────────────────────┐
│ Audio Preview          [Load] [Play] [■] │
├─────────────────────────────────────────┤
│ Waveform:                               │
│   ╱╲    ╱╲╱╲    ╱╲                       │
│  ╱  ╲  ╱    ╲  ╱  ╲                      │
│ ╱    ╲╱      ╲╱    ╲                     │
│ ─────────────────────────────────────    │
│ 0.0s                    duration          │
├─────────────────────────────────────────┤
│ Volume [━━━━━━━○──────] 0.8              │
│ Pitch  [━━━━○━━━━━━━━] 1.0               │
│ [x] Loop  [x] Auto-Play                  │
└─────────────────────────────────────────┘
```

**Playback:**
- Uses `Audio::AudioSystem::playSFX()` for preview (owned AudioSystem instance)
- Progress bar drives `AudioSource::seek()` on drag
- Stop calls `AudioSource::stop()`, releases the source

#### 2. AudioSource Gizmo (`SceneViewport::drawGizmo()` addition)

No new class — extends the existing `drawGizmo()` method in SceneViewport.

**When selected entity has `Audio::AudioEmitter`:**
- Convert emitter world position to screen coordinates (same as TransformGizmo)
- Draw inner circle (cyan) at `minDistance` radius — volume starts falling off here
- Draw outer circle (blue) at `maxDistance` radius — volume reaches zero here
- Draw a speaker icon or "A" label near the entity position
- Uses `ImDrawList::AddCircle` with world-to-screen conversion

**Spacing follows existing gizmo pattern:**
```
f32 worldToScreen = ctx.viewportZoom * 50.0f;
ImVec2 screenPos = worldToScreen(...);
dl->AddCircle(screenPos, minDist * worldToScreen, IM_COL32(0, 255, 255, 80), 32, 2.0f);
dl->AddCircle(screenPos, maxDist * worldToScreen, IM_COL32(0, 100, 255, 40), 32, 2.0f);
```

#### 3. Inspector Panel — Audio Source Editor

Implements the existing stub `InspectorPanel::drawAudioSource()` in `InspectorPanel.cpp`:

**Fields (when entity has `Audio::AudioEmitter`):**
- Clip path: text field with drag-drop target from AssetBrowser (filtered to audio files)
- Volume: slider 0.0–1.0 → `emitter.volume`
- Max Distance: float drag 0–2000 → `emitter.maxDistance`
- Loop: checkbox → `emitter.loop`
- Play On Spawn: checkbox → `emitter.playOnSpawn`
- Spatial: checkbox → `emitter.spatial`

**Pattern follows existing drawers:**
```cpp
if (!ImGui::CollapsingHeader("Audio Source")) return;
if (!world.has<Audio::AudioEmitter>(e)) {
    if (ImGui::Button("+ Add Audio Source")) {
        world.add<Audio::AudioEmitter>(e);
        ctx.isDirty = true;
    }
    return;
}
auto* emitter = world.get<Audio::AudioEmitter>(e);
// field editors with ctx.isDirty = true on change
```

#### 4. Audio::AudioEmitter — ECS Registration

`Audio::AudioEmitter` (already defined in `src/audio/AudioComponents.hpp`) will be registered as an ECS component. The struct already has the right fields:

```cpp
struct AudioEmitter {
    FixedString<128> clipPath;
    f32              volume      = 1.0f;
    f32              maxDistance = 500.0f;
    bool             loop        = false;
    bool             playOnSpawn = true;
    bool             spatial     = true;
};
```

#### 5. Asset Browser Integration

When an audio asset (`.wav`, `.ogg`) is dropped into the viewport:
- Create entity with `ECS::Position2D` and `Audio::AudioEmitter`
- Set `emitter.clipPath` to the dropped asset filename
- Follows existing Texture→Sprite drop pattern

---

## Integration Points

### SceneEditor
- Add `AudioPreviewPanel m_audioPreview` member (guarded by `#ifdef CF_HAS_IMGUI`)
- Call `m_audioPreview.onImGuiRender()` in the dockspace loop
- Initialize audio system in `SceneEditor::init()` if not already initialized

### CMakeLists.txt
- Add `src/editor/AudioPreviewPanel.cpp` to doppio target
- No new external dependencies (SDL_audio already linked)

### Tests
- AudioPreviewPanel has no test (depends on ImGui, same as MaterialEditorPanel)
- AudioEmitter ECS registration can be tested via existing test_editor patterns

---

## Dependencies

| Dependency | Type | Reason |
|---|---|---|
| `Audio::AudioSystem` | Internal | Preview playback and spatial audio |
| `Audio::AudioEmitter` | Internal | ECS component for audio sources |
| `Assets::AudioClip` | Internal | Asset type for audio loading |
| `AssetBrowser` | Internal | Drag-drop audio assets |
| `ImDrawList` | Internal | Viewport gizmo rendering |

---

## Acceptance Criteria

1. AudioPreviewPanel renders with play/stop/loop controls
2. Waveform visualization shows amplitude peaks for loaded audio
3. Playback starts/stops with correct volume and pitch
4. AudioSource gizmo circles appear in viewport for entities with AudioEmitter
5. Inspector shows Audio Source section with editable fields when entity has AudioEmitter
6. Dropping audio asset in viewport creates entity with AudioEmitter
7. Build compiles clean, existing tests continue to pass
