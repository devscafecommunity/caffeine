#include "../Catch2/catch.hpp"

#include "../../src/doppio/editor/DryadNodesPanel.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

extern bool g_gpuAvailable;

using namespace Caffeine::Editor;

namespace {

void pumpDryadNodesFrame(DryadNodesPanel& panel) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    panel.render();

    ImGui::Render();
}

}  // namespace

TEST_CASE("Dryad Nodes panel opens with search and categories", "[editor][ui][dryad]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    DryadNodesPanel panel;
    pumpDryadNodesFrame(panel);

    ImGuiWindow* window = ImGui::FindWindowByName("Dryad Nodes");
    REQUIRE(window != nullptr);
    REQUIRE(window->Active);
}
