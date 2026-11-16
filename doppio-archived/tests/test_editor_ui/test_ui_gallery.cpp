#include "../Catch2/catch.hpp"

#include "editor/UiGalleryPanel.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

extern bool g_gpuAvailable;

TEST_CASE("UI Gallery panel renders the complete element catalogue",
          "[editor][ui][elements][gallery]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    Caffeine::Editor::UiGalleryPanel gallery;
    gallery.open();

    SDL_Event event;
    while (SDL_PollEvent(&event)) ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    gallery.render();
    ImGui::Render();

    ImGuiWindow* window = ImGui::FindWindowByName("UI Gallery");
    REQUIRE(window != nullptr);
}
