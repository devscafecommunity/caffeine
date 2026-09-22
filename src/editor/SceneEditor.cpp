#include "editor/SceneEditor.hpp"
#include "assets/MeshCache.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "terrain/TerrainCache.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "editor/ComponentRegistry.hpp"
#include "editor/EditorIcons.hpp"
#include "debug/Profiler.hpp"
#include "debug/LogSystem.hpp"
#include "editor/PluginSystem.hpp"
#include "editor/PluginSymbolExports.hpp"
#include "render/GpuTextureCache.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EditorPaths.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/PlayMode2D.hpp"
#include "procedural/ProceduralWorldSystem.hpp"
#include "script/ScriptTypes.hpp"
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
    if (!m_viewport.init(device)) return false;
    m_gameplayPreview.init(device);
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

    EntityPresetRegistry::instance().registerBuiltIns();
    EntityPresetRegistry::instance().scanProject(m_currentProjectConfig.RootPath);
    m_entityPresets.setProjectRoot(m_currentProjectConfig.RootPath);
    m_entityPresets.setOnScriptOpen([this](const std::string& path) {
        m_scriptEditor.openFile(std::filesystem::path(path));
        m_scriptEditor.open();
    });
    m_hierarchy.setOpenPresetsCallback([this]() { m_entityPresets.open(); });
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
     m_commandPalette.registerCommand("panel_entity_presets", "Entity Presets", "Panels", [this]() {
         m_entityPresets.open();
     });
     m_commandPalette.registerCommand("panel_gameplay_preview", "Gameplay Preview", "Panels", [this]() {
         m_gameplayPreview.open();
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

    EditorPaths::init();
    const std::filesystem::path pluginsDir =
        projectConfig.RootPath.empty()
            ? (std::filesystem::current_path() / "plugins")
            : (projectConfig.RootPath / "plugins");
    const std::filesystem::path bundledPluginsDir = EditorPaths::bundledPluginsDirectory();
    anchorPluginHostSymbols();
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
            m_pendingStartupScene = scenePath.string();
        }
    }

#ifdef CF_HAS_SCRIPTING
    Script::ScriptEngine::InitParams scriptParams;
    scriptParams.world  = nullptr;
    scriptParams.events = &m_eventBus;
    scriptParams.input  = &m_input;
    if (!m_scriptEngineReady) {
        m_scriptEngineReady = m_scriptEngine.init(scriptParams);
    }
     m_ctx.scriptEngine = &m_scriptEngine;
     m_scriptEngine.setSearchRoot(m_currentProjectConfig.RootPath.string());
     m_scriptEditor.setScriptEngine(&m_scriptEngine);
     if (m_scriptEngineReady) {
         m_scriptSystem = Script::ScriptSystem(&m_scriptEngine);
     }
#endif

    registerAllComponents(ComponentRegistry::instance());
    registerPlayModeEventListeners();

    Debug::LogSystem::instance().addSink([this](Debug::LogLevel level, const char* category, const char* message) {
        m_console.addLog(level, category ? category : "", message ? message : "");
    });

    return true;
}

void SceneEditor::shutdown() {
    PluginManager::instance().shutdown();
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
        m_scriptEngineReady = m_scriptEngine.init(p);
    }
    m_scriptSystem = Script::ScriptSystem(&m_scriptEngine);
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
    if (!m_isPlaying || m_isPaused) return;

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
    m_physicsSystem.onUpdate(world, dt);
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
#endif

// ── Main render ─────────────────────────────────────────────────

void SceneEditor::render(f32 deltaTime) {
    CF_PROFILE_SCOPE("SceneEditor::render");
    if (!m_open) return;

    ECS::World* activeWorld = m_tabManager.activeWorld();
    m_ctx.activeWorld = activeWorld;
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
        
        m_layoutNeedsRebuild = false;
        m_dockingSetup = true;
    }

    renderMainMenuBar(*activeWorld);

    {
        CF_PROFILE_SCOPE("SceneEditor::propagateTransforms");
        Scene::propagateTransforms(*activeWorld);
    }

    // Render panels
    {
        CF_PROFILE_SCOPE("SceneEditor::hierarchy");
        m_hierarchy.render(*activeWorld, m_ctx);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::inspector");
        m_inspector.render(*activeWorld, m_ctx);
    }
    renderPlaybar(*activeWorld);
    m_viewport.setFrameCommandBuffer(m_frameCmd);
#ifdef CF_HAS_SDL3
    m_gameplayPreview.setFrameCommandBuffer(m_frameCmd);
#endif
    {
        CF_PROFILE_SCOPE("SceneEditor::viewport");
        m_viewport.render(*activeWorld, m_ctx);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::assetBrowser");
        m_assetBrowser.render(*activeWorld, m_ctx);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::console");
        m_console.render();
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::profiler");
        m_profiler.render(Debug::Profiler::instance());
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::entityDebugger");
        m_entityDebugger.render(*activeWorld, m_ctx);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::scriptEditor");
        m_scriptEditor.render();
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::settings");
        m_settingsPanel.render();
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::materialEditor");
        m_materialEditor.onImGuiRender();
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::terrainEditor");
        m_terrainEditor.render(*activeWorld, m_ctx);
    }
    m_entityPresets.setProjectRoot(m_currentProjectConfig.RootPath);
    {
        CF_PROFILE_SCOPE("SceneEditor::entityPresets");
        m_entityPresets.render(*activeWorld, m_ctx);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::audioPreview");
        m_audioPreview.onImGuiRender();
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::cameraPreview");
        m_cameraPreview.onImGuiRender(*activeWorld, m_ctx, m_viewport);
    }
    {
        CF_PROFILE_SCOPE("SceneEditor::gameplayPreview");
        m_gameplayPreview.render(*activeWorld, m_ctx);
    }
    m_viewport.setFrameCommandBuffer(nullptr);
#ifdef CF_HAS_SDL3
    m_gameplayPreview.setFrameCommandBuffer(nullptr);
#endif
    m_animationTimeline.render(deltaTime);
    m_animatorController.render();
    m_tilemapEditor.render();
    m_commandPalette.render();
    m_buildDialog.render();
    {
        CF_PROFILE_SCOPE("SceneEditor::plugins");
        PluginManager::instance().renderPanels();
        PluginManager::instance().renderManagerUI();
    }

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
     ImGui::DockBuilderDockWindow("Gameplay Preview", dockCenter);
     ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
     ImGui::DockBuilderDockWindow("Console", dockBottom);
     ImGui::DockBuilderDockWindow("Profiler", dockBottom);
     ImGui::DockBuilderDockWindow("Entity Debugger", dockBottom);
     ImGui::DockBuilderDockWindow("Plugin Manager", dockBottom);
     ImGui::DockBuilderDockWindow("Material Editor", dockBottom);
     ImGui::DockBuilderDockWindow("Terrain Editor", dockBottom);
     ImGui::DockBuilderDockWindow("Entity Presets", dockBottom);

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
            bool gameplayPreviewOpen = m_gameplayPreview.isOpen();
            if (ImGui::MenuItem("Gameplay Preview", nullptr, gameplayPreviewOpen)) {
                gameplayPreviewOpen ? m_gameplayPreview.close() : m_gameplayPreview.open();
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
            bool entityPresetsOpen = m_entityPresets.isOpen();
            if (ImGui::MenuItem("Entity Presets", nullptr, entityPresetsOpen)) {
                entityPresetsOpen ? m_entityPresets.close() : m_entityPresets.open();
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

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_L)) {
        m_commandPalette.toggle();
        return;
    }

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
