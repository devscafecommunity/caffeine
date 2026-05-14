#include "catch.hpp"
#include "../src/core/Types.hpp"
#include "../src/core/IDebugHooks.hpp"
#include "../src/ecs/World.hpp"
#include "../src/ecs/Entity.hpp"
#include "../src/debug/LogSystem.hpp"
#include "../src/editor/EditorTypes.hpp"
#include "../src/editor/EditorContext.hpp"
#include "../src/editor/ConsoleWindow.hpp"
#include "../src/editor/ProfilerWindow.hpp"
#include "../src/editor/StatsOverlay.hpp"
#include "../src/editor/HierarchyPanel.hpp"

using namespace Caffeine;
using namespace Caffeine::Editor;

TEST_CASE("FrameStats - default values", "[editor]") {
    FrameStats stats;
    REQUIRE(stats.deltaTime == 0.0);
    REQUIRE(stats.fps == 0.0);
    REQUIRE(stats.frameCount == 0);
    REQUIRE(stats.elapsedTime == 0.0);
}

TEST_CASE("ConsoleWindow - default state", "[editor]") {
    ConsoleWindow console;
    REQUIRE(console.entryCount() == 0);
    REQUIRE(console.isOpen() == true);
    REQUIRE(console.autoScroll() == true);
    REQUIRE(console.filterLevel() == Debug::LogLevel::Trace);
}

TEST_CASE("ConsoleWindow - addLog increases entry count", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Info, "Test", "Message");
    REQUIRE(console.entryCount() == 1);
}

TEST_CASE("ConsoleWindow - addLog stores level correctly", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Warn, "Test", "Warning");
    REQUIRE(console.entry(0).level == Debug::LogLevel::Warn);
}

TEST_CASE("ConsoleWindow - addLog stores category correctly", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Info, "TestCat", "Message");
    const auto& entry = console.entry(0);
    REQUIRE(entry.category == FixedString<32>("TestCat"));
}

TEST_CASE("ConsoleWindow - addLog stores message correctly", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Info, "Cat", "TestMessage");
    const auto& entry = console.entry(0);
    REQUIRE(entry.message == FixedString<256>("TestMessage"));
}

TEST_CASE("ConsoleWindow - clear empties entries", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Info, "Cat", "Msg1");
    console.addLog(Debug::LogLevel::Info, "Cat", "Msg2");
    REQUIRE(console.entryCount() == 2);
    console.clear();
    REQUIRE(console.entryCount() == 0);
}

TEST_CASE("ConsoleWindow - filterLevel default is Trace", "[editor]") {
    ConsoleWindow console;
    REQUIRE(console.filterLevel() == Debug::LogLevel::Trace);
}

TEST_CASE("ConsoleWindow - setFilterLevel changes filter", "[editor]") {
    ConsoleWindow console;
    console.setFilterLevel(Debug::LogLevel::Error);
    REQUIRE(console.filterLevel() == Debug::LogLevel::Error);
}

TEST_CASE("ConsoleWindow - autoScroll default is true", "[editor]") {
    ConsoleWindow console;
    REQUIRE(console.autoScroll() == true);
}

TEST_CASE("ConsoleWindow - setAutoScroll changes state", "[editor]") {
    ConsoleWindow console;
    console.setAutoScroll(false);
    REQUIRE(console.autoScroll() == false);
}

TEST_CASE("ConsoleWindow - open close", "[editor]") {
    ConsoleWindow console;
    REQUIRE(console.isOpen() == true);
    console.close();
    REQUIRE(console.isOpen() == false);
    console.open();
    REQUIRE(console.isOpen() == true);
}

TEST_CASE("ConsoleWindow - isOpen default is true", "[editor]") {
    ConsoleWindow console;
    REQUIRE(console.isOpen() == true);
}

TEST_CASE("ProfilerWindow - default not paused", "[editor]") {
    ProfilerWindow profiler;
    REQUIRE(profiler.isPaused() == false);
}

TEST_CASE("ProfilerWindow - pushFrameTime advances frameIndex", "[editor]") {
    ProfilerWindow profiler;
    REQUIRE(profiler.frameIndex() == 0);
    profiler.pushFrameTime(16.0f);
    REQUIRE(profiler.frameIndex() == 1);
}

TEST_CASE("ProfilerWindow - pause stops frameIndex advancing", "[editor]") {
    ProfilerWindow profiler;
    profiler.pushFrameTime(16.0f);
    REQUIRE(profiler.frameIndex() == 1);
    profiler.pause();
    profiler.pushFrameTime(16.0f);
    REQUIRE(profiler.frameIndex() == 1);
}

TEST_CASE("ProfilerWindow - resume restarts advancing", "[editor]") {
    ProfilerWindow profiler;
    profiler.pause();
    profiler.pushFrameTime(16.0f);
    REQUIRE(profiler.frameIndex() == 0);
    profiler.resume();
    profiler.pushFrameTime(16.0f);
    REQUIRE(profiler.frameIndex() == 1);
}

TEST_CASE("ProfilerWindow - lastFrameTime returns 0 when empty", "[editor]") {
    ProfilerWindow profiler;
    REQUIRE(profiler.lastFrameTime() == 0.0f);
}

TEST_CASE("ProfilerWindow - lastFrameTime returns pushed value", "[editor]") {
    ProfilerWindow profiler;
    profiler.pushFrameTime(16.5f);
    REQUIRE(profiler.lastFrameTime() == 16.5f);
}

TEST_CASE("ProfilerWindow - pushFrameTime wraps at 120", "[editor]") {
    ProfilerWindow profiler;
    for (u32 i = 0; i < 121; ++i) {
        profiler.pushFrameTime(static_cast<f32>(i));
    }
    REQUIRE(profiler.frameIndex() == 121);
    REQUIRE(profiler.frameTimes()[0] == 120.0f);
}

TEST_CASE("ProfilerWindow - open close", "[editor]") {
    ProfilerWindow profiler;
    REQUIRE(profiler.isOpen() == true);
    profiler.close();
    REQUIRE(profiler.isOpen() == false);
    profiler.open();
    REQUIRE(profiler.isOpen() == true);
}

TEST_CASE("StatsOverlay - default isOpen true", "[editor]") {
    StatsOverlay stats;
    REQUIRE(stats.isOpen() == true);
}

TEST_CASE("StatsOverlay - close open", "[editor]") {
    StatsOverlay stats;
    stats.close();
    REQUIRE(stats.isOpen() == false);
    stats.open();
    REQUIRE(stats.isOpen() == true);
}

TEST_CASE("ConsoleWindow - multiple log entries with different levels", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Trace, "Cat", "Trace");
    console.addLog(Debug::LogLevel::Info, "Cat", "Info");
    console.addLog(Debug::LogLevel::Warn, "Cat", "Warn");
    console.addLog(Debug::LogLevel::Error, "Cat", "Error");
    REQUIRE(console.entryCount() == 4);
    REQUIRE(console.entry(0).level == Debug::LogLevel::Trace);
    REQUIRE(console.entry(1).level == Debug::LogLevel::Info);
    REQUIRE(console.entry(2).level == Debug::LogLevel::Warn);
    REQUIRE(console.entry(3).level == Debug::LogLevel::Error);
}

TEST_CASE("ConsoleWindow - entry accessor returns correct entry", "[editor]") {
    ConsoleWindow console;
    console.addLog(Debug::LogLevel::Info, "Cat1", "Msg1");
    console.addLog(Debug::LogLevel::Warn, "Cat2", "Msg2");
    const auto& e0 = console.entry(0);
    const auto& e1 = console.entry(1);
    REQUIRE(e0.category == FixedString<32>("Cat1"));
    REQUIRE(e0.message == FixedString<256>("Msg1"));
    REQUIRE(e1.category == FixedString<32>("Cat2"));
    REQUIRE(e1.message == FixedString<256>("Msg2"));
}

// ============================================================================
// EditorContext Tests
// ============================================================================

TEST_CASE("EditorContext - Default State", "[editor][context]") {
    EditorContext ctx;
    REQUIRE(ctx.selectedEntity.isValid() == false);
    REQUIRE(ctx.hoveredEntity.isValid() == false);
    REQUIRE(ctx.isDirty == false);
    REQUIRE(static_cast<int>(ctx.gizmoMode) == static_cast<int>(EditorContext::GizmoMode::Translate));
    REQUIRE(static_cast<int>(ctx.gizmoSpace) == static_cast<int>(EditorContext::GizmoSpace::World));
    REQUIRE(ctx.viewportZoom == 1.0f);
}

TEST_CASE("EditorContext - Gizmo Mode Transitions", "[editor][context]") {
    EditorContext ctx;
    ctx.gizmoMode = EditorContext::GizmoMode::None;
    REQUIRE(static_cast<int>(ctx.gizmoMode) == static_cast<int>(EditorContext::GizmoMode::None));
    ctx.gizmoMode = EditorContext::GizmoMode::Rotate;
    REQUIRE(static_cast<int>(ctx.gizmoMode) == static_cast<int>(EditorContext::GizmoMode::Rotate));
    ctx.gizmoMode = EditorContext::GizmoMode::Scale;
    REQUIRE(static_cast<int>(ctx.gizmoMode) == static_cast<int>(EditorContext::GizmoMode::Scale));
}

// ============================================================================
// NameComponent Tests
// ============================================================================

TEST_CASE("NameComponent - Default Name", "[editor][name]") {
    ECS::World world;
    ECS::Entity e = world.create();
    const char* name = getEntityName(world, e);
    REQUIRE(name != nullptr);
    REQUIRE(strcmp(name, "Unnamed") == 0);
}

TEST_CASE("NameComponent - Set and Get Name", "[editor][name]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Hero");
    REQUIRE(strcmp(getEntityName(world, e), "Hero") == 0);
}

TEST_CASE("NameComponent - Update Name", "[editor][name]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Villain");
    REQUIRE(strcmp(getEntityName(world, e), "Villain") == 0);
    setEntityName(world, e, "AntiHero");
    REQUIRE(strcmp(getEntityName(world, e), "AntiHero") == 0);
}

TEST_CASE("NameComponent - Long Name Truncation", "[editor][name]") {
    ECS::World world;
    ECS::Entity e = world.create();
    const char* longName = "ThisIsAVeryLongEntityNameThatShouldDefinitelyExceedTheSixtyFourCharacterBufferLimitOfTheNameComponent";
    setEntityName(world, e, longName);
    const char* stored = getEntityName(world, e);
    REQUIRE(stored != nullptr);
    REQUIRE(strlen(stored) == 63);
}

TEST_CASE("NameComponent - Multiple Entities", "[editor][name]") {
    ECS::World world;
    ECS::Entity e1 = world.create();
    ECS::Entity e2 = world.create();
    ECS::Entity e3 = world.create();
    setEntityName(world, e1, "Player");
    setEntityName(world, e2, "Enemy");
    setEntityName(world, e3, "NPC");
    REQUIRE(strcmp(getEntityName(world, e1), "Player") == 0);
    REQUIRE(strcmp(getEntityName(world, e2), "Enemy") == 0);
    REQUIRE(strcmp(getEntityName(world, e3), "NPC") == 0);
    // Verify independence
    setEntityName(world, e1, "Hero");
    REQUIRE(strcmp(getEntityName(world, e1), "Hero") == 0);
    REQUIRE(strcmp(getEntityName(world, e2), "Enemy") == 0);
    REQUIRE(strcmp(getEntityName(world, e3), "NPC") == 0);
}

// ============================================================================
// HierarchyPanel Tests
// ============================================================================

TEST_CASE("HierarchyPanel - Constructor stores context", "[editor][hierarchy]") {
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    REQUIRE(panel.context() == &ctx);
}

TEST_CASE("HierarchyPanel - Default state", "[editor][hierarchy]") {
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    REQUIRE(panel.isOpen() == true);
}

TEST_CASE("HierarchyPanel - setContext updates pointer", "[editor][hierarchy]") {
    EditorContext ctx1;
    EditorContext ctx2;
    HierarchyPanel panel(&ctx1);
    REQUIRE(panel.context() == &ctx1);
    panel.setContext(&ctx2);
    REQUIRE(panel.context() == &ctx2);
}

TEST_CASE("HierarchyPanel - close and open", "[editor][hierarchy]") {
    EditorContext ctx;
    HierarchyPanel panel(&ctx);
    REQUIRE(panel.isOpen() == true);
    panel.close();
    REQUIRE(panel.isOpen() == false);
    panel.open();
    REQUIRE(panel.isOpen() == true);
}
