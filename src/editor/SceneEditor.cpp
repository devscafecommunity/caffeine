#include "editor/SceneEditor.hpp"
#include "assets/MeshCache.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "terrain/TerrainCache.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "editor/ComponentRegistry.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/EditorShortcuts.hpp"
#include "editor/EditorTheme.hpp"
#include "debug/Profiler.hpp"
#include "debug/LogSystem.hpp"
#include "editor/PluginSystem.hpp"
#include "editor/PluginSymbolExports.hpp"
#include "render/GpuTextureCache.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EditorPaths.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/EnvironmentEffectsSystem.hpp"
#include "scene/PlayMode2D.hpp"
#include "navigation/NavigationSystem.hpp"
#include "effects/EffectSystem.hpp"
#include "editor/EditorCameraMath.hpp"
#include "procedural/ProceduralWorldSystem.hpp"
#include "script/ScriptTypes.hpp"
#if defined(CF_HAS_DRYAD)
#include "script/DryadHost.hpp"
#endif
#include "events/Events.hpp"
#include "input/InputManager.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/CameraComponents.hpp"
#include <algorithm>
#include <cmath>
#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#ifdef CF_HAS_IMGUI
#include <imgui_internal.h>

namespace Caffeine::Editor {

// ── Init / Shutdown ─────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneEditor::init(RHI::RenderDevice* device, Assets::AssetManager* assetManager,
                       const ProjectConfig& projectConfig) {
    m_renderDevice = device;
    m_photonPanel.setRenderDevice(device);
    m_photonPanel.setProcessedDirectory(projectConfig.AssetProcessedPath.string());
    if (!m_viewport.init(device)) return false;
    m_gameplayPreview.init(device);
    m_materialEditor.initGpu(device);
    m_assetBrowser.init(projectConfig);
    m_assetBrowser.initGpu(device);
    m_assetBrowser.setOnScriptOpen([this](const std::filesystem::path& path) {
#if defined(CF_HAS_DRYAD)
        if (Script::isDryadScriptPath(path.string())) {
            m_dryadNodeScript.open();
            m_dryadNodeScript.openFromScript(path);
            return;
        }
#endif
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

    EntityPresetRegistry::instance().registerBuiltIns();
    EntityPresetRegistry::instance().scanProject(m_currentProjectConfig.RootPath);
    m_entityPresets.setProjectRoot(m_currentProjectConfig.RootPath);
    m_entityPresets.setOnScriptOpen([this](const std::string& path) {
        m_scriptEditor.openFile(std::filesystem::path(path));
        m_scriptEditor.open();
    });
    m_hierarchy.setOpenPresetsCallback([this]() { m_entityPresets.open(); });
    m_tabManager.newScene("Untitled");

    m_commandPalette.registerCommand("panel_photon", "Photon GI", "Rendering", [this]() {
        UIKit::PanelRegistry::instance().open("photon.gi");
    });
    m_commandPalette.registerCommand("panel_terrain", "Terrain Editor", "World", [this]() {
        UIKit::PanelRegistry::instance().open("terrain.editor");
    });
    m_commandPalette.registerCommand("panel_hierarchy", "Hierarchy Panel", "Scene", [this]() {
        m_hierarchy.open();
    });
    m_commandPalette.registerCommand("panel_inspector", "Inspector Panel", "Scene", [this]() {
        m_inspector.open();
    });
    m_commandPalette.registerCommand("panel_console", "Console", "Debug", [this]() {
        m_console.open();
    });
    m_commandPalette.registerCommand("panel_profiler", "Profiler", "Debug", [this]() {
        m_profiler.open();
    });
    m_commandPalette.registerCommand("panel_entity_debugger", "Entity Debugger", "Debug", [this]() {
        m_entityDebugger.open();
    });
    m_commandPalette.registerCommand("panel_plugin_manager", "Plugin Manager", "Plugins", [this]() {
        PluginManager::instance().openManager();
    });
    m_commandPalette.registerCommand("panel_asset_browser", "Asset Browser", "Content", [this]() {
        m_assetBrowser.open();
    });
    m_commandPalette.registerCommand("panel_animation_timeline", "Animation Timeline", "Animation", [this]() {
        m_animationTimeline.open();
    });
    m_commandPalette.registerCommand("panel_animator_controller", "Animator Controller", "Animation", [this]() {
        m_animatorController.open();
    });
    m_commandPalette.registerCommand("panel_tilemap", "Tilemap Editor", "Content", [this]() {
        m_tilemapEditor.open();
    });
     m_commandPalette.registerCommand("panel_script_editor", "Script Editor", "Tools", [this]() {
         m_scriptEditor.open();
     });
#if defined(CF_HAS_DRYAD)
     m_commandPalette.registerCommand("panel_dryad_nodes", "Dryad Node Script", "Tools", [this]() {
         m_dryadNodeScript.open();
     });
#endif
     m_commandPalette.registerCommand("panel_material_editor", "Material Editor", "Content", [this]() {
         m_materialEditor.open();
     });
     m_commandPalette.registerCommand("panel_image_manager", "Image Manager", "Content", [this]() {
         m_imageManager.open();
     });
     m_commandPalette.registerCommand("panel_hud_editor", "HUD Editor", "Tools", [this]() {
         m_hudEditor.open();
     });
     m_commandPalette.registerCommand("panel_particles", "Particles", "Tools", [this]() {
         m_particleEditor.open();
     });
     m_commandPalette.registerCommand("panel_effects", "Effects", "Tools", [this]() {
         m_effectEditor.open();
     });
     m_commandPalette.registerCommand("panel_entity_presets", "Entity Presets", "Scene", [this]() {
         m_entityPresets.open();
     });
     m_commandPalette.registerCommand("panel_gameplay_preview", "Gameplay Preview", "Scene", [this]() {
         m_gameplayPreview.open();
     });
     m_commandPalette.registerCommand("panel_settings", "Settings", "Tools", [this]() {
         m_settingsPanel.open();
     });
    m_commandPalette.registerCommand("panel_viewport", "Scene Viewport", "Scene", [this]() {
        m_viewport.open();
    });
    m_commandPalette.registerCommand("panel_toolbox", "Toolbox", "Scene", [this]() {
        m_toolbox.open();
    });
    m_commandPalette.registerCommand("panel_audio_preview", "Audio Preview", "Content", [this]() {
        m_audioPreview.open();
    });
    m_commandPalette.registerCommand("panel_camera_preview", "Camera Preview", "Scene", [this]() {
        m_cameraPreview.open();
    });
    m_commandPalette.registerCommand("action_build", "Build and Run", "Actions", [this]() {
        m_buildDialog.open();
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

    EditorPaths::init();
    const std::filesystem::path pluginsDir =
        projectConfig.RootPath.empty()
            ? (std::filesystem::current_path() / "plugins")
            : (projectConfig.RootPath / "plugins");
    const std::filesystem::path bundledPluginsDir = EditorPaths::bundledPluginsDirectory();
    anchorPluginHostSymbols();
    // Component types must exist before plugins load: plugins resolve them by
    // name to register Inspector drawers.
    registerAllComponents(ComponentRegistry::instance());
    registerPanels();
    PluginManager::instance().initialize(pluginsDir, &m_inspector, &m_commandPalette, &m_ctx,
                                         bundledPluginsDir);
    // Plugins contributed panels during initialize; treat that as the baseline
    // so the first frame does not rebuild an already persisted docking layout.
    m_panelsRevision = m_panels.revision();

    {
        // Precedence: user layout > project layout > bundled default. A first
        // launch loads the project/bundled document but writes user changes to
        // the per-user config file, never back into the asset tree.
        const std::filesystem::path userWorkspace(UIKit::Workspace::userConfigPath());
        std::filesystem::path workspacePath = userWorkspace;
        if (!std::filesystem::exists(workspacePath)) {
            workspacePath = m_currentProjectConfig.RootPath / "ui" / "workspace.json";
        }
        if (!std::filesystem::exists(workspacePath)) {
            workspacePath = EditorPaths::resolve("ui/workspace.json");
        }
        std::string workspaceError;
        if (!m_workspace.load(workspacePath.string(), &workspaceError)) {
            std::fprintf(stderr, "[workspace] %s (using defaults)\n", workspaceError.c_str());
        }
    }

     m_inspector.open();
     m_assetBrowser.open();

     // Register layout change callback
     m_settingsPanel.setLayoutChangeCallback([this]() {
         const auto& profile = m_settingsPanel.layoutManager().currentProfile();
         m_workspace.config().leftSize = profile.hierarchyWidth;
         m_workspace.config().rightSize = profile.inspectorWidth;
         m_workspace.saveTo(UIKit::Workspace::userConfigPath());
         requestLayoutRebuild();
     });
     m_settingsPanel.setEditorContext(&m_ctx);
     m_settingsPanel.applyPreferencesToContext(m_ctx);

    // Always open the project's scene in the viewport, in that project's 2D or 3D mode.
    if (projectConfig.TemplateType == "2D") {
        m_ctx.viewMode = EditorContext::ViewMode::Mode2D;
    } else if (projectConfig.TemplateType == "3D") {
        m_ctx.viewMode = EditorContext::ViewMode::Mode3D;
    }
    {
        std::string lastScene = projectConfig.LastScene.empty() ? std::string("scenes/main.caf")
                                                                : projectConfig.LastScene;
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
            m_pendingStartupScene = scenePath.string();
        }
    }

#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine::InitParams scriptParams;
    scriptParams.world  = nullptr;
    scriptParams.events = &m_eventBus;
    scriptParams.input  = &m_input;
    scriptParams.ui     = &m_uiSystem;
    if (!m_scriptEngineReady) {
        m_scriptEngineReady = m_scriptEngine.init(scriptParams);
    }
     m_ctx.scriptEngine = &m_scriptEngine;
     m_scriptEngine.setSearchRoot(m_currentProjectConfig.RootPath.string());
     m_scriptEditor.setScriptEngine(&m_scriptEngine);
     if (m_scriptEngineReady) {
         m_scriptSystem = Script::ScriptSystem(&m_scriptEngine, &m_eventBus);
         m_scriptSystem.setProjectRoot(m_currentProjectConfig.RootPath.string());
     }
#endif

    {
        int index = 0;
        for (const auto& entry : ComponentRegistry::instance().entries()) {
            const std::string id = "add_component_" + std::to_string(index++);
            const std::string label = std::string("Add ") + entry.name;
            m_commandPalette.registerCommand(
                Caffeine::FixedString<64>(id.c_str()), Caffeine::FixedString<128>(label.c_str()),
                Caffeine::FixedString<128>("Components"),
                [this, entry]() {
                    ECS::World* world = m_tabManager.activeWorld();
                    if (!world) return;
                    // Quick Search can add a component without a selection: create
                    // and select a dedicated entity so the action is never a no-op.
                    ECS::Entity target = m_ctx.selectedEntity;
                    if (!target.isValid()) {
                        target = world->create(entry.name.c_str());
                        m_ctx.selectEntity(target);
                    }
                    if (entry.has(*world, target)) return;
                    m_ctx.beginUndo(EditorCommand::AddComponent, target.id(), *world);
                    entry.add(*world, target);
                    m_ctx.endUndo(*world);
                },
                true, entry.plugin.empty() ? nullptr : entry.plugin.c_str(),
                entry.keywords.empty() ? nullptr : entry.keywords.c_str());
        }
    }
    registerPlayModeEventListeners();

    Debug::LogSystem::instance().addSink([this](Debug::LogLevel level, const char* category, const char* message) {
        m_console.addLog(level, category ? category : "", message ? message : "");
    });

    return true;
}

void SceneEditor::shutdown() {
    PluginManager::instance().shutdown();
    m_panelsRegistered = false;
    UIKit::PanelRegistry::instance().clear();
#ifdef CF_HAS_SDL3
    if (m_renderDevice) {
        m_viewport.shutdown();
        m_gameplayPreview.shutdown();
        m_cameraPreview.shutdownGpu();
        m_materialEditor.shutdownGpu();
        m_assetBrowser.shutdownGpu();
        EditorIcons::shutdown();
        Render::GpuTextureCache::instance().releaseAll(m_renderDevice);
        Assets::MeshCache::getInstance().releaseGpuResources(m_renderDevice);
        Render::GpuProceduralMeshes::releaseGpuResources(m_renderDevice);
        Terrain::TerrainCache::instance().releaseGpuResources(m_renderDevice);
    }
#endif
    Terrain::TerrainCache::instance().clear();
    m_tabManager.clearAll();
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
            snap.hasTransform = true;
            snap.px = pos.position.x; snap.py = pos.position.y; snap.pz = pos.position.z;
            snap.rz = pos.rotation.z;
            m_playSnapshot.push_back(snap);
        });
    ECS::ComponentQuery q3;
    q3.with<ECS::Position3D>();
    world.forEach<ECS::Position3D>(q3,
        [&](ECS::Entity e, ECS::Position3D& p) {
            EntitySnapshot snap;
            snap.id = e.id();
            snap.hasPos3 = true;
            snap.px = p.position.x; snap.py = p.position.y; snap.pz = p.position.z;
            if (auto* r = world.get<ECS::Rotation3D>(e)) {
                snap.hasRot3 = true;
                snap.qx = r->quaternion.x;
                snap.qy = r->quaternion.y;
                snap.qz = r->quaternion.z;
                snap.qw = r->quaternion.w;
            }
            m_playSnapshot.push_back(snap);
        });
    m_isPlaying = true;
    m_isPaused  = false;

    m_playCamera2DFollowsCamera = false;
    if (m_ctx.viewMode == EditorContext::ViewMode::Mode2D) {
        Scene::propagateTransforms(world);
        const ECS::Entity cameraEntity = Scene::findActiveCamera2DEntity(world);
        if (cameraEntity.isValid()) {
            if (auto* camera = world.get<ECS::Camera2DComponent>(cameraEntity)) {
                if (!world.has<Scene::Parent>(cameraEntity)) {
                    ECS::Entity followTarget;
                    ECS::ComponentQuery playerQ;
                    playerQ.with<Script::ScriptComponent>();
                    world.forEach<Script::ScriptComponent>(playerQ,
                        [&](ECS::Entity e, Script::ScriptComponent&) {
                            if (!followTarget.isValid()) followTarget = e;
                        });
                    if (!followTarget.isValid()) {
                        ECS::ComponentQuery spriteQ;
                        spriteQ.with<ECS::Sprite>();
                        world.forEach<ECS::Sprite>(spriteQ, [&](ECS::Entity e, ECS::Sprite&) {
                            if (!followTarget.isValid()) followTarget = e;
                        });
                    }
                    if (followTarget.isValid()) {
                        m_playCamera2D.setZoom(camera->zoom);
                        m_playCamera2D.follow(followTarget, 0.18f);
                        m_playCamera2DFollowsCamera = true;
                    }
                } else {
                    Scene::syncViewportFromCamera2D(world, cameraEntity, m_ctx, *camera);
                }
            }
        }
    }

    CF_INFO("Play", "Simulation started");
#ifdef CF_HAS_SCRIPTING
    m_scriptEngine.setWorld(&world);
    m_scriptEngine.setInput(&m_input);
    m_scriptEngine.setSearchRoot(m_currentProjectConfig.RootPath.string());
    if (!m_scriptEngineReady) {
        Script::ScriptEngine::InitParams p;
        p.world  = &world;
        p.events = &m_eventBus;
        p.input  = &m_input;
        p.ui     = &m_uiSystem;
        m_scriptEngineReady = m_scriptEngine.init(p);
    }
    m_scriptSystem = Script::ScriptSystem(&m_scriptEngine, &m_eventBus);
    m_scriptSystem.setProjectRoot(m_currentProjectConfig.RootPath.string());
    m_scriptSystem.resetPlayState();
    Procedural::ProceduralWorldSystem::reset();
    ECS::ComponentQuery cppQ;
    cppQ.with<Script::CppScriptComponent>();
    world.forEach<Script::CppScriptComponent>(cppQ,
        [](ECS::Entity, Script::CppScriptComponent& csc) {
            csc.initialized = false;
        });
#endif
}

void SceneEditor::exitPlayMode(ECS::World& world) {
    m_physics3D.reset();
    m_isPlaying = false;
    m_isPaused  = false;
    m_ctx.isPlayMode = false;
#ifdef CF_HAS_IMGUI
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#endif
    m_ctx.viewportPanX = m_viewportPlaySnapshot.panX;
    m_ctx.viewportPanY = m_viewportPlaySnapshot.panY;
    m_ctx.viewportZoom = m_viewportPlaySnapshot.zoom;
    m_playCamera2D.stopFollowing();
    m_playCamera2DFollowsCamera = false;

    for (auto& snap : m_playSnapshot) {
        ECS::Entity e(snap.id, &world);
        if (!e.isValid()) continue;
        if (snap.hasTransform) {
            if (auto* pos = world.get<ECS::Transform>(e)) {
                pos->position.x = snap.px;
                pos->position.y = snap.py;
                pos->position.z = snap.pz;
                pos->rotation.z = snap.rz;
            }
        }
        if (snap.hasPos3) {
            if (auto* p = world.get<ECS::Position3D>(e)) {
                p->position = Vec3(snap.px, snap.py, snap.pz);
            }
        }
        if (snap.hasRot3) {
            if (auto* r = world.get<ECS::Rotation3D>(e)) {
                r->quaternion = Vec4(snap.qx, snap.qy, snap.qz, snap.qw);
            }
        }
    }
    m_playSnapshot.clear();
#ifdef CF_HAS_SCRIPTING
    m_scriptSystem.resetPlayState();
#endif
    Procedural::ProceduralWorldSystem::reset();
}

void SceneEditor::tickSystems(ECS::World& world, f32 dt) {
    Animation::setSkinProjectRoot(m_ctx.projectRootPath.string());
    const Vec3 editorCam =
        editorCameraPosition(m_ctx.camYaw, m_ctx.camPitch, m_ctx.camDistance, m_ctx.camFocus);
    Scene::tickEnvironmentEffects(world, (m_isPlaying && !m_isPaused) ? dt : 0.0f,
                                  Scene::firstCamera3DPosition(world, editorCam));
    if (!m_isPlaying || m_isPaused) {
        Scene::propagateTransforms(world);
        return;
    }

    {
        auto& io = ImGui::GetIO();
        if (m_isPlaying) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    }

    m_input.beginFrame();
    {
        auto& io = ImGui::GetIO();
        const bool typing = io.WantTextInput;
#ifdef CF_HAS_SDL3
        int keyCount = 0;
        const bool* ks = SDL_GetKeyboardState(&keyCount);
        const int limit = std::min(keyCount, static_cast<int>(Input::Key::KeyCount));
        if (!typing && ks) {
            for (int i = 0; i < limit; ++i) {
                const auto key = static_cast<Input::Key>(i);
                if (ks[i]) m_input.injectKeyDown(key);
                else m_input.injectKeyUp(key);
            }
        } else {
            for (int i = 0; i < static_cast<int>(Input::Key::KeyCount); ++i) {
                m_input.injectKeyUp(static_cast<Input::Key>(i));
            }
        }

        float mx = 0.0f, my = 0.0f;
        const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(&mx, &my);
        m_input.injectMouseMove(mx, my);
        auto syncMouse = [&](SDL_MouseButtonFlags mask, Input::MouseButton button) {
            if (mouseButtons & mask) m_input.injectMouseButtonDown(button);
            else m_input.injectMouseButtonUp(button);
        };
        syncMouse(SDL_BUTTON_LMASK, Input::MouseButton::Left);
        syncMouse(SDL_BUTTON_MMASK, Input::MouseButton::Middle);
        syncMouse(SDL_BUTTON_RMASK, Input::MouseButton::Right);

        float stickLX = 0.0f, stickLY = 0.0f, stickRX = 0.0f, stickRY = 0.0f;
        int padCount = 0;
        SDL_JoystickID* pads = SDL_GetGamepads(&padCount);
        if (pads && padCount > 0) {
            SDL_Gamepad* gp = SDL_GetGamepadFromID(pads[0]);
            if (!gp) gp = SDL_OpenGamepad(pads[0]);
            if (gp) {
                auto axis = [&](SDL_GamepadAxis a) -> f32 {
                    const f32 v = static_cast<f32>(SDL_GetGamepadAxis(gp, a)) / 32767.0f;
                    return (std::abs(v) < 0.18f) ? 0.0f : v;
                };
                stickLX = axis(SDL_GAMEPAD_AXIS_LEFTX);
                stickLY = -axis(SDL_GAMEPAD_AXIS_LEFTY);
                stickRX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
                stickRY = axis(SDL_GAMEPAD_AXIS_RIGHTY);
                auto syncBtn = [&](SDL_GamepadButton b, Input::GamepadButton ib) {
                    if (SDL_GetGamepadButton(gp, b)) m_input.injectGamepadButtonDown(ib);
                    else m_input.injectGamepadButtonUp(ib);
                };
                syncBtn(SDL_GAMEPAD_BUTTON_SOUTH, Input::GamepadButton::A);
                syncBtn(SDL_GAMEPAD_BUTTON_EAST, Input::GamepadButton::B);
                syncBtn(SDL_GAMEPAD_BUTTON_WEST, Input::GamepadButton::X);
                syncBtn(SDL_GAMEPAD_BUTTON_NORTH, Input::GamepadButton::Y);
                syncBtn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Input::GamepadButton::LeftBumper);
                syncBtn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Input::GamepadButton::RightBumper);
            }
        }
        if (pads) SDL_free(pads);

        float relX = 0.0f, relY = 0.0f;
        SDL_GetRelativeMouseState(&relX, &relY);
        const f32 lookX = (std::abs(relX) > std::abs(io.MouseDelta.x)) ? relX : io.MouseDelta.x;
        const f32 lookY = (std::abs(relY) > std::abs(io.MouseDelta.y)) ? relY : io.MouseDelta.y;
        m_input.injectGamepadAxis(Input::GamepadAxis::LeftX, stickLX);
        m_input.injectGamepadAxis(Input::GamepadAxis::LeftY, stickLY);
        m_input.injectGamepadAxis(Input::GamepadAxis::RightX, stickRX + lookX * 0.12f);
        m_input.injectGamepadAxis(Input::GamepadAxis::RightY, stickRY + lookY * 0.12f);
#else
        if (!typing) {
            const ImGuiKey keys[] = {
                ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F, ImGuiKey_G,
                ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N,
                ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T, ImGuiKey_U,
                ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z, ImGuiKey_Space,
                ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
                ImGuiKey_Escape, ImGuiKey_Enter, ImGuiKey_Tab
            };
            const Input::Key mapped[] = {
                Input::Key::A, Input::Key::B, Input::Key::C, Input::Key::D, Input::Key::E, Input::Key::F,
                Input::Key::G, Input::Key::H, Input::Key::I, Input::Key::J, Input::Key::K, Input::Key::L,
                Input::Key::M, Input::Key::N, Input::Key::O, Input::Key::P, Input::Key::Q, Input::Key::R,
                Input::Key::S, Input::Key::T, Input::Key::U, Input::Key::V, Input::Key::W, Input::Key::X,
                Input::Key::Y, Input::Key::Z, Input::Key::Space, Input::Key::Up, Input::Key::Down,
                Input::Key::Left, Input::Key::Right, Input::Key::LShift, Input::Key::RShift,
                Input::Key::LCtrl, Input::Key::RCtrl, Input::Key::Escape, Input::Key::Return,
                Input::Key::Tab
            };
            for (int i = 0; i < static_cast<int>(sizeof(keys) / sizeof(keys[0])); ++i) {
                if (ImGui::IsKeyDown(keys[i])) m_input.injectKeyDown(mapped[i]);
                else m_input.injectKeyUp(mapped[i]);
            }
        }
        m_input.injectMouseMove(io.MousePos.x, io.MousePos.y);
        if (io.MouseDown[0]) m_input.injectMouseButtonDown(Input::MouseButton::Left);
        else m_input.injectMouseButtonUp(Input::MouseButton::Left);
        if (io.MouseDown[2]) m_input.injectMouseButtonDown(Input::MouseButton::Middle);
        else m_input.injectMouseButtonUp(Input::MouseButton::Middle);
        if (io.MouseDown[1]) m_input.injectMouseButtonDown(Input::MouseButton::Right);
        else m_input.injectMouseButtonUp(Input::MouseButton::Right);
        m_input.injectGamepadAxis(Input::GamepadAxis::RightX, io.MouseDelta.x * 0.12f);
        m_input.injectGamepadAxis(Input::GamepadAxis::RightY, io.MouseDelta.y * 0.12f);
#endif
    }
    m_input.endFrame();

    m_animationSystem.onUpdate(world, dt);
    Effects::tickEffects(world, dt, editorCameraPosition(m_ctx.camYaw, m_ctx.camPitch, m_ctx.camDistance, m_ctx.camFocus));
    Navigation::updateNavigation(world, dt);
    m_physicsSystem.onUpdate(world, dt);
    m_physics3D.onUpdate(world, dt);
#ifdef CF_HAS_SCRIPTING
    if (m_scriptEngineReady) {
        m_scriptEngine.setWorld(&world);
        m_scriptSystem.onUpdate(world, dt);
    Procedural::ProceduralWorldSystem::update(world);
    }
#endif
    {
        auto& io = ImGui::GetIO();
        m_uiSystem.injectMousePosition({io.MousePos.x, io.MousePos.y});
        m_uiSystem.injectMouseClick(io.MouseDown[0]);
    }
    m_uiSystem.onUpdate(world, dt);

    if (m_ctx.viewMode == EditorContext::ViewMode::Mode2D) {
        Scene::propagateTransforms(world);
        const ECS::Entity cameraEntity = Scene::findActiveCamera2DEntity(world);
        if (cameraEntity.isValid()) {
            const auto* camera = world.get<ECS::Camera2DComponent>(cameraEntity);
            if (camera) {
                if (m_playCamera2DFollowsCamera) {
                    m_playCamera2D.update(dt, world);
                    if (auto* camTransform = world.get<ECS::Transform>(cameraEntity)) {
                        const Vec2 camPos = m_playCamera2D.position();
                        camTransform->position.x = camPos.x;
                        camTransform->position.y = camPos.y;
                    }
                }
                Scene::syncViewportFromCamera2D(world, cameraEntity, m_ctx, *camera);
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

namespace {

ImGuiID g_windowChromeArmed = 0;

void drawWindowChromeGlyph(ImDrawList* drawList, const ImRect& hit, bool dockBack) {
    const ImU32 col = IM_COL32(230, 230, 230, 235);
    const float inset = ImMax(2.0f, hit.GetWidth() * 0.22f);
    const ImVec2 a = hit.Min + ImVec2(inset, inset + 1.0f);
    const ImVec2 b = hit.Max - ImVec2(inset + 1.0f, inset);
    drawList->AddRect(a, b, col, 1.0f, 0, 1.0f);
    const ImVec2 tip = dockBack
        ? ImVec2(a.x + 1.0f, b.y - 1.0f)
        : ImVec2(hit.Max.x - inset, hit.Min.y + inset);
    const ImVec2 tail = dockBack
        ? tip + ImVec2(4.0f, -4.0f)
        : tip + ImVec2(-4.0f, 4.0f);
    drawList->AddLine(tail, tip, col, 1.0f);
    if (dockBack) {
        drawList->AddLine(tip, tip + ImVec2(3.0f, 0.0f), col, 1.0f);
        drawList->AddLine(tip, tip + ImVec2(0.0f, -3.0f), col, 1.0f);
    } else {
        drawList->AddLine(tip, tip + ImVec2(-3.0f, 0.0f), col, 1.0f);
        drawList->AddLine(tip, tip + ImVec2(0.0f, 3.0f), col, 1.0f);
    }
}

bool windowChromeButton(ImGuiID id, const ImRect& hit, bool dockBack, const char* tip,
                        ImGuiViewport* viewport) {
    ImGuiContext& g = *GImGui;
    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
    const bool hovered = hit.Contains(g.IO.MousePos);
    if (hovered)
        drawList->AddRectFilled(hit.Min, hit.Max, IM_COL32(255, 255, 255, 36), 2.0f);
    drawWindowChromeGlyph(drawList, hit, dockBack);
    if (hovered)
        ImGui::SetTooltip("%s", tip);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        g_windowChromeArmed = id;
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) || g_windowChromeArmed != id)
        return false;
    g_windowChromeArmed = 0;
    const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    return hovered && std::fabs(drag.x) < 4.0f && std::fabs(drag.y) < 4.0f;
}

bool windowIsOwnOsWindow(const ImGuiWindow* window) {
    if (!window || !window->Viewport || window->DockIsActive) return false;
    return window->ViewportOwned && window->Viewport->ID != ImGui::GetMainViewport()->ID;
}

void popWindowToMonitor(ImGuiWindow* window) {
    if (!window) return;
    window->WindowClass.ViewportFlagsOverrideSet |= ImGuiViewportFlags_NoAutoMerge;
    if (window->DockIsActive)
        ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), window);
}

void dockWindowBack(ImGuiWindow* window, ImGuiID dockspaceId) {
    if (!window || dockspaceId == 0) return;
    window->WindowClass.ViewportFlagsOverrideSet &= ~ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetWindowDock(window, dockspaceId, ImGuiCond_Always);
}

void renderIndependentWindowButtons(ImGuiID dockspaceId) {
    ImGuiContext& g = *GImGui;
    const float buttonSz = g.FontSize;
    const ImVec2 framePad = g.Style.FramePadding;

    for (int n = 0; n < g.DockContext.Nodes.Data.Size; ++n) {
        ImGuiDockNode* node = static_cast<ImGuiDockNode*>(g.DockContext.Nodes.Data[n].val_p);
        if (!node || !node->IsVisible || !node->IsLeafNode() || node->LastFrameActive < g.FrameCount)
            continue;
        ImGuiWindow* window = node->VisibleWindow;
        if (!window || window->Flags & ImGuiWindowFlags_NoDocking)
            continue;

        ImRect hit;
        if (node->TabBar && !node->IsHiddenTabBar()) {
            const ImRect tab = window->DC.DockTabItemRect;
            if (tab.GetWidth() < buttonSz || tab.GetHeight() < 4.0f)
                continue;
            const float closeX = ImMax(tab.Min.x, tab.Max.x - framePad.x - buttonSz);
            hit = ImRect(ImVec2(closeX - buttonSz - 1.0f, tab.Min.y + framePad.y),
                         ImVec2(closeX - 1.0f, tab.Min.y + framePad.y + buttonSz));
            if (!node->TabBar->BarRect.Contains(hit.GetCenter()))
                continue;
        } else {
            hit = ImRect(ImVec2(node->Pos.x + node->Size.x - buttonSz - 8.0f, node->Pos.y + 4.0f),
                         ImVec2(node->Pos.x + node->Size.x - 8.0f, node->Pos.y + 4.0f + buttonSz));
        }

        ImGuiViewport* viewport = node->HostWindow ? node->HostWindow->Viewport
                                                   : ImGui::GetMainViewport();
        if (windowChromeButton(window->ID ^ 0x510u, hit, false,
                "Abre esta janela noutro monitor. Continua ligada a mesma cena.", viewport))
            popWindowToMonitor(window);
    }

    for (ImGuiWindow* window : g.Windows) {
        if (!window->WasActive || window->Hidden || !windowIsOwnOsWindow(window))
            continue;
        if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip |
                             ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoDocking))
            continue;
        const ImRect title = window->TitleBarRect();
        if (title.GetHeight() < 4.0f)
            continue;
        const float closeX = title.Max.x - framePad.x - buttonSz;
        const ImRect hit(ImVec2(closeX - g.Style.ItemInnerSpacing.x - buttonSz, title.Min.y + framePad.y),
                         ImVec2(closeX - g.Style.ItemInnerSpacing.x, title.Min.y + framePad.y + buttonSz));
        if (windowChromeButton(window->ID ^ 0xA11u, hit, true,
                "Encaixa outra vez na janela principal.", window->Viewport))
            dockWindowBack(window, dockspaceId);
    }
}

}  // namespace

#endif

// ── Main render ─────────────────────────────────────────────────

void SceneEditor::processDeferredTasks() {
    if (auto* world = m_tabManager.activeWorld()) {
        m_photonPanel.processPendingBake(*world, m_ctx);
    }
}

void SceneEditor::render(f32 deltaTime) {
    CF_PROFILE_SCOPE("SceneEditor::render");
    if (!m_open) return;

    ECS::World* activeWorld = m_tabManager.activeWorld();
    m_ctx.activeWorld = activeWorld;
    m_ctx.projectRootPath = m_currentProjectConfig.RootPath;
    m_ctx.assetRootPath = m_assetBrowser.assetRoot();
    if (!activeWorld) {
        renderUnsavedChangesPopup(nullptr);
        return;
    }

    if (!m_pendingStartupScene.empty()) {
        const std::string scenePath = m_pendingStartupScene;
        m_pendingStartupScene.clear();
        if (loadScene(scenePath.c_str(), *activeWorld)) {
            m_tabManager.activeTab().name = std::filesystem::path(scenePath).stem().string();
            m_tabManager.activeTab().path = scenePath;
        }
        if (m_currentProjectConfig.TemplateType != "2D" &&
            m_currentProjectConfig.TemplateType != "3D") {
            bool has3D = false;
            bool has2D = false;
            ECS::ComponentQuery cameras3D;
            cameras3D.with<ECS::Camera3DComponent>();
            activeWorld->forEach<ECS::Camera3DComponent>(
                cameras3D, [&](ECS::Entity, ECS::Camera3DComponent&) { has3D = true; });
            ECS::ComponentQuery meshes;
            meshes.with<ECS::MeshFilterComponent>();
            activeWorld->forEach<ECS::MeshFilterComponent>(
                meshes, [&](ECS::Entity, ECS::MeshFilterComponent&) { has3D = true; });
            ECS::ComponentQuery cameras2D;
            cameras2D.with<ECS::Camera2DComponent>();
            activeWorld->forEach<ECS::Camera2DComponent>(
                cameras2D, [&](ECS::Entity, ECS::Camera2DComponent&) { has2D = true; });
            ECS::ComponentQuery sprites;
            sprites.with<ECS::Sprite>();
            activeWorld->forEach<ECS::Sprite>(sprites, [&](ECS::Entity, ECS::Sprite&) { has2D = true; });
            m_ctx.viewMode = (has2D && !has3D) ? EditorContext::ViewMode::Mode2D
                                               : EditorContext::ViewMode::Mode3D;
        }
        ImGui::SetWindowFocus("Scene Viewport");
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

    if (m_workspace.poll()) m_layoutNeedsRebuild = true;
    if (m_panels.revision() != m_panelsRevision) {
        m_panelsRevision = m_panels.revision();
        m_layoutNeedsRebuild = true;
    }

    if (!m_dockingSetup || m_layoutNeedsRebuild) {
        const auto& profile = m_settingsPanel.layoutManager().currentProfile();
        // Respect the docking persisted in the user imgui.ini. Rebuild only when
        // there is no split layout yet or the user asked for a reload/reset.
        // (DockSpace() creates a bare central node even on first launch, so a
        // non-null node alone is not enough to detect a saved layout.)
        ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(m_dockspaceId);
        const bool hasExistingLayout = rootNode != nullptr && rootNode->IsSplitNode();
        if (m_layoutNeedsRebuild || !hasExistingLayout) {
            applyLayoutProfile(m_dockspaceId, profile);
            m_selectDefaultTabs = true;
        }
        m_layoutNeedsRebuild = false;
        m_dockingSetup = true;
    }

    renderMainMenuBar(*activeWorld);

    {
        CF_PROFILE_SCOPE("SceneEditor::propagateTransforms");
        Scene::propagateTransforms(*activeWorld);
    }

    renderPlaybar(*activeWorld);
    if (m_ctx.selectedEntity.isValid()) {
        Animation::ensurePoseForMesh(*activeWorld, m_ctx.selectedEntity,
                                     m_ctx.projectRootPath.string());
    }
    if (!m_isPlaying || m_isPaused) {
        f32 skinDt = 0.0f;
        if (m_animationTimeline.isPlaying()) {
            skinDt = deltaTime;
        } else {
            ECS::ComponentQuery playing;
            playing.with<Animation::SkinnedPose>();
            activeWorld->forEach<Animation::SkinnedPose>(
                playing, [&](ECS::Entity, Animation::SkinnedPose& pose) {
                    if (pose.playing && !pose.timelineHold) skinDt = deltaTime;
                });
        }
        Animation::tickSkinnedPoses(*activeWorld, skinDt);
    }

    // Panels render through the registry. Dock layout and visibility belong to
    // PanelRegistry / Workspace, so core and plugin panels share one path.
    m_viewport.setFrameCommandBuffer(m_frameCmd);
#ifdef CF_HAS_SDL3
    m_gameplayPreview.setFrameCommandBuffer(m_frameCmd);
    m_materialEditor.setFrameCommandBuffer(m_frameCmd);
    m_assetBrowser.setFrameCommandBuffer(m_frameCmd);
#endif
    m_panels.renderAll();
    m_viewport.setFrameCommandBuffer(nullptr);
#ifdef CF_HAS_SDL3
    m_gameplayPreview.setFrameCommandBuffer(nullptr);
    m_materialEditor.setFrameCommandBuffer(nullptr);
    m_assetBrowser.setFrameCommandBuffer(nullptr);
#endif

    m_commandPalette.render();
    {
        CF_PROFILE_SCOPE("SceneEditor::plugins");
        PluginManager::instance().renderPanels();
    }

    if (m_selectDefaultTabs) {
        // Fresh layouts should open on the primary panel of each dock area,
        // not whichever window happened to be submitted last.
        ImGui::SetWindowFocus("Hierarchy");
        ImGui::SetWindowFocus("Scene Viewport");
        ImGui::SetWindowFocus("Asset Browser");
        m_selectDefaultTabs = false;
    }

    serviceBrowseSession(m_ctx, m_assetBrowser);

    renderIndependentWindowButtons(m_dockspaceId);

    ImGui::End(); // DockSpace

    renderUnsavedChangesPopup(activeWorld);
    renderStatusBar(*activeWorld);
    m_profiler.pushFrameTime(deltaTime * 1000.0f);
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
            if (EditorIcons::menuItem(EditorIcon::Undo, "Undo", EditorShortcuts::instance().chord(Shortcut::Undo), false, m_ctx.undoStack.canUndo())) {
                m_ctx.undoStack.undo(world);
                if (!world.isEntityAlive(m_ctx.selectedEntity)) m_ctx.selectedEntity = ECS::Entity::INVALID;
            }
            if (EditorIcons::menuItem(EditorIcon::Redo, "Redo", EditorShortcuts::instance().chord(Shortcut::Redo), false, m_ctx.undoStack.canRedo())) {
                m_ctx.undoStack.redo(world);
                if (!world.isEntityAlive(m_ctx.selectedEntity)) m_ctx.selectedEntity = ECS::Entity::INVALID;
            }
            ImGui::Separator();
            if (EditorIcons::menuItem(EditorIcon::Copy, "Copy", EditorShortcuts::instance().chord(Shortcut::Copy), false, m_ctx.selectedEntity.isValid())) {
                m_ctx.clipboardEntity = m_ctx.selectedEntity;
            }
            if (EditorIcons::menuItem(EditorIcon::Paste, "Paste", EditorShortcuts::instance().chord(Shortcut::Paste), false, m_ctx.clipboardEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.clipboardEntity);
            }
            if (EditorIcons::menuItem(EditorIcon::Duplicate, "Duplicate", EditorShortcuts::instance().chord(Shortcut::Duplicate), false, m_ctx.selectedEntity.isValid())) {
                m_hierarchy.duplicateEntity(world, m_ctx.selectedEntity);
            }
            PluginManager::instance().renderMenuItems("Edit");
            ImGui::EndMenu();
        }

        if (PluginManager::instance().hasMenuItems("Help") && ImGui::BeginMenu("Help")) {
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
            PluginManager::instance().renderMenuItems("Plugins");
            const auto& pluginPanels = PluginManager::instance().panels();
            if (!pluginPanels.empty()) {
                ImGui::Separator();
                for (const auto& panel : pluginPanels) {
                    if (!PluginManager::instance().isContributionEnabled(panel.pluginName)) {
                        continue;
                    }
                    bool open = panel.open;
                    if (ImGui::MenuItem(panel.title.c_str(), nullptr, &open)) {
                        if (open) {
                            PluginManager::instance().openPanel(panel.title);
                        } else {
                            PluginManager::instance().closePanel(panel.title);
                        }
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Panels")) {
                m_panels.drawMenu();
                ImGui::EndMenu();
            }
            PluginManager::instance().renderMenuItems("View");
            syncLayoutProfileFromPanels();
            ImGui::Separator();
            if (ImGui::MenuItem("Photon Global Illumination")) {
                UIKit::PanelRegistry::instance().open("photon.gi");
            }
            if (ImGui::MenuItem("Terrain Editor")) {
                UIKit::PanelRegistry::instance().open("terrain.editor");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reload UI theme", "F5")) {
                EditorTheme::apply({EditorTheme::darkMode(), EditorTheme::fontSize()});
            }
            if (ImGui::MenuItem("Reload UI layout")) {
                const std::filesystem::path userWorkspace(UIKit::Workspace::userConfigPath());
                const std::string source =
                    std::filesystem::exists(userWorkspace) ? userWorkspace.string()
                                                           : m_workspace.path();
                if (!source.empty()) {
                    std::string error;
                    if (!m_workspace.load(source, &error)) {
                        std::fprintf(stderr, "[workspace] %s\n", error.c_str());
                    }
                }
                m_layoutNeedsRebuild = true;
            }
            if (ImGui::MenuItem("Save UI layout")) {
                if (m_workspace.saveTo(UIKit::Workspace::userConfigPath())) {
                    std::fprintf(stderr, "[workspace] saved %s\n",
                                 UIKit::Workspace::userConfigPath().c_str());
                }
            }
            if (ImGui::MenuItem("Reset UI layout")) {
                m_workspace.reset();
                m_workspace.saveTo(UIKit::Workspace::userConfigPath());
                m_layoutNeedsRebuild = true;
            }
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
    if (ImGui::IsKeyPressed(ImGuiKey_F5) && !ImGui::GetIO().WantTextInput) {
        EditorTheme::apply({EditorTheme::darkMode(), EditorTheme::fontSize()});
        const std::filesystem::path userWorkspace(UIKit::Workspace::userConfigPath());
        const std::string source = std::filesystem::exists(userWorkspace)
                                       ? userWorkspace.string()
                                       : m_workspace.path();
        if (!source.empty()) {
            std::string error;
            if (!m_workspace.load(source, &error)) {
                std::fprintf(stderr, "[workspace] %s\n", error.c_str());
            }
        }
        m_layoutNeedsRebuild = true;
    }

    if (EditorShortcuts::instance().pressed(Shortcut::CommandPalette) ||
        EditorShortcuts::instance().pressed(Shortcut::CommandPaletteAlt)) {
        m_commandPalette.toggle();
        return;
    }

    if (EditorShortcuts::instance().pressed(Shortcut::Save)) {
        const bool saveMaterial = m_materialEditor.isOpen() &&
                                  (m_materialEditor.wasFocusedLastFrame() || m_materialEditor.isDirty());
        if (saveMaterial) m_materialEditor.handleSaveShortcut();
        if (!m_materialEditor.wasFocusedLastFrame()) {
            if (m_ctx.currentScenePath.empty()) {
                saveSceneAs(world);
            } else {
                saveScene(m_ctx.currentScenePath.c_str(), world);
            }
        }
    }
    if (EditorShortcuts::instance().pressed(Shortcut::NewScene)) {
        if (m_ctx.isDirty) {
            m_pendingAction = PendingAction::NewScene;
            ImGui::OpenPopup("Unsaved Changes?");
        } else {
            doNewScene();
        }
    }
    if (EditorShortcuts::instance().pressed(Shortcut::Undo)) {
        if (m_ctx.undoStack.canUndo()) m_ctx.undoStack.undo(world);
        if (!world.isEntityAlive(m_ctx.selectedEntity)) m_ctx.selectedEntity = ECS::Entity::INVALID;
    }
    if (EditorShortcuts::instance().pressed(Shortcut::Redo)) {
        if (m_ctx.undoStack.canRedo()) m_ctx.undoStack.redo(world);
        if (!world.isEntityAlive(m_ctx.selectedEntity)) m_ctx.selectedEntity = ECS::Entity::INVALID;
    }

    if (EditorShortcuts::instance().pressed(Shortcut::Copy)) {
        if (m_ctx.selectedEntity.isValid()) {
            m_ctx.clipboardEntity = m_ctx.selectedEntity;
        }
    }
    if (EditorShortcuts::instance().pressed(Shortcut::Paste)) {
        if (m_ctx.clipboardEntity.isValid()) {
            m_hierarchy.duplicateEntity(world, m_ctx.clipboardEntity);
        }
    }
    if (EditorShortcuts::instance().pressed(Shortcut::Duplicate)) {
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

void SceneEditor::registerPanels() {
    if (m_panelsRegistered) return;
    m_panelsRegistered = true;

    auto& panels = UIKit::PanelRegistry::instance();
    panels.clear();

    auto add = [&panels](const char* id, const char* title, const char* icon,
                         UIKit::DockArea dock, std::function<void()> render,
                         std::function<void()> open, std::function<void()> close,
                         std::function<bool()> isOpen, const char* category = "Core",
                         int order = 0, bool defaultOpen = true) {
        UIKit::PanelDesc desc;
        desc.id = id;
        desc.title = title;
        desc.icon = icon ? icon : "";
        desc.category = category;
        desc.dock = dock;
        desc.defaultOpen = defaultOpen;
        desc.order = order;
        desc.render = std::move(render);
        desc.open = std::move(open);
        desc.close = std::move(close);
        desc.isOpen = std::move(isOpen);
        panels.add(std::move(desc));
    };

    add("core.hierarchy", "Hierarchy", EditorIcon::Hierarchy, UIKit::DockArea::Left,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_hierarchy.render(*world, m_ctx);
        },
        [this] { m_hierarchy.open(); }, [this] { m_hierarchy.close(); },
        [this] { return m_hierarchy.isOpen(); }, "Core", 10);

    add("core.toolbox", "Toolbox", EditorIcon::Hierarchy, UIKit::DockArea::Left,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_toolbox.render(*world, m_ctx, m_viewport);
        },
        [this] { m_toolbox.open(); }, [this] { m_toolbox.close(); },
        [this] { return m_toolbox.isOpen(); }, "Core", 20);

    add("core.viewport", "Scene Viewport", EditorIcon::Viewport, UIKit::DockArea::Center,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_viewport.render(*world, m_ctx);
        },
        [this] { m_viewport.open(); }, [this] { m_viewport.close(); },
        [this] { return m_viewport.isOpen(); }, "Core", 0);

    add("core.inspector", "Inspector", EditorIcon::Inspector, UIKit::DockArea::Right,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_inspector.render(*world, m_ctx);
        },
        [this] { m_inspector.open(); }, [this] { m_inspector.close(); },
        [this] { return m_inspector.isOpen(); }, "Core", 10);

    add("core.gameplay_preview", "Gameplay Preview", EditorIcon::Viewport,
        UIKit::DockArea::Center,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_gameplayPreview.render(*world, m_ctx);
        },
        [this] { m_gameplayPreview.open(); }, [this] { m_gameplayPreview.close(); },
        [this] { return m_gameplayPreview.isOpen(); }, "Core", 10, false);

    add("core.camera_preview", "Camera Preview", EditorIcon::Viewport, UIKit::DockArea::Center,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) {
                m_cameraPreview.onImGuiRender(*world, m_ctx, m_viewport);
            }
        },
        [this] { m_cameraPreview.open(); }, [this] { m_cameraPreview.close(); },
        [this] { return m_cameraPreview.isOpen(); }, "Core", 20, false);

    add("core.asset_browser", "Asset Browser", EditorIcon::Assets, UIKit::DockArea::Bottom,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_assetBrowser.render(*world, m_ctx);
        },
        [this] { m_assetBrowser.open(); }, [this] { m_assetBrowser.close(); },
        [this] { return m_assetBrowser.isOpen(); }, "Core", 10);

    add("core.console", "Console", EditorIcon::Console, UIKit::DockArea::Bottom,
        [this] { m_console.render(); }, [this] { m_console.open(); }, [this] { m_console.close(); },
        [this] { return m_console.isOpen(); }, "Debug", 20);

    add("core.profiler", "Profiler", EditorIcon::Profiler, UIKit::DockArea::Bottom,
        [this] { m_profiler.render(Debug::Profiler::instance()); },
        [this] { m_profiler.open(); }, [this] { m_profiler.close(); },
        [this] { return m_profiler.isOpen(); }, "Debug", 30, false);

    add("core.entity_debugger", "Entity Debugger", EditorIcon::EntityDebugger,
        UIKit::DockArea::Bottom,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_entityDebugger.render(*world, m_ctx);
        },
        [this] { m_entityDebugger.open(); }, [this] { m_entityDebugger.close(); },
        [this] { return m_entityDebugger.isOpen(); }, "Debug", 40, false);

    add("core.script_editor", "Script Editor", EditorIcon::NewScene, UIKit::DockArea::Bottom,
        [this] { m_scriptEditor.render(); }, [this] { m_scriptEditor.open(); },
        [this] { m_scriptEditor.close(); }, [this] { return m_scriptEditor.isOpen(); },
        "Content", 10, false);

    add("core.settings", "Settings", EditorIcon::Inspector, UIKit::DockArea::Bottom,
        [this] { m_settingsPanel.render(); }, [this] { m_settingsPanel.open(); },
        [this] { m_settingsPanel.close(); }, [this] { return m_settingsPanel.isOpen(); },
        "Core", 90, false);

    add("photon.gi", "Photon", EditorIcon::Viewport, UIKit::DockArea::Right,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_photonPanel.render(*world, m_ctx);
        },
        [this] { m_photonPanel.open(); }, [this] { m_photonPanel.close(); },
        [this] { return m_photonPanel.isOpen(); }, "Rendering", 10, false);

    add("terrain.editor", "Terrain", EditorIcon::Viewport, UIKit::DockArea::Bottom,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) m_terrainPanel.render(*world, m_ctx);
        },
        [this] { m_terrainPanel.open(); }, [this] { m_terrainPanel.close(); },
        [this] { return m_terrainPanel.isOpen(); }, "World", 20, true);

    add("core.material_editor", "Material Editor", EditorIcon::Assets, UIKit::DockArea::Bottom,
        [this] {
            m_materialEditor.setProjectRoot(m_currentProjectConfig.RootPath.string());
            if (!m_ctx.materialToOpen.empty()) {
                m_materialEditor.open();
                m_materialEditor.openFromPath(std::filesystem::path(m_ctx.materialToOpen));
                m_ctx.materialToOpen.clear();
            }
            m_materialEditor.onImGuiRender(m_ctx);
        },
        [this] { m_materialEditor.open(); }, [this] { m_materialEditor.close(); },
        [this] { return m_materialEditor.isOpen(); }, "Content", 20, false);

    add("core.image_manager", "Image Manager", EditorIcon::Assets, UIKit::DockArea::Center,
        [this] { m_imageManager.onImGuiRender(m_ctx); },
        [this] { m_imageManager.open(); }, [this] { m_imageManager.close(); },
        [this] { return m_imageManager.isOpen(); }, "Content", 30, false);

    add("core.hud_editor", "HUD Editor", EditorIcon::Viewport, UIKit::DockArea::Center,
        [this] { m_hudEditor.onImGuiRender(m_ctx); },
        [this] { m_hudEditor.open(); }, [this] { m_hudEditor.close(); },
        [this] { return m_hudEditor.isOpen(); }, "Tools", 40, false);

    add("core.particles", "Particles", EditorIcon::Animation, UIKit::DockArea::Center,
        [this] { m_particleEditor.onImGuiRender(m_ctx); },
        [this] { m_particleEditor.open(); }, [this] { m_particleEditor.close(); },
        [this] { return m_particleEditor.isOpen(); }, "Tools", 50, false);

    add("core.effects", "Effects", EditorIcon::Profiler, UIKit::DockArea::Center,
        [this] { m_effectEditor.onImGuiRender(m_ctx); },
        [this] { m_effectEditor.open(); }, [this] { m_effectEditor.close(); },
        [this] { return m_effectEditor.isOpen(); }, "Tools", 60, false);

#if defined(CF_HAS_DRYAD)
    add("core.dryad_node_script", "Dryad Node Script", EditorIcon::Animation,
        UIKit::DockArea::Center, [this] {
            if (!m_ctx.dryadToOpen.empty()) {
                std::filesystem::path path(m_ctx.dryadToOpen);
                if (path.is_relative() && !m_currentProjectConfig.RootPath.empty()) {
                    path = m_currentProjectConfig.RootPath / path;
                }
                m_dryadNodeScript.open();
                m_dryadNodeScript.openFromScript(path);
                m_ctx.dryadToOpen.clear();
            }
            m_dryadNodeScript.onImGuiRender(m_ctx);
        },
        [this] { m_dryadNodeScript.open(); }, [this] { m_dryadNodeScript.close(); },
        [this] { return m_dryadNodeScript.isOpen(); }, "Content", 40, false);
#endif

    add("core.entity_presets", "Entity Presets", EditorIcon::Assets, UIKit::DockArea::Bottom,
        [this] {
            m_entityPresets.setProjectRoot(m_currentProjectConfig.RootPath);
            if (auto* world = m_tabManager.activeWorld()) m_entityPresets.render(*world, m_ctx);
        },
        [this] { m_entityPresets.open(); }, [this] { m_entityPresets.close(); },
        [this] { return m_entityPresets.isOpen(); }, "Scene", 30, false);

    add("core.audio_preview", "Audio Preview", EditorIcon::Assets, UIKit::DockArea::Bottom,
        [this] { m_audioPreview.onImGuiRender(); },
        [this] { m_audioPreview.open(); }, [this] { m_audioPreview.close(); },
        [this] { return m_audioPreview.isOpen(); }, "Content", 50, false);

    add("core.animation_timeline", "Animation Timeline", EditorIcon::Animation,
        UIKit::DockArea::Bottom,
        [this] {
            i32 posedBone = -1;
            if (m_ctx.skeletonEntityId == m_ctx.selectedEntity.id()) posedBone = m_ctx.skeletonBone;
            m_animationTimeline.setScene(m_tabManager.activeWorld(), m_ctx.selectedEntity.id(),
                                         posedBone);
            m_animationTimeline.setProjectRoot(m_ctx.projectRootPath.string());
            m_animationTimeline.render(ImGui::GetIO().DeltaTime);
            if (m_animationTimeline.consumePickedBone(posedBone)) {
                m_ctx.skeletonEntityId = m_ctx.selectedEntity.id();
                m_ctx.skeletonBone = posedBone;
            }
        },
        [this] { m_animationTimeline.open(); }, [this] { m_animationTimeline.close(); },
        [this] { return m_animationTimeline.isOpen(); }, "Animation", 10, false);

    add("core.animator_controller", "Animator Controller", EditorIcon::Animator,
        UIKit::DockArea::Bottom,
        [this] {
            if (auto* world = m_tabManager.activeWorld()) {
                m_animatorController.bindSelection(world, m_ctx.selectedEntity,
                                                  m_ctx.projectRootPath.string());
            }
            m_animatorController.render();
        },
        [this] { m_animatorController.open(); }, [this] { m_animatorController.close(); },
        [this] { return m_animatorController.isOpen(); }, "Animation", 20, false);

    add("core.tilemap_editor", "Tilemap Editor", EditorIcon::Assets, UIKit::DockArea::Bottom,
        [this] { m_tilemapEditor.render(); }, [this] { m_tilemapEditor.open(); },
        [this] { m_tilemapEditor.close(); }, [this] { return m_tilemapEditor.isOpen(); },
        "Scene", 20, false);

    add("core.build_run", "Build & Run", EditorIcon::Build, UIKit::DockArea::Bottom,
        [this] { m_buildDialog.render(); }, [this] { m_buildDialog.open(); },
        [this] { m_buildDialog.close(); }, [this] { return m_buildDialog.isOpen(); },
        "Build", 10, false);

    add("core.plugin_manager", "Plugin Manager", EditorIcon::Plugins, UIKit::DockArea::Bottom,
        [this] { PluginManager::instance().renderManagerUI(); },
        [this] { PluginManager::instance().openManager(); },
        [this] { PluginManager::instance().closeManager(); },
        [this] { return PluginManager::instance().isManagerOpen(); }, "Plugins", 10, false);
}

void SceneEditor::applyLayoutProfile(ImGuiID dockspaceId, const LayoutProfile& profile) {
    // Visibility is owned by the registry so the View menu and layout profiles
    // stay in sync.
    m_panels.setVisible("core.hierarchy", profile.hierarchyOpen);
    m_panels.setVisible("core.inspector", profile.inspectorOpen);
    m_panels.setVisible("core.viewport", profile.viewportOpen);
    m_panels.setVisible("core.asset_browser", profile.assetsOpen);
    m_panels.setVisible("core.console", profile.consoleOpen);
    m_panels.setVisible("core.profiler", profile.profilerOpen);
    m_panels.setVisible("core.script_editor", profile.scriptEditorOpen);
    m_panels.setVisible("core.tilemap_editor", profile.tilemapEditorOpen);
    m_panels.setVisible("core.animation_timeline", profile.animationTimelineOpen);
    m_panels.setVisible("core.animator_controller", profile.animatorControllerOpen);

    m_workspace.apply(dockspaceId, m_panels);
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
