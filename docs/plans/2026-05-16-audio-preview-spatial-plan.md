# Audio Preview & Spatial Placement Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add audio asset preview (playback + waveform) and spatial audio gizmos/editor to the Caffeine Studio IDE.

**Architecture:** Standalone AudioPreviewPanel with its own AudioSystem for preview playback; AudioSourceGizmo drawn via SceneViewport::drawGizmo(); Inspector stub implements AudioEmitter field editor. Follows existing panel patterns (AnimationTimelinePanel, TilemapEditorPanel).

**Tech Stack:** C++20, ImGui (ImDrawList), SDL3 audio (SDL_AudioStream), internal ECS

---

### Task 1: Register Audio::AudioEmitter as ECS Component

**Files:**
- Modify: `src/editor/SceneSerializer.cpp` — serialize AudioEmitter clipPath, volume, maxDistance, loop, playOnSpawn, spatial
- Modify: `src/editor/InspectorPanel.cpp` — already includes AudioComponents.hpp? check; the stub already compiles

**Context:** `Audio::AudioEmitter` is defined in `src/audio/AudioComponents.hpp` in namespace `Caffeine::Audio`. The ECS uses `world.add<ComponentType>()` — any struct works as an ECS component automatically via ComponentID system. No explicit registration needed. However, the SceneSerializer needs to handle AudioEmitter fields.

**Step 1: Verify AudioEmitter compiles as ECS component**

The ECS system uses `ComponentID::get<T>()` which works for any type. Since `Audio::AudioEmitter` is just a struct with trivially-copyable fields (FixedString is not trivially copyable though — it has a char array), it should work with `world.add<Audio::AudioEmitter>()` already. No code change needed for basic ECS registration.

**Step 2: Add AudioEmitter serialization to SceneSerializer**

Find the SceneSerializer serialization code and add AudioEmitter field serialization. First check how the serializer handles components.

Run: `grep -n "case ComponentID" src/editor/SceneSerializer.cpp` to see existing pattern.

Add serialization for `Audio::AudioEmitter`:
- `clipPath` → string
- `volume` → f32
- `maxDistance` → f32
- `loop` → bool
- `playOnSpawn` → bool
- `spatial` → bool

**Step 3: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -5`
Expected: clean build

Run: `cmake --build build --target CaffeineTest 2>&1 | tail -5`
Expected: clean build

**Step 4: Commit**

```bash
git add src/editor/SceneSerializer.cpp
git commit -m "feat(audio): register AudioEmitter as ECS component with serialization"
```

---

### Task 2: Implement drawAudioSource in InspectorPanel

**Files:**
- Modify: `src/editor/InspectorPanel.cpp` — implement the stub at line 184

**Step 1: Read InspectorPanel.cpp around line 180-186**

The current stub is:
```cpp
void InspectorPanel::drawAudioSource(ECS::World&, ECS::Entity, EditorContext&) {}
```

Check if `AudioComponents.hpp` is already included (it's not — need to add include).

**Step 2: Add include**

Add `#include "audio/AudioComponents.hpp"` at the top of InspectorPanel.cpp (inside the `#ifdef CF_HAS_IMGUI` guard section).

**Step 3: Implement drawAudioSource**

Pattern (following drawSprite):
```cpp
void InspectorPanel::drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx) {
    if (!world.has<Audio::AudioEmitter>(e)) {
        if (ImGui::CollapsingHeader("Audio Source")) {
            if (ImGui::Button("+ Add Audio Source")) {
                world.add<Audio::AudioEmitter>(e);
                ctx.isDirty = true;
            }
        }
        return;
    }
    if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* emitter = world.get<Audio::AudioEmitter>(e);
        
        // Clip path (with drag-drop from AssetBrowser)
        char buf[128];
        strncpy(buf, emitter->clipPath.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Clip", buf, sizeof(buf))) {
            emitter->clipPath = buf;
            ctx.isDirty = true;
        }
        // Drag-drop target for audio assets
        if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
            if (asset->type == AssetType::Audio) {
                std::filesystem::path p(asset->path);
                emitter->clipPath = p.filename().string().c_str();
                ctx.isDirty = true;
            }
        }
        
        // Volume
        ImGui::SliderFloat("Volume", &emitter->volume, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        
        // Max Distance
        ImGui::DragFloat("Max Distance", &emitter->maxDistance, 1.0f, 0.0f, 2000.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        
        // Toggles
        ImGui::Checkbox("Loop", &emitter->loop);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        ImGui::Checkbox("Play on Spawn", &emitter->playOnSpawn);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
        ImGui::Checkbox("Spatial", &emitter->spatial);
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.isDirty = true;
    }
}
```

Also add clipPath getter — `FixedString<128>` uses `c_str()` method.

**Step 4: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build

**Step 5: Commit**

```bash
git add src/editor/InspectorPanel.cpp
git commit -m "feat(editor): implement AudioEmitter inspector panel"
```

---

### Task 3: Create AudioPreviewPanel

**Files:**
- Create: `src/editor/AudioPreviewPanel.hpp`
- Create: `src/editor/AudioPreviewPanel.cpp`

**Step 1: Create AudioPreviewPanel.hpp**

```cpp
#pragma once
#include "core/Types.hpp"
#include "audio/AudioSystem.hpp"
#include "assets/AssetTypes.hpp"
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

using namespace Caffeine;

class AudioPreviewPanel {
public:
    AudioPreviewPanel();
    ~AudioPreviewPanel();

    bool init();
    void shutdown();

    void onImGuiRender();

    void loadAsset(Assets::AudioClip* clip, const char* name);
    void play();
    void stop();

    bool isOpen() const { return m_open; }
    void open()  { m_open = true; }
    void close() { m_open = false; }

    Audio::AudioSystem* previewSystem() { return m_previewSystem; }

private:
    void renderPlaybackControls();
    void renderWaveform();
    void renderSettings();

    void computeWaveformPeaks(const Assets::AudioClip* clip);

    Audio::AudioSystem* m_previewSystem = nullptr;
    Audio::AudioSource* m_previewSource = nullptr;
    Assets::AudioClip*  m_currentAsset  = nullptr;
    char m_assetName[256] = {};

    float m_volume = 1.0f;
    float m_pitch  = 1.0f;
    bool  m_looping = false;

    // Waveform peak data (pre-computed from PCM data)
    struct Peak { float min; float max; };
    std::vector<Peak> m_peaks;
    bool m_peaksDirty = false;

    // Playback progress tracking
    float m_progress = 0.0f;

    bool m_open = true;
    bool m_initialized = false;
};

} // namespace Caffeine::Editor
```

**Step 2: Create AudioPreviewPanel.cpp**

```cpp
#include "editor/AudioPreviewPanel.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

AudioPreviewPanel::AudioPreviewPanel() {}

AudioPreviewPanel::~AudioPreviewPanel() { shutdown(); }

bool AudioPreviewPanel::init() {
    if (m_initialized) return true;
    m_previewSystem = new Audio::AudioSystem();
    if (!m_previewSystem->init(8, 1)) {
        delete m_previewSystem;
        m_previewSystem = nullptr;
        return false;
    }
    m_initialized = true;
    return true;
}

void AudioPreviewPanel::shutdown() {
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    if (m_previewSystem) {
        m_previewSystem->shutdown();
        delete m_previewSystem;
        m_previewSystem = nullptr;
    }
    m_currentAsset = nullptr;
    m_initialized = false;
}

void AudioPreviewPanel::loadAsset(Assets::AudioClip* clip, const char* name) {
    m_currentAsset = clip;
    if (name) {
        strncpy(m_assetName, name, sizeof(m_assetName) - 1);
        m_assetName[sizeof(m_assetName) - 1] = '\0';
    }
    m_peaksDirty = true;
    m_progress = 0.0f;
    // Stop any current preview
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
}

void AudioPreviewPanel::play() {
    if (!m_initialized || !m_currentAsset) return;
    // Stop previous
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    // Convert Assets::AudioClip → Audio::AudioClip for the audio system
    // We need to register the PCM data with the AudioSystem
    Audio::AudioClip* audioClip = m_previewSystem->registerClip(
        m_currentAsset->pcmData,
        m_currentAsset->pcmDataSize,
        m_currentAsset->sampleRate,
        m_currentAsset->channels,
        m_currentAsset->bitsPerSample,
        // Duration = sampleCount / sampleRate
        static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)
    );
    if (!audioClip) return;

    m_previewSource = m_previewSystem->playSFX(audioClip, m_volume, 0.0f);
    if (m_previewSource) {
        m_previewSource->setLoop(m_looping);
    }
}

void AudioPreviewPanel::stop() {
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    m_progress = 0.0f;
}

void AudioPreviewPanel::computeWaveformPeaks(const Assets::AudioClip* clip) {
    if (!clip || !clip->pcmData || clip->pcmDataSize == 0) return;
    
    const int kNumPeaks = 200; // Number of samples for waveform display
    m_peaks.resize(kNumPeaks);
    
    // For 16-bit PCM, each sample is i16 (2 bytes)
    u32 bytesPerSample = (clip->bitsPerSample / 8);
    if (bytesPerSample == 0) return;
    u32 totalSamples = static_cast<u32>(clip->pcmDataSize / bytesPerSample);
    if (totalSamples == 0) return;
    
    u32 samplesPerPeak = std::max(1u, totalSamples / kNumPeaks);
    
    for (int i = 0; i < kNumPeaks; ++i) {
        float minVal = 0.0f, maxVal = 0.0f;
        u32 startSample = i * samplesPerPeak;
        u32 endSample = std::min(startSample + samplesPerPeak, totalSamples);
        
        for (u32 s = startSample; s < endSample; ++s) {
            u64 offset = static_cast<u64>(s) * bytesPerSample;
            if (offset + bytesPerSample > clip->pcmDataSize) break;
            
            float sample;
            if (clip->bitsPerSample == 16) {
                sample = *reinterpret_cast<const i16*>(clip->pcmData + offset) / 32768.0f;
            } else if (clip->bitsPerSample == 8) {
                sample = (clip->pcmData[offset] - 128) / 128.0f;
            } else {
                sample = 0.0f;
            }
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }
        m_peaks[i] = {minVal, maxVal};
    }
}

void AudioPreviewPanel::onImGuiRender() {
    if (!m_open) return;
    
    ImGui::Begin("Audio Preview", &m_open);
    
    // Update progress
    if (m_previewSource && m_previewSource->isPlaying()) {
        m_progress = m_previewSource->currentTime();
    } else if (m_previewSource && !m_previewSource->isPlaying() && !m_previewSource->isPaused()) {
        // Playback finished
        if (m_looping) {
            play();
        } else {
            m_previewSource = nullptr;
            m_progress = 0.0f;
        }
    }
    
    renderPlaybackControls();
    renderWaveform();
    renderSettings();
    
    ImGui::End();
}

void AudioPreviewPanel::renderPlaybackControls() {
    // Asset name and transport controls
    char title[320];
    snprintf(title, sizeof(title), "%s", m_assetName[0] ? m_assetName : "No asset loaded");
    ImGui::TextUnformatted(title);
    
    ImGui::SameLine();
    
    if (ImGui::Button(m_previewSource && m_previewSource->isPlaying() ? "||" : "|>")) {
        if (m_previewSource && m_previewSource->isPlaying()) {
            m_previewSource->pause();
        } else {
            play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("[]")) {
        stop();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_looping);
    if (m_previewSource) {
        m_previewSource->setLoop(m_looping);
    }
    
    // Progress slider
    float duration = m_currentAsset ? 
        static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate) : 0.0f;
    
    if (duration > 0.0f) {
        char timeBuf[64];
        snprintf(timeBuf, sizeof(timeBuf), "%.1f / %.1f", m_progress, duration);
        ImGui::SameLine();
        ImGui::TextUnformatted(timeBuf);
        
        float prog = duration > 0.0f ? m_progress / duration : 0.0f;
        if (ImGui::SliderFloat("##progress", &prog, 0.0f, 1.0f, "")) {
            m_progress = prog * duration;
            if (m_previewSource) {
                m_previewSource->seek(m_progress);
            }
        }
    }
}

void AudioPreviewPanel::renderWaveform() {
    if (!m_currentAsset || !m_currentAsset->pcmData) {
        ImGui::TextUnformatted("No audio data");
        return;
    }
    
    // Compute peaks if needed
    if (m_peaksDirty) {
        computeWaveformPeaks(m_currentAsset);
        m_peaksDirty = false;
    }
    
    if (m_peaks.empty()) return;
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    float canvasWidth = ImGui::GetContentRegionAvail().x;
    float canvasHeight = 80.0f;
    
    // Draw background
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight),
                      IM_COL32(20, 20, 30, 255));
    
    // Draw center line
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    dl->AddLine(ImVec2(canvasPos.x, centerY), ImVec2(canvasPos.x + canvasWidth, centerY),
                IM_COL32(60, 60, 80, 255));
    
    // Draw waveform peaks
    float barWidth = canvasWidth / static_cast<float>(m_peaks.size());
    for (size_t i = 0; i < m_peaks.size(); ++i) {
        float x = canvasPos.x + static_cast<float>(i) * barWidth;
        float minY = centerY - m_peaks[i].max * (canvasHeight * 0.4f);
        float maxY = centerY - m_peaks[i].min * (canvasHeight * 0.4f);
        
        // Bar color: cyan for played portion, gray for unplayed
        float progressRatio = m_currentAsset ? m_progress / 
            (static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)) : 0.0f;
        float t = static_cast<float>(i) / static_cast<float>(m_peaks.size());
        u32 color = (t <= progressRatio) ? IM_COL32(100, 200, 255, 200) : IM_COL32(100, 120, 140, 120);
        
        dl->AddLine(ImVec2(x, minY), ImVec2(x, maxY), color, std::max(1.0f, barWidth * 0.8f));
    }
    
    // Draw progress line
    float progX = canvasPos.x + canvasWidth * (m_currentAsset ? m_progress / 
        (static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)) : 0.0f);
    dl->AddLine(ImVec2(progX, canvasPos.y), ImVec2(progX, canvasPos.y + canvasHeight),
                IM_COL32(255, 255, 100, 200), 1.0f);
    
    ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
}

void AudioPreviewPanel::renderSettings() {
    ImGui::Separator();
    ImGui::TextUnformatted("Settings");
    
    if (ImGui::SliderFloat("Volume", &m_volume, 0.0f, 1.0f, "%.2f")) {
        if (m_previewSource) {
            m_previewSource->setVolume(m_volume);
        }
    }
    
    if (ImGui::SliderFloat("Pitch", &m_pitch, 0.1f, 4.0f, "%.1f")) {
        if (m_previewSource) {
            m_previewSource->setPitch(m_pitch);
        }
    }
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
```

**Step 3: Verify build compiles**

The AudioPreviewPanel uses `Audio::AudioSystem`, `Audio::AudioSource`, `Assets::AudioClip` — all already available since caffeine-core provides them and doppio links it.

**Step 4: Commit**

```bash
git add src/editor/AudioPreviewPanel.hpp src/editor/AudioPreviewPanel.cpp
git commit -m "feat(editor): add AudioPreviewPanel with waveform and playback controls"
```

---

### Task 4: Add AudioSource Gizmo to SceneViewport

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — extend drawGizmo
- Modify: `src/editor/SceneViewport.hpp` — no change needed (already includes Components.hpp)

**Step 1: Add AudioComponents.hpp include**

Add `#include "audio/AudioComponents.hpp"` to SceneViewport.cpp.

**Step 2: Extend drawGizmo**

After the existing TransformGizmo code (or in a separate section), add AudioEmitter gizmo rendering:

Find: `void SceneViewport::drawGizmo(...)` around line 151.

After the existing gizmo switch block (line 210), add:

```cpp
// ── AudioEmitter gizmo ──
if (world.has<Audio::AudioEmitter>(ctx.selectedEntity)) {
    auto* emitter = world.get<Audio::AudioEmitter>(ctx.selectedEntity);
    if (emitter->spatial) {
        f32 worldToScreen = ctx.viewportZoom * 50.0f;
        
        // minDistance circle (cyan, inner)
        float minRad = emitter->maxDistance * 0.5f; // heuristic: minDist = maxDist/2 if not explicit
        // If AudioEmitter had minDistance field, use it. For now use maxDistance * 0.5f
        dl->AddCircle(screenPos, minRad * worldToScreen, IM_COL32(0, 255, 255, 80), 32, 2.0f);
        // Add label
        char distBuf[64];
        snprintf(distBuf, sizeof(distBuf), "min %.0f", minRad);
        dl->AddText(ImVec2(screenPos.x + minRad * worldToScreen, screenPos.y - 8),
                    IM_COL32(0, 255, 255, 180), distBuf);
        
        // maxDistance circle (blue, outer)
        dl->AddCircle(screenPos, emitter->maxDistance * worldToScreen, IM_COL32(50, 130, 255, 60), 32, 2.0f);
        snprintf(distBuf, sizeof(distBuf), "max %.0f", emitter->maxDistance);
        dl->AddText(ImVec2(screenPos.x + emitter->maxDistance * worldToScreen, screenPos.y - 8),
                    IM_COL32(50, 130, 255, 180), distBuf);
        
        // Speaker icon (simple "A" label)
        dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 20),
                    IM_COL32(180, 180, 255, 220), "♪");
    }
}
```

**Step 3: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build

**Step 4: Commit**

```bash
git add src/editor/SceneViewport.cpp
git commit -m "feat(editor): add AudioEmitter spatial gizmo to viewport"
```

---

### Task 5: Integrate AudioPreviewPanel with SceneEditor

**Files:**
- Modify: `src/editor/SceneEditor.hpp` — add member
- Modify: `src/editor/SceneEditor.cpp` — init, render, shutdown
- Modify: `src/editor/SceneSerializer.cpp` — add AudioEmitter serialization

**Step 1: Add member to SceneEditor.hpp**

```cpp
// Near other panel includes:
#include "editor/AudioPreviewPanel.hpp"

// In class SceneEditor members (guarded):
AudioPreviewPanel m_audioPreview;
```

Also add accessor if desired:
```cpp
AudioPreviewPanel& audioPreview() { return m_audioPreview; }
```

**Step 2: Init in SceneEditor::init()**

In `SceneEditor::init()`:
```cpp
if (!m_audioPreview.init()) {
    // Audio preview init failed — non-fatal, just log
    std::fprintf(stderr, "Warning: AudioPreviewPanel init failed\n");
}
```

**Step 3: Render in SceneEditor::render()**

In the main render function, after other panels:
```cpp
m_audioPreview.onImGuiRender();
```

**Step 4: Shutdown in SceneEditor::shutdown()**

```cpp
m_audioPreview.shutdown();
```

**Step 5: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build

**Step 6: Commit**

```bash
git add src/editor/SceneEditor.hpp src/editor/SceneEditor.cpp
git commit -m "feat(editor): integrate AudioPreviewPanel into SceneEditor"
```

---

### Task 6: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt` — add AudioPreviewPanel.cpp to doppio target

**Step 1: Add source file**

Find the doppio target sources section and add:
```
src/editor/AudioPreviewPanel.cpp
```

**Step 2: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add AudioPreviewPanel to doppio target"
```

---

### Task 7: Asset Drop Integration for Audio

**Files:**
- Modify: `src/editor/SceneViewport.cpp` — handle Audio asset drops

**Step 1: Extend asset drop handler**

In `SceneViewport::render()`, find the existing asset drop handler (around line 70-93). Add after the Texture→Sprite block:

```cpp
if (asset->type == AssetType::Audio) {
    auto& emitter = world.add<Audio::AudioEmitter>(entity);
    emitter.clipPath = assetPath.filename().string().c_str();
}
```

**Step 2: Verify build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build

**Step 3: Commit**

```bash
git add src/editor/SceneViewport.cpp
git commit -m "feat(editor): handle audio asset drops in viewport"
```

---

### Task 8: Final Verification

**Step 1: Full build**

Run: `cmake --build build --target doppio 2>&1 | tail -10`
Expected: clean build
Run: `cmake --build build --target CaffeineTest 2>&1 | tail -10`
Expected: clean build

**Step 2: Run all tests**

Run: `./build/tests/CaffeineTest`
Expected: ALL TESTS PASSED

**Step 3: Push**

```bash
git push origin 121-44-audio-preview-spatial-placement
```
