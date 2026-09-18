#include "editor/SceneEditor.hpp"
#include "assets/MeshCache.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "terrain/TerrainCache.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "editor/ComponentRegistry.hpp"
#include "editor/EditorIcons.hpp"
#include "debug/Profiler.hpp"
#include "editor/PluginSystem.hpp"
#include "editor/EditorPaths.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/PlayMode2D.hpp"
#include "script/ScriptTypes.hpp"
#include "events/Events.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui_internal.h>

namespace Caffeine::Editor {

// ── Init / Shutdown ─────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                       const ProjectConfig& projectConfig) {
    m_renderDevice = device;
    if (!m_viewport.init(device)) return false;
    m_materialEditor.initGpu(device);
    m_assetBrowser.init(projectConfig);
    m_assetBrowser.setOnScriptOpen([this](const std::filesystem::path& path) {
        m_scriptEditor.open();
        m_scriptEditor.openFile(path);
    });
    m_assetBrowser.setOnMaterialOpen([this](const std::filesystem::path& path) {
        m_materialEditor.open();
        m_materialEditor.openFromPath(path);
    });
    m_assetManager = assetManager;
    m_currentProjectConfig = projectConfig;
    m_currentProjectConfig.RootPath =
        ProjectManager::ResolveEditorProjectRoot(projectConfig.RootPath);
    m_tabManager.newScene("Untitled");

    m_commandPalette.registerCommand("panel_hierarchy", "Hierarchy Panel", "Panels", [this]() {
        m_hierarchy.open();
    });
    m_commandPalette.registerCommand("panel_inspector", "Inspector Panel", "Panels", [this]() {
        m_inspector.open();
    });
    m_commandPalette.registerCommand("panel_console", "Console", "Panels", [this]() {
        m_console.open();
    });
    m_commandPalette.registerCommand("panel_profiler", "Profiler", "Panels", [this]() {
        m_profiler.open();
    });
    m_commandPalette.registerCommand("panel_entity_debugger", "Entity Debugger", "Panels", [this]() {
        m_entityDebugger.open();
    });
    m_commandPalette.registerCommand("panel_plugin_manager", "Plugin Manager", "Panels", [this]() {
        PluginManager::instance().openManager();
    });
    m_commandPalette.registerCommand("panel_asset_browser", "Asset Browser", "Panels", [this]() {
        m_assetBrowser.open();
    });
    m_commandPalette.registerCommand("panel_animation_timeline", "Animation Timeline", "Panels", [this]() {
        m_animationTimeline.open();
    });
    m_commandPalette.registerCommand("panel_animator_controller", "Animator Controller", "Panels", [this]() {
        m_animatorController.open();
    });
    m_commandPalette.registerCommand("panel_tilemap", "Tilemap Editor", "Panels", [this]() {
        m_tilemapEditor.open();
    });
     m_commandPalette.registerCommand("panel_script_editor", "Script Editor", "Panels", [this]() {
         m_scriptEditor.open();
     });
     m_commandPalette.registerCommand("panel_material_editor", "Material Editor", "Panels", [this]() {
         m_materialEditor.open();
     });
     m_commandPalette.registerCommand("panel_terrain_editor", "Terrain Editor", "Panels", [this]() {
         m_terrainEditor.open();
     });
     m_commandPalette.registerCommand("panel_settings", "Settings", "Panels", [this]() {
         m_settingsPanel.open();
     });
    m_commandPalette.registerCommand("panel_viewport", "Scene Viewport", "Panels", [this]() {
    });

    m_commandPalette.registerCommand("action_new_scene", "New Scene", "Actions", [this]() {
        doNewScene();
    });
    m_commandPalette.registerCommand("action_save_scene", "Save Scene", "Actions", [this]() {
        if (auto* world = m_tabManager.activeWorld()) {
            saveSceneAs(*world);
        }
    });
    m_commandPalette.registerCommand("action_load_tileset", "Load Tileset", "Actions", [this]() {
        m_tilemapEditor.open();
    });

     m_audioPreview.init();

    m_buildDialog.setPrepareBuildCallback([this]() { return prepareSceneForBuild(); });
    m_buildDialog.setProjectContext(projectConfig);

    const std::filesystem::path pluginsDir =
        projectConfig.RootPath.empty()
            ? (std::filesystem::current_path() / "plugins")
            : (projectConfig.RootPath / "plugins");
    std::filesystem::path bundledPluginsDir;
    if (EditorPaths::isReady()) {
        bundledPluginsDir = EditorPaths::root().parent_path() / "plugins";
    }
    PluginManager::instance().initialize(pluginsDir, &m_inspector, &m_commandPalette, &m_ctx,
                                         bundledPluginsDir);

     m_inspector.open();
     m_assetBrowser.open();

     // Register layout change callback
     m_settingsPanel.setLayoutChangeCallback([this]() {
         requestLayoutRebuild();
     });
     m_settingsPanel.setEditorContext(&m_ctx);
     m_settingsPanel.applyPreferencesToContext(m_ctx);

    // Auto-load last scene if project config has one
    if (m_settingsPanel.preferences().reopenLastSceneOnStartup && !projectConfig.LastScene.empty()) {
        std::string lastScene = projectConfig.LastScene;
        if (lastScene.find("/build/") != std::string::npos ||
            lastScene.find("\\build\\") != std::string::npos ||
            lastScene.rfind("build/", 0) == 0) {
            lastScene = "scenes/main.caf";
        }
        std::filesystem::path scenePath = projectConfig.RootPath / lastScene;
        if (!std::filesystem::exists(scenePath) && lastScene != "scenes/main.caf") {
            scenePath = projectConfig.RootPath / "scenes" / "main.caf";
        }
        if (std::filesystem::exists(scenePath)) {
            if (auto* world = m_tabManager.activeWorld()) {
                loadScene(scenePath.string().c_str(), *world);
                m_tabManager.activeTab().name = scenePath.stem().string();
                m_tabManager.activeTab().path = scenePath.string();
            }
        }
    }

#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine::InitParams scriptParams;
    scriptParams.world  = nullptr;
    scriptParams.events = &m_eventBus;
    if (!m_scriptEngineReady) {
        m_scriptEngineReady = m_scriptEngine.init(scriptParams);
    }
     m_ctx.scriptEngine = &m_scriptEngine;
     m_scriptEditor.setScriptEngine(&m_scriptEngine);
#endif

    registerAllComponents(ComponentRegistry::instance());
    registerPlayModeEventListeners();

    return true;
}

void SceneEditor::shutdown() {
    PluginManager::instance().shutdown();
#ifdef CF_HAS_SDL3
    if (m_renderDevice) {
        m_materialEditor.shutdownGpu();
        Assets::MeshCache::getInstance().releaseGpuResources(m_renderDevice);
        Render::GpuProceduralMeshes::releaseGpuResources(m_renderDevice);
        Terrain::TerrainCache::instance().releaseGpuResources(m_renderDevice);
    }
#endif
    Terrain::TerrainCache::instance().clear();
    m_tabManager.clearAll();
    m_viewport.shutdown();
    m_audioPreview.shutdown();
    m_scriptFileWatcher.stop();
    m_scriptWatcherStarted = false;
}

bool SceneEditor::hasUnsavedChanges() const {
    if (m_ctx.isDirty) {
        return true;
    }
    for (int i = 0; i < m_tabManager.tabCount(); ++i) {
        if (i != m_tabManager.activeTabIndex() && m_tabManager.tab(i).isDirty) {
            return true;
        }
    }
    return false;
}

void SceneEditor::dismissBlockingPopups() {
    m_assetBrowser.dismissTransientUI();
    m_buildDialog.close();
    m_settingsPanel.close();
    ImGuiContext& g = *GImGui;
    while (g.OpenPopupStack.Size > 0) {
        ImGui::ClosePopupToLevel(g.OpenPopupStack.Size - 1, false);
    }
}

void SceneEditor::onQuitRequested() {
    if (!hasUnsavedChanges() || !m_settingsPanel.preferences().confirmOnSceneClose) {
        m_open = false;
        m_quitConfirmed = true;
        return;
    }
    dismissBlockingPopups();
    m_pendingAction = PendingAction::Exit;
    m_showQuitPopup = true;
}

// ── Play mode control ───────────────────────────────────────────

void SceneEditor::registerPlayModeEventListeners() {
    if (m_playListenersRegistered) return;
    m_collisionListener = m_eventBus.subscribe<Events::OnCollision2D>(
        [this](const Events::OnCollision2D& event) {
            if (!m_isPlaying || m_isPaused) return;
            ECS::World* world = m_tabManager.activeWorld();
            if (!world) return;
            handleCollision2D(*world, event);
        });
    m_playListenersRegistered = true;
}

void SceneEditor::handleCollision2D(ECS::World& world, const Events::OnCollision2D& event) {
#ifdef CF_HAS_SCRIPTING
    if (!m_scriptEngineReady) return;

    auto notify = [&](u32 entityId, u32 otherId) {
        ECS::Entity self(entityId, &world);
        ECS::Entity other(otherId, &world);
        if (!self.isValid() || !other.isValid()) return;
        const auto* script = world.get<Script::ScriptComponent>(self);
        if (!script || script->scriptPath.empty()) return;
        m_scriptEngine.callOnCollision(script->scriptPath, self, other);
    };

    notify(event.entityA, event.entityB);
    notify(event.entityB, event.entityA);
#endif
}

void SceneEditor::enterPlayMode(ECS::World& world) {
    m_viewportPlaySnapshot.panX = m_ctx.viewportPanX;
    m_viewportPlaySnapshot.panY = m_ctx.viewportPanY;
    m_viewportPlaySnapshot.zoom = m_ctx.viewportZoom;
    m_ctx.isPlayMode = true;

    m_playSnapshot.clear();
    ECS::ComponentQuery q;
    q.with<ECS::Transform>();
    world.forEach<ECS::Transform>(q,
        [&](ECS::Entity e, ECS::Transform& pos) {
            EntitySnapshot snap;
            snap.id = e.id();
            snap.px = pos.position.x; snap.py = pos.position.y;
            snap.rz = pos.rotation.z;
            m_playSnapshot.push_back(snap);
        });
    m_isPlaying = true;
    m_isPaused  = false;
#ifdef CF_HAS_SCRIPTING
    if (!m_scriptEngineReady) {
        Script::ScriptEngine::InitParams p;
        p.world  = &world;
        p.events = &m_eventBus;
        m_scriptEngineReady = m_scriptEngine.init(p);
        m_scriptSystem = Script::ScriptSystem(&m_scriptEngine);
    }
#endif
}

void SceneEditor::exitPlayMode(ECS::World& world) {
    m_isPlaying = false;
    m_isPaused  = false;
    m_ctx.isPlayMode = false;
    m_ctx.viewportPanX = m_viewportPlaySnapshot.panX;
    m_ctx.viewportPanY = m_viewportPlaySnapshot.panY;
    m_ctx.viewportZoom = m_viewportPlaySnapshot.zoom;
    m_playCamera2D.stopFollowing();

    for (auto& snap : m_playSnapshot) {
        ECS::Entity e(snap.id, &world);
        if (!e.isValid()) continue;
        if (auto* pos = world.get<ECS::Transform>(e)) { pos->position.x = snap.px; pos->position.y = snap.py; pos->rotation.z = snap.rz; }
    }
    m_playSnapshot.clear();
}

void SceneEditor::tickSystems(ECS::World& world, f32 dt) {
    if (!m_isPlaying || m_isPaused) return;
    m_animationSystem.onUpdate(world, dt);
    m_physicsSystem.onUpdate(world, dt);
#ifdef CF_HAS_SCRIPTING
    if (m_scriptEngineReady) m_scriptSystem.onUpdate(world, dt);
#endif
    {
        auto& io = ImGui::GetIO();
        m_uiSystem.injectMousePosition({io.MousePos.x, io.MousePos.y});
        m_uiSystem.injectMouseClick(io.MouseDown[0]);
    }
    m_uiSystem.onUpdate(world, dt);

    if (m_ctx.viewMode == EditorContext::ViewMode::Mode2D) {
        const ECS::Entity cameraEntity = Scene::findActiveCamera2DEntity(world);
        if (cameraEntity.isValid()) {
            const auto* transform = world.get<ECS::Transform>(cameraEntity);
            const auto* camera = world.get<ECS::Camera2DComponent>(cameraEntity);
            if (transform && camera) {
                m_playCamera2D.update(dt, world);
                Scene::syncViewportFromCamera2D(m_ctx, *transform, *camera);
            }
        }
    }

    m_eventBus.dispatch();
}

void SceneEditor::renderPlaybar(ECS::World& world) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBringToFrontOnFocus
                           | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 60.0f, 30.0f), ImGuiCond_Always);
    if (ImGui::Begin("##PlayBar", nullptr, flags)) {
        if (!m_isPlaying) {
            if (ImGui::Button(" Play ")) enterPlayMode(world);
        } else {
            if (m_isPaused) {
                if (ImGui::Button("Resume")) m_isPaused = false;
            } else {
                if (ImGui::Button(" Pause")) m_isPaused = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(" Stop ")) exitPlayMode(world);
        }
    }
    ImGui::End();
}
#endif

// ── Main render ─────────────────────────────────────────────────

void SceneEditor::render(f32 deltaTime) {
    CF_PROFILE_SCOPE("SceneEditor::render");
    if (!m_open) return;

    ECS::World* activeWorld = m_tabManager.activeWorld();
    if (!activeWorld) {
        renderUnsavedChangesPopup(nullptr);
        return;
    }

    if (m_tabManager.activeTabIndex() >= 0) {
        m_tabManager.activeTab().isDirty = m_ctx.isDirty;
    }

    if (m_showQuitPopup) {
        dismissBlockingPopups();
        m_showQuitPopup = false;
    }

    tickSystems(*activeWorld, deltaTime);
    m_ctx.tickTransientStatus(deltaTime);
    PluginManager::instance().refreshPlugins(deltaTime);

    handleShortcuts(*activeWorld);

#ifdef CF_HAS_SCRIPTING
    if (!m_scriptWatcherStarted && !m_currentProjectConfig.RootPath.empty()) {
        std::filesystem::path scriptsDir = m_currentProjectConfig.RootPath / "scripts";
        if (std::filesystem::exists(scriptsDir)) {
            m_scriptFileWatcher.start(scriptsDir, true);
            m_scriptWatcherStarted = true;
        }
    }
    if (m_scriptWatcherStarted) {
        auto changed = m_scriptFileWatcher.poll();
        if (!changed.empty() && m_scriptEngineReady && m_ctx.scriptEngine) {
            for (const auto& path : changed) {
                std::string err;
                m_ctx.scriptEngine->loadScript(path.string(), &err);
            }
        }
    }
#endif

    // Setup dockspace root window
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar
                                 | ImGuiWindowFlags_NoDocking
                                 | ImGuiWindowFlags_NoTitleBar
                                 | ImGuiWindowFlags_NoCollapse
                                 | ImGuiWindowFlags_NoResize
                                 | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    // ── Scene tab bar ──
    auto tabResult = m_tabManager.renderTabBar();
    if (tabResult.switchToIndex >= 0) {
        m_tabManager.setActiveTab(tabResult.switchToIndex, m_ctx);
    }
    if (tabResult.newTabRequested) {
        doNewScene();
    }
    if (tabResult.closeCandidate >= 0) {
        closeTab(tabResult.closeCandidate);
    }

    // Re-acquire active world after potential tab switch/close
    activeWorld = m_tabManager.activeWorld();
    if (!activeWorld) { ImGui::End(); return; }

    m_dockspaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(m_dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (!m_dockingSetup || m_layoutNeedsRebuild) {
        ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(m_dockspaceId);
        bool hasExistingLayout = existingNode != nullptr && existingNode->IsSplitNode();

        const auto& profile = m_settingsPanel.layoutManager().currentProfile();
        if (!hasExistingLayout || m_layoutNeedsRebuild) {
            applyLayoutProfile(m_dockspaceId, profile);
        }
        
         // Apply visibility from profile to panels
         profile.hierarchyOpen ? m_hierarchy.open() : m_hierarchy.close();
         profile.inspectorOpen ? m_inspector.open() : m_inspector.close();
         profile.viewportOpen ? m_viewport.open() : m_viewport.close();
         profile.assetsOpen ? m_assetBrowser.open() : m_assetBrowser.close();
         profile.consoleOpen ? m_console.open() : m_console.close();
         profile.profilerOpen ? m_profiler.open() : m_profiler.close();
         profile.scriptEditorOpen ? m_scriptEditor.open() : m_scriptEditor.close();
         profile.tilemapEditorOpen ? m_tilemapEditor.open() : m_tilemapEditor.close();
         profile.animationTimelineOpen ? m_animationTimeline.open() : m_animationTimeline.close();
         profile.animatorControllerOpen ? m_animatorController.open() : m_animatorController.close();
         m_materialEditor.open();
         m_terrainEditor.open();
        
        m_layoutNeedsRebuild = false;
        m_dockingSetup = true;
    }

    renderMainMenuBar(*activeWorld);

    Scene::propagateTransforms(*activeWorld);

    // Render panels
    m_hierarchy.render(*activeWorld, m_ctx);
    m_inspector.render(*activeWorld, m_ctx);
    renderPlaybar(*activeWorld);
    m_viewport.setFrameCommandBuffer(m_frameCmd);
    m_viewport.render(*activeWorld, m_ctx);
    m_viewport.setFrameCommandBuffer(nullptr);
    m_assetBrowser.render(*activeWorld, m_ctx);
    m_console.render();
    m_profiler.render(Debug::Profiler::instance());
    m_entityDebugger.render(*activeWorld, m_ctx);
    m_scriptEditor.render();
    m_settingsPanel.render();
    m_materialEditor.onImGuiRender();
    m_terrainEditor.render(*activeWorld, m_ctx);
    m_audioPreview.onImGuiRender();
    m_cameraPreview.onImGuiRender(*activeWorld, m_ctx, m_viewport);
    m_animationTimeline.render(deltaTime);
    m_animatorController.render();
    m_tilemapEditor.render();
    m_commandPalette.render();
    m_buildDialog.render();
    PluginManager::instance().renderPanels();
    PluginManager::instance().renderManagerUI();

    ImGui::End(); // DockSpace

    renderUnsavedChangesPopup(activeWorld);
    renderStatusBar(*activeWorld);
    m_profiler.pushFrameTime(deltaTime * 1000.0f);
}

// ── Dockspace setup ─────────────────────────────────────────────

void SceneEditor::setupDockspace(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID dockLeft, dockRight, dockBottom, dockCenter;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.20f, &dockLeft, &dockCenter);
    ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);
    ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottom, &dockCenter);

     ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
     ImGui::DockBuilderDockWindow("Inspector", dockRight);
     ImGui::DockBuilderDockWindow("Scene Viewport", dockCenter);
     ImGui::DockBuilderDockWindow("Camera Preview", dockCenter);
     ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
     ImGui::DockBuilderDockWindow("Console", dockBottom);
     ImGui::DockBuilderDockWindow("Profiler", dockBottom);
     ImGui::DockBuilderDockWindow("Entity Debugger", dockBottom);
     ImGui::DockBuilderDockWindow("Plugin Manager", dockBottom);
     ImGui::DockBuilderDockWindow("Material Editor", dockBottom);
     ImGui::DockBuilderDockWindow("Terrain Editor", dockBottom);

     ImGui::DockBuilderFinish(dockspaceId);
}

// ── Menu bar ────────────────────────────────────────────────────

void SceneEditor::renderMainMenuBar(ECS::World& world) {
    if (ImGui::BeginMainMenuBar()) {
        if (EditorIcons::hasBrandLogo()) {
            EditorIcons::brandLogo(ImGui::GetFrameHeight() * 0.85f);
            ImGui::SameLine();
        }

        if (ImGui::BeginMenu("File")) {
            if (EditorIcons::menuItem(EditorIcon::NewScene, "New Scene", "Ctrl+N")) {
                if (m_ctx.isDirty) {
                    m_pendingAction = PendingAction::NewScene;
                    ImGui::OpenPopup("Unsaved Changes?");
                } else {
                    doNewScene();
                }
            }
            if (EditorIcons::menuItem(EditorIcon::Save, "Save", "Ctrl+S")) {
                if (m_ctx.currentScenePath.empty()) {
                    saveSceneAs(world);
                } else {
                    saveScene(m_ctx.currentScenePath.c_str(), world);
                }
            }
            if (EditorIcons::menuItem(EditorIcon::SaveAs, "Save As...")) {
                saveSceneAs(world);
            }
            if (EditorIcons::menuItem(EditorIcon::Open, "Open...", "Ctrl+O")) {
                if (m_ctx.isDirty) {
                    m_pendingAction = PendingAction::OpenScene;
                    ImGui::OpenPopup("Unsaved Changes?");
                } else {
                    auto newWorld = std::make_unique<ECS::World>();
                    Editor::SceneSerializer serializer(*newWorld);
                    if (serializer.deserialize("scene.caf")) {
                        m_tabManager.addTab("scene.caf", std::move(newWorld));
                        m_tabManager.setActiveTab(m_tabManager.tabCount() - 1, m_ctx);
                    }
                }
            }
            PluginManager::instance().renderMenuItems("File");
            ImGui::Separator();
            if (EditorIcons::menuItem(EditorIcon::Exit, "Exit")) {
                onQuitRequested();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (EditorIcons::menuItem(EditorIcon::Undo, "Undo", "Ctrl+Z", false, m_ctx.undoStack.canUndo())) {
                m_ctx.undoStack.undo(world);
            }
            if (EditorIcons::menuItem(EditorIcon::Redo, "Redo", "Ctrl+Y", false, m_ctx.undoStack.canRedo())) {
                m_ctx.undoStack.redo(world);
            }
            ImGui::Separator();
            if (EditorIcons::menuItem(EditorIcon::Copy, "Copy", "Ctrl+C", false, m_ctx.selectedEntity.isValid())) {
                m_ctx.clipboardEntity = m_ctx.selectedEntity;
            }
            if (EditorIcons::menuItem(EditorIcon::Paste, "Paste", "Ctrl+V", false, m_ctx.clipboardEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.clipboardEntity);
            }
            if (EditorIcons::menuItem(EditorIcon::Duplicate, "Duplicate", "Ctrl+D", false, m_ctx.selectedEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.selectedEntity);
            }
            PluginManager::instance().renderMenuItems("Edit");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            PluginManager::instance().renderMenuItems("Help");
            ImGui::EndMenu();
        }

        PluginManager::instance().renderMenuExtensions();

        if (ImGui::BeginMenu("Plugins")) {
            if (EditorIcons::menuItem(EditorIcon::Plugins, "Plugin Manager...")) {
                PluginManager::instance().openManager();
            }
            if (EditorIcons::menuItem(EditorIcon::Refresh, "Refresh Plugins")) {
                PluginManager::instance().refreshPluginsDirectory();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            bool hierarchyOpen = m_hierarchy.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Hierarchy, "Hierarchy", nullptr, &hierarchyOpen)) {
                hierarchyOpen ? m_hierarchy.open() : m_hierarchy.close();
            }
            bool inspectorOpen = m_inspector.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Inspector, "Inspector", nullptr, &inspectorOpen)) {
                inspectorOpen ? m_inspector.open() : m_inspector.close();
            }
            bool viewportOpen = m_viewport.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Viewport, "Viewport", nullptr, &viewportOpen)) {
                viewportOpen ? m_viewport.open() : m_viewport.close();
            }
            bool assetsOpen = m_assetBrowser.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Assets, "Assets", nullptr, &assetsOpen)) {
                assetsOpen ? m_assetBrowser.open() : m_assetBrowser.close();
            }
            bool consoleOpen = m_console.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Console, "Console", nullptr, &consoleOpen)) {
                consoleOpen ? m_console.open() : m_console.close();
            }
            bool profilerOpen = m_profiler.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Profiler, "Profiler", nullptr, &profilerOpen)) {
                profilerOpen ? m_profiler.open() : m_profiler.close();
            }
            bool entityDebuggerOpen = m_entityDebugger.isOpen();
            if (EditorIcons::menuItem(EditorIcon::EntityDebugger, "Entity Debugger", nullptr, &entityDebuggerOpen)) {
                entityDebuggerOpen ? m_entityDebugger.open() : m_entityDebugger.close();
            }
            ImGui::Separator();
            bool atOpen = m_animationTimeline.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Animation, "Animation Timeline", nullptr, &atOpen))
                atOpen ? m_animationTimeline.open() : m_animationTimeline.close();
            bool acOpen = m_animatorController.isOpen();
            if (EditorIcons::menuItem(EditorIcon::Animator, "Animator Controller", nullptr, &acOpen))
                acOpen ? m_animatorController.open() : m_animatorController.close();
            bool terrainEditorOpen = m_terrainEditor.isOpen();
            if (ImGui::MenuItem("Terrain Editor", nullptr, terrainEditorOpen)) {
                terrainEditorOpen ? m_terrainEditor.close() : m_terrainEditor.open();
            }
            syncLayoutProfileFromPanels();
            ImGui::EndMenu();
        }

        {
            const float btnW    = 60.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float totalW  = m_isPlaying ? (btnW * 2 + spacing) : btnW;
            ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x * 0.5f - totalW * 0.5f);

#ifdef CF_HAS_SCRIPTING
            if (!m_isPlaying) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.55f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.70f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.40f, 0.12f, 1.0f));
                bool playClicked = EditorIcons::hasIcon("arrow-right")
                    ? EditorIcons::iconButton("arrow-right", "play", 18.0f)
                    : ImGui::Button("Play", ImVec2(btnW, 0));
                if (playClicked) enterPlayMode(world);
                ImGui::PopStyleColor(3);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.45f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.58f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.33f, 0.08f, 1.0f));
                bool transportClicked = false;
                if (m_isPaused) {
                    transportClicked = EditorIcons::hasIcon("arrow-right")
                        ? EditorIcons::iconButton("arrow-right", "resume", 18.0f)
                        : ImGui::Button("Resume", ImVec2(btnW, 0));
                    if (transportClicked) m_isPaused = false;
                } else {
                    transportClicked = EditorIcons::hasIcon("arrow-close-down")
                        ? EditorIcons::iconButton("arrow-close-down", "pause", 18.0f)
                        : ImGui::Button("Pause", ImVec2(btnW, 0));
                    if (transportClicked) m_isPaused = true;
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.12f, 0.12f, 1.0f));
                const bool stopClicked = EditorIcons::hasIcon("arrow-close-left")
                    ? EditorIcons::iconButton("arrow-close-left", "stop", 18.0f)
                    : ImGui::Button("Stop", ImVec2(btnW, 0));
                if (stopClicked) exitPlayMode(world);
                ImGui::PopStyleColor(3);
            }
#else
            ImGui::BeginDisabled();
            ImGui::Button(reinterpret_cast<const char*>(u8"\u25B6 Play"), ImVec2(btnW, 0));
            ImGui::EndDisabled();
#endif
        }

        char dirtyMarker = m_ctx.isDirty ? '*' : ' ';
        char buf[64];
        snprintf(buf, sizeof(buf), "Caffeine Studio — Scene%c", dirtyMarker);
        ImGui::Text("    %s", buf);

        ImGui::EndMainMenuBar();
    }
}

// ── Status bar ──────────────────────────────────────────────────

void SceneEditor::renderStatusBar(ECS::World& world) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));

    if (ImGui::BeginMainMenuBar()) {
        // Scene path
        if (!m_ctx.currentScenePath.empty()) {
            ImGui::Text("Scene: %s", m_ctx.currentScenePath.c_str());
        } else {
            ImGui::Text("Scene: untitled");
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Dirty flag
        if (m_ctx.isDirty) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Modified");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1), "Saved");
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Undo stack depth
        ImGui::Text("Undo: %u", m_ctx.undoStack.count());

        if (!m_ctx.transientStatus.empty()) {
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();
            const ImVec4 color = m_ctx.transientStatusIsError
                ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                : ImVec4(0.45f, 0.9f, 0.45f, 1.0f);
            ImGui::TextColored(color, "%s", m_ctx.transientStatus.c_str());
        }

        if (m_settingsPanel.preferences().showFPSInStatusBar) {
            const f32 frameMs = m_profiler.lastFrameTime();
            if (frameMs > 0.0f) {
                const f32 fps = 1000.0f / frameMs;
                ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
                if (frameMs > 33.0f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%.1f FPS  %.1f ms", fps, frameMs);
                } else {
                    ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.65f, 1.0f), "%.1f FPS  %.1f ms", fps, frameMs);
                }
            }
        }

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar();
}

// ── Shortcuts ───────────────────────────────────────────────────

void SceneEditor::handleShortcuts(ECS::World& world) {
    bool ctrl = ImGui::GetIO().KeyCtrl;
    bool shift = ImGui::GetIO().KeyShift;

    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_P)) {
        m_commandPalette.toggle();
        return;
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (m_ctx.currentScenePath.empty()) {
            saveSceneAs(world);
        } else {
            saveScene(m_ctx.currentScenePath.c_str(), world);
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
        if (m_ctx.isDirty) {
            m_pendingAction = PendingAction::NewScene;
            ImGui::OpenPopup("Unsaved Changes?");
        } else {
            doNewScene();
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (ImGui::GetIO().KeyShift) {
            if (m_ctx.undoStack.canRedo()) m_ctx.undoStack.redo(world);
        } else {
            if (m_ctx.undoStack.canUndo()) m_ctx.undoStack.undo(world);
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (m_ctx.undoStack.canRedo()) m_ctx.undoStack.redo(world);
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        if (m_ctx.selectedEntity.isValid()) {
            m_ctx.clipboardEntity = m_ctx.selectedEntity;
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        if (m_ctx.clipboardEntity.isValid()) {
            m_hierarchy.duplicateEntity(world, m_ctx.clipboardEntity);
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        if (m_ctx.selectedEntity.isValid()) {
            m_hierarchy.duplicateEntity(world, m_ctx.selectedEntity);
        }
    }
}

// ── Serialization ───────────────────────────────────────────────

bool SceneEditor::saveScene(const char* path, ECS::World& world) {
    Editor::SceneSerializer serializer(world);
    if (!serializer.serialize(path)) return false;
    if (m_tabManager.activeTabIndex() >= 0) {
        m_tabManager.activeTab().path = path;
        m_tabManager.activeTab().isDirty = false;
    }
    m_ctx.currentScenePath = path;
    m_ctx.isDirty = false;

    // Persist LastScene in project.caffeine so it reopens automatically
    if (!m_currentProjectConfig.RootPath.empty()) {
        std::filesystem::path root = m_currentProjectConfig.RootPath;
        std::filesystem::path scenePath(path);
        // Store relative path if scene is inside the project root
        std::error_code ec;
        auto rel = std::filesystem::relative(scenePath, root, ec);
        std::string lastScene = (!ec && !rel.empty()) ? rel.string() : scenePath.string();
        if (lastScene.rfind("data/scenes/", 0) == 0) {
            lastScene = "scenes/" + std::filesystem::path(lastScene).filename().string();
        }
        if (lastScene.find("/build/") != std::string::npos ||
            lastScene.find("\\build\\") != std::string::npos) {
            lastScene = "scenes/" + std::filesystem::path(lastScene).filename().string();
        }
        m_currentProjectConfig.LastScene = lastScene;
        ProjectManager pm;
        pm.SaveProjectFile(m_currentProjectConfig);
    }

    return true;
}

bool SceneEditor::saveSceneAs(ECS::World& world) {
    std::filesystem::path defaultPath;
    if (!m_currentProjectConfig.RootPath.empty()) {
        defaultPath = m_currentProjectConfig.RootPath / "scenes" / "main.caf";
        std::error_code ec;
        std::filesystem::create_directories(defaultPath.parent_path(), ec);
    } else {
        defaultPath = "scenes/main.caf";
    }
    return saveScene(defaultPath.string().c_str(), world);
}

std::string SceneEditor::prepareSceneForBuild() {
    ECS::World* world = m_tabManager.activeWorld();
    if (!world || m_currentProjectConfig.RootPath.empty()) {
        return {};
    }

    // Always snapshot the active editor scene to a stable project path for packaging.
    const std::filesystem::path scenePath =
        m_currentProjectConfig.RootPath / "scenes" / "main.caf";

    std::error_code ec;
    std::filesystem::create_directories(scenePath.parent_path(), ec);
    if (!saveScene(scenePath.string().c_str(), *world)) {
        return {};
    }

    if (m_tabManager.activeTabIndex() >= 0) {
        m_tabManager.activeTab().path = scenePath.string();
        m_tabManager.activeTab().name = scenePath.stem().string();
    }

    return "scenes/main.caf";
}

bool SceneEditor::loadScene(const char* path, ECS::World& world) {
    Editor::SceneSerializer serializer(world);
    if (!serializer.deserialize(path)) return false;
    m_ctx.currentScenePath = path;
    m_ctx.selectedEntity = ECS::Entity::INVALID;
    m_ctx.isDirty = false;
    if (m_tabManager.activeTabIndex() >= 0) {
        m_tabManager.activeTab().path = path;
        m_tabManager.activeTab().name = std::filesystem::path(path).stem().string();
        m_tabManager.activeTab().isDirty = false;
    }
    return true;
}

// ── Close tab with confirmation ──────────────────────────────────

void SceneEditor::closeTab(int index) {
    if (index < 0 || index >= m_tabManager.tabCount()) return;
    auto& tab = m_tabManager.tab(index);
    const bool dirty = (index == m_tabManager.activeTabIndex()) ? m_ctx.isDirty : tab.isDirty;
    if (dirty) {
        m_pendingCloseTab = index;
        ImGui::OpenPopup("Unsaved Tab?");
    } else {
        m_tabManager.closeScene(index);
    }
}

// ── Unsaved changes popup ───────────────────────────────────────

void SceneEditor::renderUnsavedChangesPopup(ECS::World* world) {
    // "Unsaved Changes?" — triggered by menu actions (New/Open/Exit)
    if (m_pendingAction != PendingAction::None) {
        if (!ImGui::IsPopupOpen("Unsaved Changes?", ImGuiPopupFlags_AnyPopupId)) {
            dismissBlockingPopups();
            ImGui::OpenPopup("Unsaved Changes?");
        }
        if (ImGui::BeginPopupModal("Unsaved Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("There are unsaved changes. Do you want to save before continuing?");
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (world) {
                    if (m_ctx.currentScenePath.empty()) {
                        saveSceneAs(*world);
                    } else {
                        saveScene(m_ctx.currentScenePath.c_str(), *world);
                    }
                }
                ImGui::CloseCurrentPopup();
                executePendingAction(world);
                m_pendingAction = PendingAction::None;
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                executePendingAction(world);
                m_pendingAction = PendingAction::None;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                m_pendingAction = PendingAction::None;
            }
            ImGui::EndPopup();
        }
    }

    // "Unsaved Tab?" — triggered by closing a dirty tab
    if (m_pendingCloseTab >= 0) {
        bool popupOpen = true;
        if (ImGui::BeginPopupModal("Unsaved Tab?", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save changes to \"%s\" before closing?",
                        m_tabManager.tab(m_pendingCloseTab).name.c_str());
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0))) {
                auto& tab = m_tabManager.tab(m_pendingCloseTab);
                if (m_pendingCloseTab == m_tabManager.activeTabIndex()) {
                    if (m_ctx.currentScenePath.empty()) {
                        saveSceneAs(*tab.world);
                    } else {
                        saveScene(m_ctx.currentScenePath.c_str(), *tab.world);
                    }
                } else if (!tab.path.empty()) {
                    Editor::SceneSerializer serializer(*tab.world);
                    if (serializer.serialize(tab.path)) {
                        tab.isDirty = false;
                    }
                } else {
                    saveSceneAs(*tab.world);
                }
                m_tabManager.closeScene(m_pendingCloseTab);
                ImGui::CloseCurrentPopup();
                m_pendingCloseTab = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
                m_tabManager.closeScene(m_pendingCloseTab);
                ImGui::CloseCurrentPopup();
                m_pendingCloseTab = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                m_pendingCloseTab = -1;
            }
            ImGui::EndPopup();
        }
        if (!popupOpen) {
            m_pendingCloseTab = -1;
        }
    }
}

void SceneEditor::executePendingAction(ECS::World* world) {
    switch (m_pendingAction) {
        case PendingAction::NewScene:
            doNewScene();
            break;
        case PendingAction::OpenScene: {
            if (world) {
                (void)world;
            }
            auto newWorld = std::make_unique<ECS::World>();
            Editor::SceneSerializer serializer(*newWorld);
            if (serializer.deserialize("scene.caf")) {
                m_tabManager.addTab("scene.caf", std::move(newWorld));
                m_tabManager.setActiveTab(m_tabManager.tabCount() - 1, m_ctx);
            }
            break;
        }
        case PendingAction::Exit:
            m_open = false;
            m_quitConfirmed = true;
            break;
        default:
            break;
    }
}

void SceneEditor::doNewScene() {
    m_tabManager.newScene("Untitled");
    m_tabManager.setActiveTab(m_tabManager.tabCount() - 1, m_ctx);
}

// ── Asset drop ──────────────────────────────────────────────────

void SceneEditor::handleAssetDrop(ECS::World& world) {
    auto dropped = m_assetBrowser.getDroppedAsset();
    if (!dropped) return;

    std::filesystem::path assetPath = *dropped;
    std::string ext = assetPath.extension().string();

    m_ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);

    ECS::Entity entity = world.create();
    setEntityName(world, entity, assetPath.stem().string().c_str());
    world.add<ECS::Transform>(entity);

    if (ext == ".caf" || ext == ".png" || ext == ".jpg") {
        world.add<ECS::Sprite>(entity, assetPath.string(), 0);
    }

    m_ctx.selectedEntity = entity;
    m_ctx.endUndo(world);
}

// ── Layout profile application ──────────────────────────────────

void SceneEditor::syncLayoutProfileFromPanels() {
    LayoutProfile profile = m_settingsPanel.layoutManager().currentProfile();
    profile.hierarchyOpen = m_hierarchy.isOpen();
    profile.inspectorOpen = m_inspector.isOpen();
    profile.viewportOpen = m_viewport.isOpen();
    profile.assetsOpen = m_assetBrowser.isOpen();
    profile.consoleOpen = m_console.isOpen();
    profile.profilerOpen = m_profiler.isOpen();
    profile.animationTimelineOpen = m_animationTimeline.isOpen();
    profile.animatorControllerOpen = m_animatorController.isOpen();
    profile.tilemapEditorOpen = m_tilemapEditor.isOpen();
    profile.scriptEditorOpen = m_scriptEditor.isOpen();
    m_settingsPanel.layoutManager().updateCurrentProfile(profile);
    m_settingsPanel.savePreferences();
}

void SceneEditor::applyLayoutProfile(ImGuiID dockspaceId, const LayoutProfile& profile) {
    // Remove the old dockspace layout
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    // Count visible panels to determine splits
    int visibleCount = 0;
    if (profile.hierarchyOpen) visibleCount++;
    if (profile.inspectorOpen) visibleCount++;
    if (profile.viewportOpen) visibleCount++;
    if (profile.assetsOpen) visibleCount++;
    if (profile.consoleOpen) visibleCount++;
    if (profile.profilerOpen) visibleCount++;
    if (profile.animationTimelineOpen) visibleCount++;
    if (profile.animatorControllerOpen) visibleCount++;
    if (profile.tilemapEditorOpen) visibleCount++;
    if (profile.scriptEditorOpen) visibleCount++;

    if (visibleCount == 0) {
        // Ensure at least viewport is visible
        ImGui::DockBuilderDockWindow("Scene Viewport", dockspaceId);
        ImGui::DockBuilderFinish(dockspaceId);
        return;
    }

    ImGuiID dockLeft = dockspaceId;
    ImGuiID dockRight = dockspaceId;
    ImGuiID dockBottom = dockspaceId;
    ImGuiID dockCenter = dockspaceId;

    // Left panel (Hierarchy) - if enabled
    if (profile.hierarchyOpen) {
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, profile.hierarchyWidth, &dockLeft, &dockCenter);
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    }

    // Right panel (Inspector) - if enabled
    if (profile.inspectorOpen) {
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, profile.inspectorWidth / (1.0f - profile.hierarchyWidth), &dockRight, &dockCenter);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
    }

    // Bottom panels (Assets, Console, Profiler, etc.) - if any enabled
    if (profile.assetsOpen || profile.consoleOpen || profile.profilerOpen || 
        profile.animationTimelineOpen || profile.animatorControllerOpen || profile.tilemapEditorOpen || profile.scriptEditorOpen) {
        ImGuiID dockBottomRegion;
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottomRegion, &dockCenter);
        
        if (profile.assetsOpen) ImGui::DockBuilderDockWindow("Asset Browser", dockBottomRegion);
        if (profile.consoleOpen) ImGui::DockBuilderDockWindow("Console", dockBottomRegion);
        if (profile.profilerOpen) ImGui::DockBuilderDockWindow("Profiler", dockBottomRegion);
        if (profile.animationTimelineOpen) ImGui::DockBuilderDockWindow("Animation Timeline", dockBottomRegion);
        if (profile.animatorControllerOpen) ImGui::DockBuilderDockWindow("Animator Controller", dockBottomRegion);
        if (profile.tilemapEditorOpen) ImGui::DockBuilderDockWindow("Tilemap Editor", dockBottomRegion);
        if (profile.scriptEditorOpen) ImGui::DockBuilderDockWindow("Script Editor", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Build & Run", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Audio Preview", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Terrain Editor", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Settings", dockBottomRegion);
    }

    // Center panel (Viewport) - always visible or fallback
    if (profile.viewportOpen) {
        ImGui::DockBuilderDockWindow("Scene Viewport", dockCenter);
    } else if (visibleCount > 0) {
        // If viewport is hidden but other panels are visible, use remaining space
        ImGui::DockBuilderDockWindow("Scene Viewport", dockCenter);
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
