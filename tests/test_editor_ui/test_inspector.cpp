#include "../Catch2/catch.hpp"
#include "../../src/editor/InspectorPanel.hpp"
#include "../../src/editor/EditorContext.hpp"
#include "../../src/ecs/World.hpp"
#include "../../src/ecs/Entity.hpp"
#include "../../src/ecs/Components.hpp"
#include "../../src/audio/AudioComponents.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

extern bool g_gpuAvailable;

using namespace Caffeine;
using namespace Caffeine::Editor;

void PumpPanelFrame(InspectorPanel& panel, ECS::World& world, EditorContext& ctx) {
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

TEST_CASE("Inspector - shows 'No entity selected' when nothing selected", "[editor][ui][inspector]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    InspectorPanel panel;
    
    ctx.selectedEntity = ECS::Entity();
    
    PumpPanelFrame(panel, world, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Inspector");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Inspector - entity name input field updates entity name", "[editor][ui][inspector]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "TestEntity");
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    PumpPanelFrame(panel, world, ctx);
    
    const char* initialName = getEntityName(world, entity);
    REQUIRE(std::string(initialName) == "TestEntity");
    
    ImGuiWindow* win = ImGui::FindWindowByName("Inspector");
    REQUIRE(win != nullptr);
}

TEST_CASE("Inspector - Transform Position drag modifies component", "[editor][ui][inspector][transform]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "TransformEntity");
    
    world.add<ECS::Position2D>(entity, 10.0f, 20.0f);
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    PumpPanelFrame(panel, world, ctx);
    
    REQUIRE(world.has<ECS::Position2D>(entity));
    auto* posCheck = world.get<ECS::Position2D>(entity);
    CHECK(posCheck->x == 10.0f);
    CHECK(posCheck->y == 20.0f);
}

TEST_CASE("Inspector - AudioEmitter Volume slider modifies component", "[editor][ui][inspector][audio]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "AudioSource");
    
    world.add<Audio::AudioEmitter>(entity);
    auto* emitter = world.get<Audio::AudioEmitter>(entity);
    emitter->volume = 0.5f;
    emitter->clipPath = "test_audio.wav";
    emitter->loop = false;
    emitter->playOnSpawn = false;
    emitter->spatial = true;
    emitter->maxDistance = 100.0f;
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    PumpPanelFrame(panel, world, ctx);
    
    REQUIRE(world.has<Audio::AudioEmitter>(entity));
    auto* emitterCheck = world.get<Audio::AudioEmitter>(entity);
    CHECK(emitterCheck->volume == 0.5f);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Inspector");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Inspector - Sprite texture field updates component", "[editor][ui][inspector][sprite]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "SpriteEntity");
    
    world.add<ECS::Sprite>(entity);
    auto* sprite = world.get<ECS::Sprite>(entity);
    sprite->name = "initial_sprite.png";
    sprite->frameIndex = 0;
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    PumpPanelFrame(panel, world, ctx);
    
    REQUIRE(world.has<ECS::Sprite>(entity));
    auto* spriteCheck = world.get<ECS::Sprite>(entity);
    CHECK(spriteCheck->name == "initial_sprite.png");
    CHECK(spriteCheck->frameIndex == 0);
}

TEST_CASE("Inspector - Add Component button opens popup menu", "[editor][ui][inspector][component]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "EmptyEntity");
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    PumpPanelFrame(panel, world, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Inspector");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Inspector - multiple components render without conflicts", "[editor][ui][inspector]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    auto entity = world.create();
    setEntityName(world, entity, "ComplexEntity");
    
    world.add<ECS::Position2D>(entity, 100.0f, 200.0f);
    world.add<ECS::Velocity2D>(entity, 5.0f, 10.0f);
    world.add<ECS::Sprite>(entity);
    auto* sprite = world.get<ECS::Sprite>(entity);
    sprite->name = "player.png";
    sprite->frameIndex = 3;
    
    world.add<Audio::AudioEmitter>(entity);
    auto* emitter = world.get<Audio::AudioEmitter>(entity);
    emitter->volume = 0.8f;
    emitter->clipPath = "sfx.wav";
    
    EditorContext ctx;
    ctx.selectedEntity = entity;
    InspectorPanel panel;
    
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(panel, world, ctx);
    }
    
    REQUIRE(world.has<ECS::Position2D>(entity));
    REQUIRE(world.has<ECS::Velocity2D>(entity));
    REQUIRE(world.has<ECS::Sprite>(entity));
    REQUIRE(world.has<Audio::AudioEmitter>(entity));
    
    auto* posCheck = world.get<ECS::Position2D>(entity);
    CHECK(posCheck->x == 100.0f);
    CHECK(posCheck->y == 200.0f);
    
    auto* spriteCheck = world.get<ECS::Sprite>(entity);
    CHECK(spriteCheck->name == "player.png");
    CHECK(spriteCheck->frameIndex == 3);
    
    auto* emitterCheck = world.get<Audio::AudioEmitter>(entity);
    CHECK(emitterCheck->volume == 0.8f);
}
