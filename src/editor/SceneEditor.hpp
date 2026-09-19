#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/SceneViewport.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/ConsoleWindow.hpp"
#include "editor/ProfilerWindow.hpp"
#include "editor/EntityDebugger.hpp"
#include "editor/PluginSystem.hpp"
#include "editor/SceneSerializer.hpp"
#include "editor/SceneTabManager.hpp"
#include "editor/ScriptEditorWindow.hpp"
#include "editor/ProjectManager.hpp"

#ifdef CF_HAS_IMGUI
#include "editor/MaterialEditorPanel.hpp"
#include "editor/AudioPreviewPanel.hpp"
#include "editor/CameraPreviewPanel.hpp"
#include "editor/TerrainEditorPanel.hpp"
#include "editor/EntityPresetsPanel.hpp"
#endif

#include "editor/AnimationTimeline.hpp"
#include "editor/AnimatorController.hpp"
#include "editor/TilemapEditor.hpp"
#include "editor/CommandPalette.hpp"
#include "editor/BuildDialog.hpp"
#include "editor/SettingsPanel.hpp"

#include "core/io/FileWatcher.hpp"

#include "physics/PhysicsSystem2D.hpp"
#include "ui/UISystem.hpp"
#include "events/EventBus.hpp"
#include "animation/AnimationSystem.hpp"
#include "render/Camera2D.hpp"
#include "input/InputManager.hpp"

#ifdef CF_HAS_SCRIPTING
#include "script/ScriptEngine.hpp"
#include "script/ScriptSystem.hpp"
#endif

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>
#include <functional>
#include <vector>

namespace Caffeine::Editor {

class SceneEditor {
public:
    enum class PendingAction : u8 {
        None,
        NewScene,
        OpenScene,
        Exit
    };

    SceneEditor() : m_hierarchy(&m_ctx) {}

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
              const ProjectConfig& projectConfig);
    void shutdown();
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
#endif

    void render(f32 deltaTime = 0.016f);

    // ── Serialization ──

    bool saveScene(const char* path, ECS::World& world);
    bool saveSceneAs(ECS::World& world);
    bool loadScene(const char* path, ECS::World& world);
    std::string prepareSceneForBuild();

    // ── Accessors ──

    EditorContext& context() { return m_ctx; }
    HierarchyPanel& hierarchy() { return m_hierarchy; }
    InspectorPanel& inspector() { return m_inspector; }
    SceneViewport&  viewport()  { return m_viewport; }
    AssetBrowser&   assetBrowser() { return m_assetBrowser; }
    SceneTabManager& tabManager() { return m_tabManager; }
    ConsoleWindow&  console() { return m_console; }
    ProfilerWindow& profiler() { return m_profiler; }
    EntityDebuggerPanel& entityDebugger() { return m_entityDebugger; }
    ScriptEditorWindow& scriptEditor() { return m_scriptEditor; }
    AnimationTimelinePanel& animationTimeline() { return m_animationTimeline; }
    AnimatorControllerWindow& animatorController() { return m_animatorController; }
    TilemapEditorPanel& tilemapEditor() { return m_tilemapEditor; }
    CommandPalette& commandPalette() { return m_commandPalette; }
#ifdef CF_HAS_IMGUI
    AudioPreviewPanel& audioPreview() { return m_audioPreview; }
#endif

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }
    void requestLayoutRebuild() { m_layoutNeedsRebuild = true; }

    // Called when the OS window close button is pressed.
    void onQuitRequested();
    bool quitConfirmed() const { return m_quitConfirmed; }


private:
#ifdef CF_HAS_IMGUI
    void setupDockspace(ImGuiID dockspaceId);
    void applyLayoutProfile(ImGuiID dockspaceId, const LayoutProfile& profile);
    void syncLayoutProfileFromPanels();
    void renderMainMenuBar(ECS::World& world);
    void renderStatusBar(ECS::World& world);
    void renderUnsavedChangesPopup(ECS::World* world);
    void executePendingAction(ECS::World* world);
    void dismissBlockingPopups();
    bool hasUnsavedChanges() const;
    void handleAssetDrop(ECS::World& world);
    void handleShortcuts(ECS::World& world);
    void doNewScene();

    void enterPlayMode(ECS::World& world);
    void exitPlayMode(ECS::World& world);
    void tickSystems(ECS::World& world, f32 dt);
    void renderPlaybar(ECS::World& world);
    void registerPlayModeEventListeners();
    void handleCollision2D(ECS::World& world, const Events::OnCollision2D& event);
#endif

    EditorContext  m_ctx;
    HierarchyPanel m_hierarchy;
    InspectorPanel m_inspector;
    SceneViewport  m_viewport;
    AssetBrowser    m_assetBrowser;
    SceneTabManager m_tabManager;
    ConsoleWindow   m_console;
    ProfilerWindow  m_profiler;
    EntityDebuggerPanel m_entityDebugger;
    ScriptEditorWindow m_scriptEditor;

#ifdef CF_HAS_IMGUI
    MaterialEditorPanel m_materialEditor;
    AudioPreviewPanel   m_audioPreview;
    CameraPreviewPanel  m_cameraPreview;
    TerrainEditorPanel  m_terrainEditor;
    EntityPresetsPanel  m_entityPresets;
#endif

    AnimationTimelinePanel m_animationTimeline;
    AnimatorControllerWindow m_animatorController;
    TilemapEditorPanel m_tilemapEditor;
    CommandPalette m_commandPalette;
    BuildDialog m_buildDialog;
    SettingsPanel m_settingsPanel;

#ifdef CF_HAS_SDL3
    Assets::AssetManager* m_assetManager = nullptr;
    ProjectConfig m_currentProjectConfig;
    RHI::RenderDevice* m_renderDevice = nullptr;
    RHI::CommandBuffer* m_frameCmd = nullptr;
#endif

    void closeTab(int index);

    bool m_open         = true;
    bool m_quitConfirmed = false;
    bool m_showQuitPopup = false;
    bool m_dockingSetup = false;
    ImGuiID m_dockspaceId = 0;
    bool m_layoutNeedsRebuild = false;
    PendingAction m_pendingAction = PendingAction::None;
    int m_pendingCloseTab = -1;

    // ── Play Mode ──────────────────────────────────────────────
    bool m_isPlaying  = false;
    bool m_isPaused   = false;

    Events::EventBus m_eventBus;
    Physics2D::PhysicsSystem2D m_physicsSystem{&m_eventBus};
    UI::UISystem m_uiSystem{&m_eventBus};
    Animation::AnimationSystem m_animationSystem;
    Render::Camera2D m_playCamera2D;
    Events::ListenerHandle m_collisionListener = 0;
    bool m_playListenersRegistered = false;

    struct ViewportPlaySnapshot {
        f32 panX = 0.0f;
        f32 panY = 0.0f;
        f32 zoom = 1.0f;
    };
    ViewportPlaySnapshot m_viewportPlaySnapshot{};

#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine m_scriptEngine;
    Script::ScriptSystem m_scriptSystem{nullptr};
    bool m_scriptEngineReady = false;
#endif

    struct EntitySnapshot {
        u32 id;
        bool hasTransform = false;
        bool hasPos3 = false;
        float px = 0, py = 0, pz = 0;
        float rz = 0;
    };
    std::vector<EntitySnapshot> m_playSnapshot;
    Input::InputManager m_input;

    IO::FileWatcher m_scriptFileWatcher;
    bool m_scriptWatcherStarted = false;
    std::string m_pendingStartupScene;
};

} // namespace Caffeine::Editor
