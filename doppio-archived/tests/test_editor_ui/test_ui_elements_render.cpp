#include "../Catch2/catch.hpp"

#include "caffeine/ui/UI.hpp"
#include "editor/CommandPalette.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

extern bool g_gpuAvailable;

using namespace Caffeine::UIKit;
using namespace Caffeine::UIKit::Elements;

TEST_CASE("UI element panel renders a full card tree", "[editor][ui][elements]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    Panel panel("Element Test", "Composable UI");
    auto& card = panel.emplace<Card>("General");
    auto& row = card.emplace<Row>("Actions");
    row.emplace<Button>("Primary", ButtonVariant::Primary).setId("primary");
    row.emplace<Button>("Secondary").setId("secondary");
    row.emplace<Button>("Danger", ButtonVariant::Danger).setId("danger");
    card.emplace<Label>("A compact card built from elements");

    auto& grid = card.emplace<PropertyGrid>("Properties");
    float value = 1.0f;
    bool enabled = true;
    grid.typedRow<FloatProperty>("Value", value, 0.0f, 10.0f);
    grid.typedRow<BoolProperty>("Enabled", enabled);
    auto& canvas = card.emplace<Canvas>("Preview");
    canvas.setHeight(80.0f);
    canvas.setOnDraw([](UIContext&, ImDrawList* drawList, const ImVec2& origin,
                        const ImVec2& size) {
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                IM_COL32(30, 40, 56, 255));
        drawList->AddCircleFilled(ImVec2(origin.x + size.x * 0.5f,
                                         origin.y + size.y * 0.5f),
                                  18.0f, IM_COL32(220, 90, 140, 255));
    });

    UIContext ctx;
    ctx.draw(panel);
    ImGui::Render();

    ImGuiWindow* window = ImGui::FindWindowByName("Element Test");
    REQUIRE(window != nullptr);
    REQUIRE(ctx.debugEntries().size() > 6);
    const std::string dump = ctx.debugDump();
    REQUIRE(dump.find("Panel #Element Test") != std::string::npos);
    REQUIRE(dump.find("Card #General") != std::string::npos);
    REQUIRE(dump.find("Button #primary") != std::string::npos);
    REQUIRE(dump.find("PropertyGrid #Properties") != std::string::npos);
    REQUIRE(dump.find("Canvas #Preview") != std::string::npos);
}

TEST_CASE("UI Dialog opens and renders a modal body", "[editor][ui][elements]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    using namespace Caffeine::UIKit::Elements;

    Dialog dialog("Test Dialog");
    dialog.setInitialSize(ImVec2(320.0f, 200.0f));
    dialog.emplace<Label>("Dialog body");
    dialog.open();

    UIContext ctx;
    ctx.draw(dialog);
    ImGui::Render();

    ImGuiWindow* window = ImGui::FindWindowByName("Test Dialog");
    REQUIRE(window != nullptr);
}


TEST_CASE("Command Palette opens a dialog window", "[editor][ui][elements]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    Caffeine::Editor::CommandPalette palette;
    palette.registerCommand("test.command", "Test Command", "Tools", [] {}, true);
    palette.open();
    palette.render();
    ImGui::Render();

    REQUIRE(ImGui::FindWindowByName("Quick Search") != nullptr);
}
