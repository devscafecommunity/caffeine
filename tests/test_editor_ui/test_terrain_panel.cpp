#include "../Catch2/catch.hpp"

#include "../../src/ecs/TerrainComponents.hpp"
#include "../../src/ecs/World.hpp"
#include "../../src/editor/EditorContext.hpp"
#include "../../src/editor/TerrainPanel.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

extern bool g_gpuAvailable;

using namespace Caffeine;
using namespace Caffeine::Editor;

namespace {

void pumpTerrainPanelFrame(TerrainPanel& panel, ECS::World& world, EditorContext& ctx) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    panel.render(world, ctx);

    ImGui::Render();
}

}  // namespace

TEST_CASE("Terrain panel - opens with no terrain selected", "[editor][ui][terrain]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    ECS::World world;
    EditorContext ctx;
    TerrainPanel panel;

    pumpTerrainPanelFrame(panel, world, ctx);

    ImGuiWindow* window = ImGui::FindWindowByName("Terrain");
    REQUIRE(window != nullptr);
    REQUIRE(window->Active);
}

TEST_CASE("Terrain panel - renders sculpt and data tabs for a terrain entity",
          "[editor][ui][terrain]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    ECS::World world;
    ECS::Entity entity = world.create();
    world.add<ECS::TerrainComponent>(entity);

    EditorContext ctx;
    ctx.selectedEntity = entity;
    TerrainPanel panel;

    pumpTerrainPanelFrame(panel, world, ctx);

    ImGuiWindow* window = ImGui::FindWindowByName("Terrain");
    REQUIRE(window != nullptr);
    REQUIRE(window->Active);
}
