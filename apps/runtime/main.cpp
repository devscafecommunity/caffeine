#include "animation/AnimationSystem.hpp"
#include "debug/LogSystem.hpp"
#include "events/EventBus.hpp"
#include "events/Events.hpp"
#include "input/InputManager.hpp"
#include "physics/PhysicsSystem2D.hpp"
#include "script/ScriptEngine.hpp"
#include "script/ScriptSystem.hpp"
#include "script/ScriptTypes.hpp"
#include "editor/ProjectManager.hpp"
#include "editor/SceneSerializer.hpp"
#include "editor/ImGuiIntegration.hpp"
#include "runtime/RuntimeSceneRenderer.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/World.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/HierarchySystem.hpp"
#include "ui/UISystem.hpp"
#include "ui/UIRenderer.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "procedural/ProceduralWorldSystem.hpp"
#include "render/SkyboxRenderer.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#endif

namespace {

std::filesystem::path findProjectFile(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--project" || std::string(argv[i]) == "--project-dir") {
            const std::filesystem::path dir(argv[i + 1]);
            const auto direct = dir / "project.caffeine";
            if (std::filesystem::exists(direct)) return direct;
            if (std::filesystem::exists(dir) && dir.filename() == "project.caffeine") return dir;
        }
    }

    const std::filesystem::path cwdProject = std::filesystem::current_path() / "project.caffeine";
    if (std::filesystem::exists(cwdProject)) return cwdProject;

    const std::filesystem::path dataProject = std::filesystem::current_path() / ".." / "project.caffeine";
    if (std::filesystem::exists(dataProject)) return std::filesystem::weakly_canonical(dataProject);

    return {};
}

void pumpRuntimeInput(Caffeine::Input::InputManager& input) {
    input.beginFrame();
#ifdef CF_HAS_IMGUI
    const bool typing = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
#else
    const bool typing = false;
#endif
    int keyCount = 0;
    const bool* ks = SDL_GetKeyboardState(&keyCount);
    const int limit = std::min(keyCount, static_cast<int>(Caffeine::Input::Key::KeyCount));
    if (!typing && ks) {
        for (int i = 0; i < limit; ++i) {
            const auto key = static_cast<Caffeine::Input::Key>(i);
            if (ks[i]) input.injectKeyDown(key);
            else input.injectKeyUp(key);
        }
    } else {
        for (int i = 0; i < static_cast<int>(Caffeine::Input::Key::KeyCount); ++i) {
            input.injectKeyUp(static_cast<Caffeine::Input::Key>(i));
        }
    }

    float mx = 0.0f, my = 0.0f;
    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(&mx, &my);
    input.injectMouseMove(mx, my);
    auto syncMouse = [&](SDL_MouseButtonFlags mask, Caffeine::Input::MouseButton button) {
        if (mouseButtons & mask) input.injectMouseButtonDown(button);
        else input.injectMouseButtonUp(button);
    };
    syncMouse(SDL_BUTTON_LMASK, Caffeine::Input::MouseButton::Left);
    syncMouse(SDL_BUTTON_MMASK, Caffeine::Input::MouseButton::Middle);
    syncMouse(SDL_BUTTON_RMASK, Caffeine::Input::MouseButton::Right);

    float stickLX = 0.0f, stickLY = 0.0f, stickRX = 0.0f, stickRY = 0.0f;
    int padCount = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&padCount);
    if (pads && padCount > 0) {
        SDL_Gamepad* gp = SDL_GetGamepadFromID(pads[0]);
        if (!gp) gp = SDL_OpenGamepad(pads[0]);
        if (gp) {
            auto axis = [&](SDL_GamepadAxis a) -> float {
                const float v = static_cast<float>(SDL_GetGamepadAxis(gp, a)) / 32767.0f;
                return (std::abs(v) < 0.18f) ? 0.0f : v;
            };
            stickLX = axis(SDL_GAMEPAD_AXIS_LEFTX);
            stickLY = -axis(SDL_GAMEPAD_AXIS_LEFTY);
            stickRX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
            stickRY = axis(SDL_GAMEPAD_AXIS_RIGHTY);
            auto syncBtn = [&](SDL_GamepadButton b, Caffeine::Input::GamepadButton ib) {
                if (SDL_GetGamepadButton(gp, b)) input.injectGamepadButtonDown(ib);
                else input.injectGamepadButtonUp(ib);
            };
            syncBtn(SDL_GAMEPAD_BUTTON_SOUTH, Caffeine::Input::GamepadButton::A);
            syncBtn(SDL_GAMEPAD_BUTTON_EAST, Caffeine::Input::GamepadButton::B);
            syncBtn(SDL_GAMEPAD_BUTTON_WEST, Caffeine::Input::GamepadButton::X);
            syncBtn(SDL_GAMEPAD_BUTTON_NORTH, Caffeine::Input::GamepadButton::Y);
            syncBtn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Caffeine::Input::GamepadButton::LeftBumper);
            syncBtn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Caffeine::Input::GamepadButton::RightBumper);
        }
    }
    if (pads) SDL_free(pads);

    float relX = 0.0f, relY = 0.0f;
    SDL_GetRelativeMouseState(&relX, &relY);
    input.injectGamepadAxis(Caffeine::Input::GamepadAxis::LeftX, stickLX);
    input.injectGamepadAxis(Caffeine::Input::GamepadAxis::LeftY, stickLY);
    input.injectGamepadAxis(Caffeine::Input::GamepadAxis::RightX, stickRX + relX * 0.12f);
    input.injectGamepadAxis(Caffeine::Input::GamepadAxis::RightY, stickRY + relY * 0.12f);
    input.endFrame();
}

#ifdef CF_HAS_IMGUI

constexpr float kDegToRad = 3.14159265f / 180.0f;

Caffeine::Render::SkyboxRenderer g_skyboxRenderer;

Caffeine::Mat4 buildLocalMatrix3D(const Caffeine::ECS::Position3D* p,
                                  const Caffeine::ECS::Rotation3D* r,
                                  const Caffeine::ECS::Scale3D* s) {
    Caffeine::Mat4 T = p ? Caffeine::Mat4::translation(p->position) : Caffeine::Mat4::identity();
    Caffeine::Mat4 R = r ? Caffeine::Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w)
                               .normalized()
                               .toMatrix()
                         : Caffeine::Mat4::identity();
    Caffeine::Mat4 S = s ? Caffeine::Mat4::scale(s->scale.x, s->scale.y, s->scale.z)
                         : Caffeine::Mat4::identity();
    return T * R * S;
}

Caffeine::Mat4 entityMatrix(Caffeine::ECS::World& world, Caffeine::ECS::Entity entity) {
    if (auto* wt = world.get<Caffeine::Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<Caffeine::ECS::Transform>(entity)) {
        return Caffeine::Mat4::translation(t->position)
             * Caffeine::Mat4::rotationZ(t->rotation.z * kDegToRad)
             * Caffeine::Mat4::rotationY(t->rotation.y * kDegToRad)
             * Caffeine::Mat4::rotationX(t->rotation.x * kDegToRad)
             * Caffeine::Mat4::scale(t->scale.x, t->scale.y, t->scale.z);
    }
    auto* p3 = world.get<Caffeine::ECS::Position3D>(entity);
    auto* r3 = world.get<Caffeine::ECS::Rotation3D>(entity);
    auto* s3 = world.get<Caffeine::ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

Caffeine::Vec3 matrixAxis(const Caffeine::Mat4& m, int column, const Caffeine::Vec3& fallback) {
    Caffeine::Vec3 axis(m(0, column), m(1, column), m(2, column));
    const float lenSq = axis.lengthSquared();
    return (lenSq > 0.000001f) ? axis / std::sqrt(lenSq) : fallback;
}

Caffeine::Vec3 entityForward(Caffeine::ECS::World& world, Caffeine::ECS::Entity entity) {
    return -1.0f * matrixAxis(entityMatrix(world, entity), 2, Caffeine::Vec3(0.0f, 0.0f, -1.0f));
}

bool tryGetEntityPosition(Caffeine::ECS::World& world, Caffeine::ECS::Entity entity, Caffeine::Vec3& out) {
    if (auto* wt = world.get<Caffeine::Scene::WorldTransform>(entity)) {
        out = wt->matrix.transformPoint(Caffeine::Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<Caffeine::ECS::Transform>(entity)) {
        out = t->position;
        return true;
    }
    if (auto* p3 = world.get<Caffeine::ECS::Position3D>(entity)) {
        out = p3->position;
        return true;
    }
    return false;
}

Caffeine::Mat4 buildCameraViewMatrix(Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                                     Caffeine::Vec3& outPosition) {
    const Caffeine::Mat4 worldMatrix = entityMatrix(world, entity);
    outPosition = worldMatrix.transformPoint(Caffeine::Vec3(0.0f, 0.0f, 0.0f));

    const Caffeine::Vec3 right = matrixAxis(worldMatrix, 0, Caffeine::Vec3(1.0f, 0.0f, 0.0f));
    const Caffeine::Vec3 up = matrixAxis(worldMatrix, 1, Caffeine::Vec3(0.0f, 1.0f, 0.0f));
    const Caffeine::Vec3 forward = entityForward(world, entity).normalized();

    Caffeine::Mat4 view = Caffeine::Mat4::identity();
    view(0, 0) = right.x;
    view(0, 1) = right.y;
    view(0, 2) = right.z;
    view(1, 0) = up.x;
    view(1, 1) = up.y;
    view(1, 2) = up.z;
    view(2, 0) = -forward.x;
    view(2, 1) = -forward.y;
    view(2, 2) = -forward.z;
    view(0, 3) = -right.dot(outPosition);
    view(1, 3) = -up.dot(outPosition);
    view(2, 3) = forward.dot(outPosition);
    return view;
}

bool findActiveCamera3D(Caffeine::ECS::World& world, Caffeine::ECS::Entity& outEntity,
                        Caffeine::ECS::Camera3DComponent*& outCam) {
    Caffeine::ECS::ComponentQuery activeQ;
    activeQ.with<Caffeine::ECS::Camera3DComponent>();
    activeQ.with<Caffeine::ECS::CameraActiveComponent>();
    world.forEach<Caffeine::ECS::Camera3DComponent, Caffeine::ECS::CameraActiveComponent>(
        activeQ, [&](Caffeine::ECS::Entity e, Caffeine::ECS::Camera3DComponent& cam,
                     Caffeine::ECS::CameraActiveComponent&) {
            if (!outCam) {
                outEntity = e;
                outCam = &cam;
            }
        });
    if (outCam) return true;

    Caffeine::ECS::ComponentQuery q;
    q.with<Caffeine::ECS::Camera3DComponent>();
    world.forEach<Caffeine::ECS::Camera3DComponent>(q,
        [&](Caffeine::ECS::Entity e, Caffeine::ECS::Camera3DComponent& cam) {
            if (!outCam) {
                outEntity = e;
                outCam = &cam;
            }
        });
    return outCam != nullptr;
}

void renderGameView(Caffeine::ECS::World& world, Caffeine::Editor::EditorContext& ctx,
                    Caffeine::Runtime::RuntimeSceneRenderer& renderer,
                    Caffeine::RHI::RenderDevice& device, Caffeine::RHI::CommandBuffer* cmd,
                    const std::string& projectRoot) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (!ImGui::Begin("##RuntimeGameView", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    if (panelSize.x < 4.0f) panelSize.x = 4.0f;
    if (panelSize.y < 4.0f) panelSize.y = 4.0f;

    Caffeine::ECS::Entity cameraEntity;
    Caffeine::ECS::Camera3DComponent* cam3D = nullptr;
    if (!findActiveCamera3D(world, cameraEntity, cam3D)) {
        dl->AddRectFilled(origin,
                          ImVec2(origin.x + panelSize.x, origin.y + panelSize.y),
                          IM_COL32(20, 20, 24, 255));
        const char* msg = "No Camera 3D in scene";
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (panelSize.x - textSize.x) * 0.5f,
                           origin.y + (panelSize.y - textSize.y) * 0.5f),
                    IM_COL32(180, 180, 190, 230), msg);
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    Caffeine::Vec3 camPos;
    const Caffeine::Mat4 view = buildCameraViewMatrix(world, cameraEntity, camPos);
    const float aspect = panelSize.x / std::max(panelSize.y, 1.0f);
    cam3D->aspectRatio = aspect;
    const Caffeine::Mat4 proj =
        Caffeine::Mat4::perspective(cam3D->fov * kDegToRad, aspect, cam3D->nearClip, cam3D->farClip);
    const Caffeine::Mat4 vp = proj * view;

    const Caffeine::Mat4 worldMatrix = entityMatrix(world, cameraEntity);
    Caffeine::Render::SkyboxCamera skyCamera;
    skyCamera.forward = entityForward(world, cameraEntity).normalized();
    skyCamera.right = matrixAxis(worldMatrix, 0, Caffeine::Vec3(1.0f, 0.0f, 0.0f));
    skyCamera.up = matrixAxis(worldMatrix, 1, Caffeine::Vec3(0.0f, 1.0f, 0.0f));
    skyCamera.fovY = cam3D->fov * kDegToRad;
    skyCamera.aspect = aspect;

    bool drewSky = false;
    const Caffeine::Scene::ActiveSkybox activeSky = Caffeine::Scene::findActiveSkybox(world);
    if (activeSky.component) {
        const auto skyPath =
            Caffeine::Scene::resolveSkyboxTexturePath(*activeSky.component, projectRoot);
        if (!skyPath.empty()) {
            drewSky = g_skyboxRenderer.draw(dl, origin, panelSize, skyCamera, skyPath.string());
        }
    }
    if (!drewSky) {
        dl->AddRectFilledMultiColor(
            origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y), IM_COL32(20, 20, 24, 255),
            IM_COL32(20, 20, 24, 255), IM_COL32(34, 38, 50, 255), IM_COL32(34, 38, 50, 255));
    }

    Caffeine::Render::GpuSceneCamera gpuCam;
    gpuCam.position = camPos;
    gpuCam.focus = camPos + entityForward(world, cameraEntity).normalized();
    gpuCam.view = view;
    gpuCam.proj = proj;
    gpuCam.fovRad = cam3D->fov * kDegToRad;
    gpuCam.nearClip = std::max(cam3D->nearClip, 0.05f);
    gpuCam.farClip = std::max(cam3D->farClip, 50.0f);

#ifdef CF_HAS_SDL3
    renderer.setFrameCommandBuffer(cmd);
#endif
    renderer.render(world, ctx, dl, vp, camPos, origin, panelSize, cameraEntity, projectRoot,
                    &gpuCam);

    Caffeine::UI::drawWidgets(world, dl, origin, panelSize);

    ImGui::End();
    ImGui::PopStyleVar(2);
}

#endif

}  // namespace

int main(int argc, char** argv) {
    const auto projectFile = findProjectFile(argc, argv);
    if (projectFile.empty()) {
        std::fprintf(stderr, "caffeine-runtime: project.caffeine not found\n");
        std::fprintf(stderr, "Usage: caffeine-runtime [--project <build-output-dir>]\n");
        return 1;
    }

    Caffeine::Editor::ProjectManager pm;
    Caffeine::Editor::ProjectConfig config;
    if (!pm.LoadProjectFromFile(projectFile, config)) {
        std::fprintf(stderr, "caffeine-runtime: failed to parse %s\n", projectFile.string().c_str());
        return 1;
    }

    const std::filesystem::path buildRoot = std::filesystem::absolute(projectFile.parent_path());
    config.RootPath = buildRoot;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    std::string title = config.Name.empty() ? "Caffeine Game" : config.Name;
    title += " — Runtime";
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Caffeine::RHI::RenderConfig renderCfg;
    renderCfg.width = 1280;
    renderCfg.height = 720;
    renderCfg.vsync = true;
    renderCfg.windowTitle = title.c_str();

    Caffeine::RHI::RenderDevice device;
    if (!device.init(window, renderCfg)) {
        std::fprintf(stderr, "RenderDevice::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#ifdef CF_HAS_IMGUI
    Caffeine::Editor::ImGuiIntegration imgui;
    if (!imgui.init(window, &device)) {
        std::fprintf(stderr, "ImGuiIntegration::init failed\n");
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ImGui_ImplSDLGPU3_CreateDeviceObjects();

    Caffeine::Runtime::RuntimeSceneRenderer renderer;
#ifdef CF_HAS_SDL3
    if (!renderer.init(&device)) {
        std::fprintf(stderr, "RuntimeSceneRenderer GPU init failed — CPU mesh fallback\n");
    }
#endif
    Caffeine::Editor::EditorContext editorCtx;
    editorCtx.viewMode = Caffeine::Editor::EditorContext::ViewMode::Mode3D;
#endif

    Caffeine::ECS::World world;
    std::filesystem::path scenePath;
    bool sceneLoaded = false;
    if (!config.LastScene.empty()) {
        scenePath = buildRoot / config.LastScene;
        if (std::filesystem::exists(scenePath)) {
            Caffeine::Editor::SceneSerializer serializer(world);
            sceneLoaded = serializer.deserialize(scenePath.string());
            std::printf("Loaded scene: %s (%s)\n", scenePath.string().c_str(),
                        sceneLoaded ? "ok" : "failed");
        } else {
            std::fprintf(stderr, "Startup scene not found: %s\n", config.LastScene.c_str());
        }
    }

#ifdef CF_HAS_IMGUI
    if (sceneLoaded) {
        editorCtx.currentScenePath = scenePath.string();
    }
#endif

    Caffeine::Debug::LogSystem::instance().addSink(
        [](Caffeine::Debug::LogLevel level, const char* category, const char* message) {
            std::fprintf(stderr, "[%s] %s: %s\n",
                         Caffeine::Debug::LogSystem::levelToString(level),
                         category ? category : "", message ? message : "");
        });

    Caffeine::Input::InputManager input;
    Caffeine::Events::EventBus eventBus;
    Caffeine::Physics2D::PhysicsSystem2D physics(&eventBus);
    Caffeine::Animation::AnimationSystem animation;
    Caffeine::Script::ScriptEngine scriptEngine;
    Caffeine::Script::ScriptEngine::InitParams scriptParams;
    scriptParams.world = &world;
    scriptParams.input = &input;
    scriptParams.events = &eventBus;
    const bool scriptsReady = scriptEngine.init(scriptParams);
    scriptEngine.setSearchRoot(buildRoot.string());
    Caffeine::Script::ScriptSystem scriptSystem(scriptsReady ? &scriptEngine : nullptr);
    Caffeine::UI::UISystem uiSystem(&eventBus);
    if (scriptsReady) {
        scriptSystem.resetPlayState();
        eventBus.subscribe<Caffeine::Events::OnCollision2D>(
            [&](const Caffeine::Events::OnCollision2D& event) {
                if (!sceneLoaded) return;
                auto notify = [&](Caffeine::u32 entityId, Caffeine::u32 otherId) {
                    Caffeine::ECS::Entity self(entityId, &world);
                    Caffeine::ECS::Entity other(otherId, &world);
                    if (!self.isValid() || !other.isValid()) return;
                    const auto* script = world.get<Caffeine::Script::ScriptComponent>(self);
                    if (!script || script->scriptPath.empty()) return;
                    scriptEngine.callOnCollision(script->scriptPath, self, other);
                };
                notify(event.entityA, event.entityB);
                notify(event.entityB, event.entityA);
            });
        std::printf("Script engine ready (search root: %s)\n", buildRoot.string().c_str());
    } else {
        std::fprintf(stderr, "caffeine-runtime: script engine failed to init\n");
    }

    if (!sceneLoaded) {
        if (config.LastScene.empty()) {
            std::fprintf(stderr,
                         "caffeine-runtime: no startup scene in project.caffeine — rebuild from the editor "
                         "(Build & Run saves the active scene automatically)\n");
        } else {
            std::fprintf(stderr, "caffeine-runtime: startup scene not found: %s\n",
                         scenePath.string().c_str());
        }
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#ifdef CF_HAS_IMGUI
            imgui.processEvent(event);
#endif
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }

        const Uint64 nowNs = SDL_GetTicksNS();
        static Uint64 lastNs = nowNs;
        float dt = static_cast<float>(nowNs - lastNs) / 1'000'000'000.0f;
        lastNs = nowNs;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.1f) dt = 0.1f;

#ifdef CF_HAS_IMGUI
        if (ImGui::GetCurrentContext()) {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        }
#endif
        pumpRuntimeInput(input);
        if (sceneLoaded) {
            animation.onUpdate(world, dt);
            physics.onUpdate(world, dt);
            if (scriptsReady) {
                scriptEngine.setWorld(&world);
                scriptEngine.setInput(&input);
                scriptSystem.onUpdate(world, dt);
            }
            Caffeine::Procedural::ProceduralWorldSystem::update(world);
            uiSystem.onUpdate(world, dt);
        }

        Caffeine::Scene::propagateTransforms(world);

        Caffeine::RHI::CommandBuffer* cmd = device.beginFrame();
        if (cmd) {
#ifdef CF_HAS_IMGUI
            imgui.beginFrame();
            if (sceneLoaded) {
                renderGameView(world, editorCtx, renderer, device, cmd, buildRoot.string());
            } else {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
                ImGui::Begin("##empty", nullptr, ImGuiWindowFlags_NoDecoration);
                if (config.LastScene.empty()) {
                    ImGui::TextWrapped(
                        "Scene failed to load.\nRebuild with Build & Run so the active scene is packaged.");
                } else {
                    ImGui::TextWrapped("Scene failed to load:\n%s", config.LastScene.c_str());
                }
                ImGui::End();
            }
            imgui.prepareRender(cmd);
#endif

            Caffeine::RHI::RenderPassDesc pass;
            pass.clearColor[0] = 0.08f;
            pass.clearColor[1] = 0.07f;
            pass.clearColor[2] = 0.10f;
            pass.clearColor[3] = 1.0f;
            cmd->beginRenderPass(pass);
#ifdef CF_HAS_IMGUI
            imgui.endFrame(cmd);
#endif
            cmd->endRenderPass();
            device.endFrame(cmd);
        }
    }

    scriptEngine.shutdown();
#ifdef CF_HAS_IMGUI
    g_skyboxRenderer.releaseGpuTextures();
    renderer.shutdown();
    imgui.shutdown();
#endif
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
