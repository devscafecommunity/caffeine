#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "scene/SceneSerializer.hpp"
#include "render/Camera2D.hpp"
#include "editor/EditorContext.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/SceneViewport.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/ConsoleWindow.hpp"
#include "editor/ProfilerWindow.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstdio>

namespace Caffeine::Editor {
using namespace Caffeine;

class SceneEditor {
public:
    SceneEditor() : m_hierarchy(&m_ctx) {}

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
              const char* assetsPath = "assets");
    void shutdown();
#endif

    void render(ECS::World& world
#ifdef CF_HAS_SDL3
                , Render::Camera2D& editorCamera
#endif
               );

    // ── Serialization ──

    bool saveScene(const char* path, ECS::World& world);
    bool loadScene(const char* path, ECS::World& world);

    // ── Accessors ──

    EditorContext& context() { return m_ctx; }
    HierarchyPanel& hierarchy() { return m_hierarchy; }
    InspectorPanel& inspector() { return m_inspector; }
    SceneViewport&  viewport()  { return m_viewport; }
    AssetBrowser&   assetBrowser() { return m_assetBrowser; }
    ConsoleWindow&  console() { return m_console; }
    ProfilerWindow& profiler() { return m_profiler; }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void setupDockspace(ImGuiID dockspaceId);
    void renderMainMenuBar(ECS::World& world);
    void renderStatusBar(ECS::World& world);
    void handleAssetDrop(ECS::World& world);
    void handleShortcuts(ECS::World& world);
#endif

    EditorContext  m_ctx;
    HierarchyPanel m_hierarchy;
    InspectorPanel m_inspector;
    SceneViewport  m_viewport;
    AssetBrowser   m_assetBrowser;
    ConsoleWindow  m_console;
    ProfilerWindow m_profiler;

#ifdef CF_HAS_SDL3
    Assets::AssetManager* m_assetManager = nullptr;
#endif

    bool m_open         = true;
    bool m_dockingSetup = false;
};

} // namespace Caffeine::Editor
