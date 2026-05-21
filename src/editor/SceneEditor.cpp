#include "editor/SceneEditor.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "editor/ComponentRegistry.hpp"
#include "scene/HierarchySystem.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui_internal.h>

namespace Caffeine::Editor {

// ── Init / Shutdown ─────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                       const ProjectConfig& projectConfig) {
    if (!m_viewport.init(device)) return false;
    m_assetBrowser.init(projectConfig);
    m_assetBrowser.setOnScriptOpen([this](const std::filesystem::path& path) {
        m_scriptEditor.open();
        m_scriptEditor.openFile(path);
    });
    m_assetManager = assetManager;
    m_currentProjectConfig = projectConfig;
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
    m_commandPalette.registerCommand("panel_asset_browser", "Asset Browser", "Panels", [this]() {
        m_assetBrowser.open();
    });
    m_commandPalette.registerCommand("panel_animation_timeline", "Animation Timeline", "Panels", [this]() {
        m_animationTimeline.open();
    });
    m_commandPalette.registerCommand("panel_tilemap", "Tilemap Editor", "Panels", [this]() {
        m_tilemapEditor.open();
    });
    m_commandPalette.registerCommand("panel_script_editor", "Script Editor", "Panels", [this]() {
        m_scriptEditor.open();
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

    // Register layout change callback
    m_settingsPanel.setLayoutChangeCallback([this]() {
        requestLayoutRebuild();
    });

    // Auto-load last scene if project config has one
    if (!projectConfig.LastScene.empty()) {
        std::filesystem::path scenePath = projectConfig.RootPath / projectConfig.LastScene;
         if (std::filesystem::exists(scenePath)) {
            if (auto* world = m_tabManager.activeWorld()) {
                loadScene(scenePath.string().c_str(), *world);
                m_tabManager.activeTab().name = std::filesystem::path(projectConfig.LastScene).stem().string();
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

    return true;
}

void SceneEditor::shutdown() {
    m_viewport.shutdown();
    m_audioPreview.shutdown();
}

// ── Play mode control ───────────────────────────────────────────

void SceneEditor::enterPlayMode(ECS::World& world) {
    m_playSnapshot.clear();
    ECS::ComponentQuery q;
    q.with<ECS::Transform>();
    world.forEach<ECS::Transform>(q,
        [&](ECS::Entity e, ECS::Transform& pos) {
            EntitySnapshot snap;
            snap.id = e.id();
            snap.px = pos.position.x; snap.py = pos.position.y;
            snap.rz = pos.rotation.z;
            if (auto* v = world.get<ECS::Velocity2D>(e)) { snap.vx = v->x; snap.vy = v->y; }
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
    for (auto& snap : m_playSnapshot) {
        ECS::Entity e(snap.id, &world);
        if (!e.isValid()) continue;
        if (auto* pos = world.get<ECS::Transform>(e)) { pos->position.x = snap.px; pos->position.y = snap.py; pos->rotation.z = snap.rz; }
        if (auto* v   = world.get<ECS::Velocity2D>(e)) { v->x = snap.vx;  v->y = snap.vy;  }
    }
    m_playSnapshot.clear();
}

void SceneEditor::tickSystems(ECS::World& world, f32 dt) {
    if (!m_isPlaying || m_isPaused) return;
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
    if (!m_open) return;

    ECS::World* activeWorld = m_tabManager.activeWorld();
    if (!activeWorld) return;

    tickSystems(*activeWorld, deltaTime);

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
        
        m_layoutNeedsRebuild = false;
        m_dockingSetup = true;
    }

    renderMainMenuBar(*activeWorld);
    renderUnsavedChangesPopup(*activeWorld);

    Scene::propagateTransforms(*activeWorld);

    // Render panels
    m_hierarchy.render(*activeWorld, m_ctx);
    m_inspector.render(*activeWorld, m_ctx);
    renderPlaybar(*activeWorld);
    m_viewport.render(*activeWorld, m_ctx);
    m_assetBrowser.render(m_ctx);
    m_console.render();
    m_profiler.render(Debug::Profiler::instance());
    m_scriptEditor.render();
    m_settingsPanel.render();
    m_materialEditor.onImGuiRender();
    m_audioPreview.onImGuiRender();
    m_cameraPreview.onImGuiRender(*activeWorld, m_ctx);
    m_animationTimeline.render(deltaTime);
    m_tilemapEditor.render();
    m_commandPalette.render();
    m_buildDialog.render();

    handleAssetDrop(*activeWorld);

    ImGui::End(); // DockSpace

    renderStatusBar(*activeWorld);
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

    ImGui::DockBuilderFinish(dockspaceId);
}

// ── Menu bar ────────────────────────────────────────────────────

void SceneEditor::renderMainMenuBar(ECS::World& world) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                if (m_ctx.isDirty) {
                    m_pendingAction = PendingAction::NewScene;
                    ImGui::OpenPopup("Unsaved Changes?");
                } else {
                    doNewScene();
                }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (m_ctx.currentScenePath.empty()) {
                    saveSceneAs(world);
                } else {
                    saveScene(m_ctx.currentScenePath.c_str(), world);
                }
            }
            if (ImGui::MenuItem("Save As...")) {
                saveSceneAs(world);
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
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
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (m_ctx.isDirty) {
                    m_pendingAction = PendingAction::Exit;
                    ImGui::OpenPopup("Unsaved Changes?");
                } else {
                    m_open = false;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_ctx.undoStack.canUndo())) {
                m_ctx.undoStack.undo(world);
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_ctx.undoStack.canRedo())) {
                m_ctx.undoStack.redo(world);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, m_ctx.selectedEntity.isValid())) {
                m_ctx.clipboardEntity = m_ctx.selectedEntity;
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, m_ctx.clipboardEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.clipboardEntity);
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_ctx.selectedEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.selectedEntity);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &m_ctx.hierarchyOpen);
            ImGui::MenuItem("Inspector", nullptr, &m_ctx.inspectorOpen);
            ImGui::MenuItem("Viewport",  nullptr, &m_ctx.viewportOpen);
            ImGui::MenuItem("Assets",    nullptr, &m_ctx.assetsOpen);
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
                if (ImGui::Button(reinterpret_cast<const char*>(u8"\u25B6 Play"), ImVec2(btnW, 0)))
                    enterPlayMode(world);
                ImGui::PopStyleColor(3);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.45f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.58f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.33f, 0.08f, 1.0f));
                if (m_isPaused) {
                    if (ImGui::Button(reinterpret_cast<const char*>(u8"\u25B6 Resume"), ImVec2(btnW, 0)))
                        m_isPaused = false;
                } else {
                    if (ImGui::Button(reinterpret_cast<const char*>(u8"\u23F8 Pause"), ImVec2(btnW, 0)))
                        m_isPaused = true;
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.12f, 0.12f, 1.0f));
                if (ImGui::Button(reinterpret_cast<const char*>(u8"\u25A0 Stop"), ImVec2(btnW, 0)))
                    exitPlayMode(world);
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
        m_currentProjectConfig.LastScene = (!ec && !rel.empty()) ? rel.string() : scenePath.string();
        ProjectManager pm;
        pm.SaveProjectFile(m_currentProjectConfig);
    }

    return true;
}

bool SceneEditor::saveSceneAs(ECS::World& world) {
    std::filesystem::path defaultPath;
    if (!m_currentProjectConfig.RootPath.empty()) {
        defaultPath = m_currentProjectConfig.RootPath / "scene.caf";
    } else {
        defaultPath = "scene.caf";
    }
    return saveScene(defaultPath.string().c_str(), world);
}

bool SceneEditor::loadScene(const char* path, ECS::World& world) {
    Editor::SceneSerializer serializer(world);
    if (!serializer.deserialize(path)) return false;
    m_ctx.currentScenePath = path;
    m_ctx.selectedEntity = ECS::Entity::INVALID;
    m_ctx.isDirty = false;
    return true;
}

// ── Close tab with confirmation ──────────────────────────────────

void SceneEditor::closeTab(int index) {
    if (index < 0 || index >= m_tabManager.tabCount()) return;
    auto& tab = m_tabManager.tab(index);
    if (tab.isDirty) {
        m_pendingCloseTab = index;
        ImGui::OpenPopup("Unsaved Tab?");
    } else {
        m_tabManager.closeScene(index);
    }
}

// ── Unsaved changes popup ───────────────────────────────────────

void SceneEditor::renderUnsavedChangesPopup(ECS::World& world) {
    // "Unsaved Changes?" — triggered by menu actions (New/Open/Exit)
    if (m_pendingAction != PendingAction::None) {
        if (ImGui::BeginPopupModal("Unsaved Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("There are unsaved changes. Do you want to save before continuing?");
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (m_ctx.currentScenePath.empty()) {
                    saveSceneAs(world);
                } else {
                    saveScene(m_ctx.currentScenePath.c_str(), world);
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
                // Save if we have a path, otherwise save-as with default name
                const char* savePath = tab.path.empty() ? "scene.caf" : tab.path.c_str();
                Editor::SceneSerializer serializer(*tab.world);
                if (serializer.serialize(savePath)) {
                    tab.isDirty = false;
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

void SceneEditor::executePendingAction(ECS::World& world) {
    (void)world;
    switch (m_pendingAction) {
        case PendingAction::NewScene:
            doNewScene();
            break;
        case PendingAction::OpenScene: {
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
        profile.animationTimelineOpen || profile.tilemapEditorOpen || profile.scriptEditorOpen) {
        ImGuiID dockBottomRegion;
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottomRegion, &dockCenter);
        
        if (profile.assetsOpen) ImGui::DockBuilderDockWindow("Asset Browser", dockBottomRegion);
        if (profile.consoleOpen) ImGui::DockBuilderDockWindow("Console", dockBottomRegion);
        if (profile.profilerOpen) ImGui::DockBuilderDockWindow("Profiler", dockBottomRegion);
        if (profile.animationTimelineOpen) ImGui::DockBuilderDockWindow("Animation Timeline", dockBottomRegion);
        if (profile.tilemapEditorOpen) ImGui::DockBuilderDockWindow("Tilemap Editor", dockBottomRegion);
        if (profile.scriptEditorOpen) ImGui::DockBuilderDockWindow("Script Editor", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Build & Run", dockBottomRegion);
        ImGui::DockBuilderDockWindow("Audio Preview", dockBottomRegion);
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
