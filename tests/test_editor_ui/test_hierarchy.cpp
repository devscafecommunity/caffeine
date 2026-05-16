#include "../Catch2/catch.hpp"
#include "../../src/editor/HierarchyPanel.hpp"
#include "../../src/editor/EditorContext.hpp"
#include "../../src/ecs/World.hpp"
#include "../../src/ecs/Entity.hpp"
#include "../../src/ecs/Components.hpp"
#include "../../src/scene/SceneComponents.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

extern bool g_gpuAvailable;

using namespace Caffeine;
using namespace Caffeine::Editor;

void PumpPanelFrame(HierarchyPanel& panel, ECS::World& world, EditorContext& ctx) {
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

TEST_CASE("Hierarchy - empty world renders without crashing", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    PumpPanelFrame(panel, world, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Hierarchy - entity listing shows created entities", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    // Create test entities
    auto entity1 = world.create();
    setEntityName(world, entity1, "TestEntity1");
    
    auto entity2 = world.create();
    setEntityName(world, entity2, "TestEntity2");
    
    auto entity3 = world.create();
    setEntityName(world, entity3, "Child");
    
    // Make entity3 a child of entity1
    auto& parentComp = world.add<Scene::Parent>(entity3);
    parentComp.parent = entity1;
    parentComp.dirty = false;
    
    PumpPanelFrame(panel, world, ctx);
    
    // Verify entities exist in the world
    const char* name1 = getEntityName(world, entity1);
    const char* name2 = getEntityName(world, entity2);
    const char* name3 = getEntityName(world, entity3);
    
    REQUIRE(std::string(name1) == "TestEntity1");
    REQUIRE(std::string(name2) == "TestEntity2");
    REQUIRE(std::string(name3) == "Child");
    
    // Verify window is active
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Hierarchy - selection updates EditorContext", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    auto entity = world.create();
    setEntityName(world, entity, "SelectableEntity");
    
    // Initially no entity selected
    REQUIRE(!ctx.selectedEntity.isValid());
    
    // Manually set selection to simulate click
    ctx.selectedEntity = entity;
    
    PumpPanelFrame(panel, world, ctx);
    
    // Verify selection is set
    REQUIRE(ctx.selectedEntity == entity);
    REQUIRE(ctx.selectedEntity.isValid());
    
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
}

TEST_CASE("Hierarchy - search filter updates", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    // Create multiple entities
    auto entity1 = world.create();
    setEntityName(world, entity1, "Player");
    
    auto entity2 = world.create();
    setEntityName(world, entity2, "Enemy");
    
    auto entity3 = world.create();
    setEntityName(world, entity3, "Platform");
    
    // Render multiple frames to ensure all entities are registered
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(panel, world, ctx);
    }
    
    // All entities should be in the world
    REQUIRE(world.has<NameComponent>(entity1));
    REQUIRE(world.has<NameComponent>(entity2));
    REQUIRE(world.has<NameComponent>(entity3));
    
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
}

TEST_CASE("Hierarchy - multiple entities render correctly", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    // Create 5 entities
    for (int i = 0; i < 5; ++i) {
        auto e = world.create();
        std::string name = "Entity_" + std::to_string(i);
        setEntityName(world, e, name.c_str());
    }
    
    // Render multiple frames
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(panel, world, ctx);
    }
    
    // Count entities with NameComponent
    int count = 0;
    ECS::ComponentQuery allQ;
    world.forEach<NameComponent>(allQ, [&](ECS::Entity, NameComponent&) {
        ++count;
    });
    
    REQUIRE(count == 5);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("Hierarchy - parent-child relationship renders", "[editor][ui][hierarchy]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    ECS::World world;
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    panel.setWorld(&world);
    
    auto parent = world.create();
    setEntityName(world, parent, "Parent");
    
    auto child1 = world.create();
    setEntityName(world, child1, "Child1");
    auto& p1 = world.add<Scene::Parent>(child1);
    p1.parent = parent;
    p1.dirty = false;
    
    auto child2 = world.create();
    setEntityName(world, child2, "Child2");
    auto& p2 = world.add<Scene::Parent>(child2);
    p2.parent = parent;
    p2.dirty = false;
    
    // Render frames
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(panel, world, ctx);
    }
    
    // Verify parent-child relationships
    REQUIRE(world.has<Scene::Parent>(child1));
    REQUIRE(world.has<Scene::Parent>(child2));
    
    auto* pc1 = world.get<Scene::Parent>(child1);
    auto* pc2 = world.get<Scene::Parent>(child2);
    
    REQUIRE(pc1->parent == parent);
    REQUIRE(pc2->parent == parent);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Hierarchy");
    REQUIRE(win != nullptr);
}
