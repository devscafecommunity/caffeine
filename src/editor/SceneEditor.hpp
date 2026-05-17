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
#include "editor/SceneSerializer.hpp"
#include "editor/SceneTabManager.hpp"
#include "editor/ScriptEditorWindow.hpp"
#include "editor/ProjectManager.hpp"

#ifdef CF_HAS_IMGUI
#include "editor/MaterialEditorPanel.hpp"
#include "editor/AudioPreviewPanel.hpp"
#endif

#include "editor/AnimationTimeline.hpp"
#include "editor/TilemapEditor.hpp"
#include "editor/CommandPalette.hpp"
#include "editor/BuildDialog.hpp"
#include "editor/SettingsPanel.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>
#include <functional>

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
#endif

    void render(f32 deltaTime = 0.016f);

    // ── Serialization ──

    bool saveScene(const char* path, ECS::World& world);
    bool saveSceneAs(ECS::World& world);
    bool loadScene(const char* path, ECS::World& world);

    // ── Accessors ──

    EditorContext& context() { return m_ctx; }
    HierarchyPanel& hierarchy() { return m_hierarchy; }
    InspectorPanel& inspector() { return m_inspector; }
    SceneViewport&  viewport()  { return m_viewport; }
    AssetBrowser&   assetBrowser() { return m_assetBrowser; }
    SceneTabManager& tabManager() { return m_tabManager; }
    ConsoleWindow&  console() { return m_console; }
    ProfilerWindow& profiler() { return m_profiler; }
    ScriptEditorWindow& scriptEditor() { return m_scriptEditor; }
    AnimationTimelinePanel& animationTimeline() { return m_animationTimeline; }
    TilemapEditorPanel& tilemapEditor() { return m_tilemapEditor; }
    CommandPalette& commandPalette() { return m_commandPalette; }
#ifdef CF_HAS_IMGUI
    AudioPreviewPanel& audioPreview() { return m_audioPreview; }
#endif

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }
    void requestLayoutRebuild() { m_layoutNeedsRebuild = true; }


private:
#ifdef CF_HAS_IMGUI
    void setupDockspace(ImGuiID dockspaceId);
    void applyLayoutProfile(ImGuiID dockspaceId, const LayoutProfile& profile);
    void renderMainMenuBar(ECS::World& world);
    void renderStatusBar(ECS::World& world);
    void renderUnsavedChangesPopup(ECS::World& world);
    void executePendingAction(ECS::World& world);
    void handleAssetDrop(ECS::World& world);
    void handleShortcuts(ECS::World& world);
    void doNewScene();
#endif

    EditorContext  m_ctx;
    HierarchyPanel m_hierarchy;
    InspectorPanel m_inspector;
    SceneViewport  m_viewport;
    AssetBrowser    m_assetBrowser;
    SceneTabManager m_tabManager;
    ConsoleWindow   m_console;
    ProfilerWindow  m_profiler;
    ScriptEditorWindow m_scriptEditor;

#ifdef CF_HAS_IMGUI
    MaterialEditorPanel m_materialEditor;
    AudioPreviewPanel m_audioPreview;
#endif

    AnimationTimelinePanel m_animationTimeline;
    TilemapEditorPanel m_tilemapEditor;
    CommandPalette m_commandPalette;
    BuildDialog m_buildDialog;
    SettingsPanel m_settingsPanel;

#ifdef CF_HAS_SDL3
    Assets::AssetManager* m_assetManager = nullptr;
    ProjectConfig m_currentProjectConfig;
#endif

    void closeTab(int index);

    bool m_open         = true;
    bool m_dockingSetup = false;
    ImGuiID m_dockspaceId = 0;
    bool m_layoutNeedsRebuild = false;
    PendingAction m_pendingAction = PendingAction::None;
    int m_pendingCloseTab = -1;
};

} // namespace Caffeine::Editor
