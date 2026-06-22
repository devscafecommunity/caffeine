#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include "assets/AssetManager.hpp"

#include "editor/ImGuiIntegration.hpp"
#include "editor/SceneEditor.hpp"
#include "editor/ProjectStartupDialog.hpp"
#include "editor/TestRequestHandler.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/World.hpp"
#include "ecs/MeshComponents.hpp"
#include "scene/SceneComponents.hpp"
#include "math/Mat4.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    std::string scenePath;
    bool testMode = false;
    
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--scene") {
            scenePath = argv[i + 1];
            break;
        }
    }
    
    const char* testModeEnv = std::getenv("DOPPIO_TEST_MODE");
    if (testModeEnv) {
        testMode = (std::string(testModeEnv) == "1");
    }
    
    if (testMode) {
        std::fprintf(stderr, "[TEST MODE] Running headless Doppio\n");
        std::fprintf(stderr, "[TEST MODE] Scene: %s\n", scenePath.empty() ? "(none)" : scenePath.c_str());
        
        Caffeine::ECS::World world;
        Caffeine::Editor::EditorContext ctx;
        
        // Coordinate mapping: screenX = (worldX + 10) * 20, screenY = (worldY + 10) * 20
        // So entity at (0,0) → screen (200,200), (5,0) → (300,200), (10,5) → (400,300)
        struct TestEntityDef { float x, y, z; const char* name; };
        static const TestEntityDef kTestEntities[] = {
            {  0.0f,  0.0f, 0.0f, "Entity_1" },
            {  5.0f,  0.0f, 0.0f, "Entity_2" },
            { 10.0f,  5.0f, 0.0f, "Entity_3" },
        };
        for (auto& def : kTestEntities) {
            Caffeine::ECS::Entity e = world.create(def.name);
            Caffeine::ECS::MeshFilterComponent mf;
            mf.primitive = Caffeine::ECS::MeshPrimitive::Cube;
            world.add<Caffeine::ECS::MeshFilterComponent>(e, mf);
            Caffeine::Scene::WorldTransform wt;
            wt.matrix = Caffeine::Mat4::translation(Caffeine::Vec3(def.x, def.y, def.z));
            world.add<Caffeine::Scene::WorldTransform>(e, wt);
            std::fprintf(stderr, "[TEST MODE] Created entity '%s' at (%.1f, %.1f, %.1f)\n",
                         def.name, def.x, def.y, def.z);
        }
        std::fprintf(stderr, "[TEST MODE] Ready — waiting for JSON commands on stdin\n");
        
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
        
        std::string buffer;
        while (true) {
            int ch;
            bool hasInput = false;
            
            while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
                buffer += static_cast<char>(ch);
                hasInput = true;
            }
            
            if (ch == '\n' && !buffer.empty()) {
                Caffeine::Editor::TestRequestHandler::Request req;
                if (Caffeine::Editor::TestRequestHandler::tryParseRequest(buffer, req)) {
                    auto resp = Caffeine::Editor::TestRequestHandler::handleRequest(
                        req, world, ctx,
                        0, 0, 1280, 720
                    );
                    std::cout << "REQUEST_RESPONSE: " << resp.toJson() << std::endl;
                    std::cout.flush();
                }
                buffer.clear();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        return 0;
    }

    SDL_SetAppMetadata("Doppio", "0.0.1-beta", "com.devscafe.doppio");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Doppio  —  Caffeine Studio IDE  v0.0.1 Beta",
        1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Caffeine::RHI::RenderConfig cfg;
    cfg.width       = 1280;
    cfg.height      = 720;
    cfg.vsync       = true;
    cfg.windowTitle = "Doppio";

    Caffeine::RHI::RenderDevice device;
    if (!device.init(window, cfg)) {
        std::fprintf(stderr, "RenderDevice::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Caffeine::Assets::AssetManager assetManager(nullptr, "assets");

    Caffeine::Editor::ImGuiIntegration imgui;
    if (!imgui.init(window, &device)) {
        std::fprintf(stderr, "ImGuiIntegration::init failed\n");
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Caffeine::Editor::ProjectConfig selectedProject;
    selectedProject.Name = "TestProject";
    selectedProject.RootPath = std::filesystem::path(scenePath).parent_path();
    selectedProject.AssetRawPath = selectedProject.RootPath / "assets";
    
    if (scenePath.empty() || !std::filesystem::exists(scenePath)) {
        Caffeine::Editor::ProjectStartupDialog projectDialog;
        projectDialog.init();

        bool projectSelected = false;

        while (projectDialog.isOpen() && !projectSelected) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                imgui.processEvent(event);
                if (event.type == SDL_EVENT_QUIT) {
                    imgui.shutdown();
                    device.shutdown();
                    SDL_DestroyWindow(window);
                    SDL_Quit();
                    return 0;
                }
            }

            Caffeine::RHI::CommandBuffer* cmd = device.beginFrame();
            if (!cmd) {
                continue;
            }

            imgui.beginFrame();
            
            if (auto config = projectDialog.render()) {
                selectedProject = config.value();
                projectSelected = true;
            }
            imgui.prepareRender(cmd);

            Caffeine::RHI::RenderPassDesc passDesc;
            passDesc.clearColor[0] = 0.10f;
            passDesc.clearColor[1] = 0.10f;
            passDesc.clearColor[2] = 0.12f;
            passDesc.clearColor[3] = 1.00f;

            cmd->beginRenderPass(passDesc);
            imgui.endFrame(cmd);
            cmd->endRenderPass();

            device.endFrame(cmd);
            fflush(stderr);
        }

        if (!projectSelected) {
            imgui.shutdown();
            device.shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
    }

    // Create asset manager with project's asset path
    Caffeine::Assets::AssetManager projectAssetManager(nullptr, selectedProject.AssetRawPath.string().c_str());

    Caffeine::Editor::SceneEditor editor;
    if (!editor.init(&device, &projectAssetManager, selectedProject)) {
        std::fprintf(stderr, "SceneEditor::init failed\n");
        imgui.shutdown();
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }



    // Register editor debug hooks into core
    // (T0.4 — IDebugHooks will be wired here in a follow-up)

    bool running = true;
    Uint64 lastFrameTime = SDL_GetTicksNS();
    while (running && editor.isOpen()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui.processEvent(event);
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        Uint64 currentFrameTime = SDL_GetTicksNS();
        float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime) / 1'000'000'000.0f;
        lastFrameTime = currentFrameTime;

        Caffeine::RHI::CommandBuffer* cmd = device.beginFrame();
        if (!cmd) continue;

        imgui.beginFrame();
        editor.render(deltaTime);
        imgui.prepareRender(cmd);

        Caffeine::RHI::RenderPassDesc passDesc;
        passDesc.clearColor[0] = 0.10f;
        passDesc.clearColor[1] = 0.10f;
        passDesc.clearColor[2] = 0.12f;
        passDesc.clearColor[3] = 1.00f;

        cmd->beginRenderPass(passDesc);
        imgui.endFrame(cmd);
        cmd->endRenderPass();

        device.endFrame(cmd);
    }

    editor.shutdown();
    imgui.shutdown();
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
