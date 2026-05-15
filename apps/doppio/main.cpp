#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include "assets/AssetManager.hpp"
#include "render/Camera2D.hpp"

#include "editor/ImGuiIntegration.hpp"
#include "editor/SceneEditor.hpp"
#include <SDL3/SDL.h>
#include <cstdio>

int main(int, char**) {
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
    Caffeine::Render::Camera2D editorCamera;

    Caffeine::Editor::ImGuiIntegration imgui;
    if (!imgui.init(window, &device)) {
        std::fprintf(stderr, "ImGuiIntegration::init failed\n");
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Caffeine::Editor::SceneEditor editor;
    if (!editor.init(&device, &assetManager)) {
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
    while (running && editor.isOpen()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui.processEvent(event);
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        Caffeine::RHI::CommandBuffer* cmd = device.beginFrame();
        if (!cmd) continue;

        imgui.beginFrame();
        editor.render(editorCamera);
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
