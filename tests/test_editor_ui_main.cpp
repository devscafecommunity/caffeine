// ============================================================================
// @file    test_editor_ui_main.cpp
// @brief   DoppioTest - Test harness for Doppio editor UI
// @note    Integrates Catch2 + imgui_test_engine for widget-level testing
// ============================================================================

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_ui.h>

#include "rhi/RenderDevice.hpp"
#include "assets/AssetManager.hpp"
#include "editor/SceneEditor.hpp"
#include "editor/ProjectManager.hpp"
#include "render/Camera2D.hpp"

#define CATCH_CONFIG_RUNNER
#include "Catch2/catch.hpp"

#include <cstdio>

// ============================================================================
// Global test state
// ============================================================================

static struct {
    SDL_Window* window = nullptr;
    Caffeine::RHI::RenderDevice* device = nullptr;
    Caffeine::Assets::AssetManager* assetManager = nullptr;
    Caffeine::Editor::SceneEditor* editor = nullptr;
    Caffeine::Render::Camera2D* camera = nullptr;
    ImGuiTestEngine* testEngine = nullptr;
    Uint64 lastFrameTime = SDL_GetTicksNS();
    bool gpuAvailable = false;
} s_state;

bool g_gpuAvailable = false;

void PumpFrame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    if (s_state.gpuAvailable) {
        ImGui_ImplSDLGPU3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    Uint64 currentFrameTime = SDL_GetTicksNS();
    float deltaTime = static_cast<float>(currentFrameTime - s_state.lastFrameTime) / 1'000'000'000.0f;
    s_state.lastFrameTime = currentFrameTime;

    if (s_state.editor && s_state.gpuAvailable) {
        s_state.editor->render(deltaTime);
    }

    ImGui::Render();
}

// ============================================================================
// @brief  Main entry point — initializes SDL, ImGui, SceneEditor, test engine
// ============================================================================

int main(int argc, char* argv[]) {
    // ── Initialize SDL ──────────────────────────────────────────────────────
    
    SDL_SetAppMetadata("DoppioTest", "0.1.0", "com.devscafe.doppio.test");
    
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create hidden window (GPU backends need a window, even if not visible)
    s_state.window = SDL_CreateWindow(
        "DoppioTest", 1280, 720,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    
    if (!s_state.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // ── Initialize RenderDevice ─────────────────────────────────────────────
    
    Caffeine::RHI::RenderConfig cfg;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;  // Faster test execution
    cfg.windowTitle = "DoppioTest";

    s_state.device = new Caffeine::RHI::RenderDevice();
    if (!s_state.device->init(s_state.window, cfg)) {
        std::fprintf(stderr, "[WARN] RenderDevice::init failed — continuing without GPU\n");
        std::fprintf(stderr, "       (This is expected in headless CI environments)\n");
        s_state.gpuAvailable = false;
        g_gpuAvailable = false;
    } else {
        s_state.gpuAvailable = true;
        g_gpuAvailable = true;
    }

    // ── Initialize ImGui with backends ──────────────────────────────────────
    
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Disable .ini file for tests (avoid side effects)
    io.IniFilename = nullptr;

    if (s_state.gpuAvailable) {
        ImGui_ImplSDL3_InitForSDLGPU(s_state.window);
        
        ImGui_ImplSDLGPU3_InitInfo initInfo{};
        initInfo.Device = s_state.device->nativeDevice();
        initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(
            s_state.device->nativeDevice(), s_state.window);
        ImGui_ImplSDLGPU3_Init(&initInfo);
    } else {
        ImGui_ImplSDL3_InitForOther(s_state.window);
    }

    // ── Initialize AssetManager and SceneEditor ─────────────────────────────
    
    s_state.assetManager = new Caffeine::Assets::AssetManager(nullptr, "assets");
    s_state.editor = new Caffeine::Editor::SceneEditor();
    
    if (s_state.gpuAvailable) {
        // Create a minimal ProjectConfig for testing
        Caffeine::Editor::ProjectConfig testProject;
        testProject.Name = "TestProject";
        testProject.RootPath = "./test_project";
        testProject.AssetRawPath = "assets";
        testProject.TemplateType = "Empty";
        
        if (!s_state.editor->init(s_state.device, s_state.assetManager, testProject)) {
            std::fprintf(stderr, "SceneEditor::init failed\n");
            delete s_state.editor;
            delete s_state.assetManager;
            if (s_state.gpuAvailable) {
                ImGui_ImplSDLGPU3_Shutdown();
            }
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            s_state.device->shutdown();
            delete s_state.device;
            SDL_DestroyWindow(s_state.window);
            SDL_Quit();
            return 1;
        }
    } else {
        std::fprintf(stderr, "[INFO] Skipping SceneEditor init (GPU not available)\n");
        std::fprintf(stderr, "[INFO] GUI-dependent tests will be skipped\n");
    }

    s_state.camera = new Caffeine::Render::Camera2D();

    // ── Initialize imgui_test_engine ────────────────────────────────────────
    
    s_state.testEngine = ImGuiTestEngine_CreateContext();
    if (s_state.testEngine) {
        ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(s_state.testEngine);
        test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
        test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;

        ImGuiTestEngine_Start(s_state.testEngine, ImGui::GetCurrentContext());
    } else {
        std::fprintf(stderr, "[WARN] ImGuiTestEngine_CreateContext failed\n");
    }

    // ── Run Catch2 tests ────────────────────────────────────────────────────
    
    std::printf("[DoppioTest] Running UI tests...\n");
    std::printf("[DoppioTest] GPU available: %s\n", s_state.gpuAvailable ? "yes" : "no");
    
    int result = Catch::Session().run(argc, argv);

    // ── Cleanup ─────────────────────────────────────────────────────────────
    
    if (s_state.testEngine) {
        ImGuiTestEngine_Stop(s_state.testEngine);
    }

    if (s_state.editor) {
        s_state.editor->shutdown();
        delete s_state.editor;
    }
    delete s_state.assetManager;
    delete s_state.camera;

    if (s_state.gpuAvailable) {
        ImGui_ImplSDLGPU3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (s_state.testEngine) {
        ImGuiTestEngine_DestroyContext(s_state.testEngine);
    }

    if (s_state.gpuAvailable) {
        s_state.device->shutdown();
    }
    delete s_state.device;

    SDL_DestroyWindow(s_state.window);
    SDL_Quit();

    return result;
}
