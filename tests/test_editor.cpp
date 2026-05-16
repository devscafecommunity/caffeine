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
#include "../src/editor/SceneViewport.hpp"
#include "../src/editor/AssetBrowser.hpp"
#include "../src/editor/InspectorPanel.hpp"
#include "../src/editor/SceneEditor.hpp"
#include "../src/editor/SceneSerializer.hpp"
#include "../src/editor/DragDropSystem.hpp"
#include "../src/editor/ProjectManager.hpp"
#include "../src/audio/AudioComponents.hpp"
#include <fstream>

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

// ============================================================================
// AssetBrowser Tests
// ============================================================================

TEST_CASE("AssetBrowser - Default state", "[editor][assetbrowser]") {
    AssetBrowser browser;
    REQUIRE(browser.isOpen() == true);
}

TEST_CASE("AssetBrowser - close and open", "[editor][assetbrowser]") {
    AssetBrowser browser;
    REQUIRE(browser.isOpen() == true);
    browser.close();
    REQUIRE(browser.isOpen() == false);
    browser.open();
    REQUIRE(browser.isOpen() == true);
}

// ── Test fixture for AssetBrowser data-layer tests ──────────────────

namespace {

struct AssetBrowserFixture {
    std::filesystem::path tmpDir;

    AssetBrowserFixture()
        : tmpDir(std::filesystem::temp_directory_path() / "caffeine_ab_test")
    {
        std::filesystem::remove_all(tmpDir);
        std::filesystem::create_directories(tmpDir);

        // Create some test files
        {
            std::ofstream(tmpDir / "player.png")  << "fake png";
            std::ofstream(tmpDir / "theme.wav")   << "fake wav";
            std::ofstream(tmpDir / "enemy.obj")   << "fake obj";
            std::ofstream(tmpDir / "level.caf")   << "fake scene";
            std::ofstream(tmpDir / "readme.txt")  << "notes";
            std::filesystem::create_directory(tmpDir / "subdir");
            std::ofstream(tmpDir / "subdir" / "nested.txt") << "nested";
            std::filesystem::create_directory(tmpDir / "emptydir");
        }
    }

    ~AssetBrowserFixture() {
        std::filesystem::remove_all(tmpDir);
    }
};

} // anonymous namespace

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - refresh populates entries",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    browser.init(tmpDir.string().c_str());
    REQUIRE(browser.entryCount() > 0);
    REQUIRE(browser.entryCount() <= 7); // 5 files + 2 dirs (but filtered if search active etc)
    REQUIRE_FALSE(browser.searchFilter().empty() == false); // default filter is empty
    REQUIRE(browser.searchFilter().empty());
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - search filter narrows results",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    browser.init(tmpDir.string().c_str());
    usize totalBefore = browser.entryCount();

    browser.setSearchFilter("player");
    usize filteredCount = browser.entryCount();
    REQUIRE(filteredCount < totalBefore);
    REQUIRE(filteredCount > 0);
    REQUIRE(browser.searchFilter() == "player");

    // Verify filtered entry is the png
    for (const auto& e : browser.entries()) {
        REQUIRE(e.name.find("player") != std::string::npos);
    }

    // Clear filter restores all entries
    browser.setSearchFilter("");
    REQUIRE(browser.entryCount() == totalBefore);
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - directory navigation",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    browser.init(tmpDir.string().c_str());

    auto subdir = tmpDir / "subdir";
    REQUIRE(std::filesystem::exists(subdir));

    browser.navigateTo(subdir);
    REQUIRE(browser.currentPath() == subdir);
    REQUIRE(browser.canGoBack() == true);
    REQUIRE(browser.entryCount() >= 1);

    // Should see nested.txt
    bool foundNested = false;
    for (const auto& e : browser.entries()) {
        if (e.name == "nested.txt") { foundNested = true; break; }
    }
    REQUIRE(foundNested == true);
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - back navigation",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    browser.init(tmpDir.string().c_str());
    auto root = browser.currentPath();

    browser.navigateTo(tmpDir / "subdir");
    REQUIRE(browser.currentPath() != root);

    browser.navigateBack();
    REQUIRE(browser.currentPath() == root);
    REQUIRE(browser.canGoBack() == false);
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - view mode toggle",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    REQUIRE(browser.viewMode() == AssetBrowser::ViewMode::Grid);

    browser.setViewMode(AssetBrowser::ViewMode::List);
    REQUIRE(browser.viewMode() == AssetBrowser::ViewMode::List);

    browser.setViewMode(AssetBrowser::ViewMode::Grid);
    REQUIRE(browser.viewMode() == AssetBrowser::ViewMode::Grid);
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - thumbnail size get/set",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    REQUIRE(browser.thumbnailSize() == 64);

    browser.setThumbnailSize(128);
    REQUIRE(browser.thumbnailSize() == 128);

    browser.setThumbnailSize(32);
    REQUIRE(browser.thumbnailSize() == 32);
}

TEST_CASE_METHOD(AssetBrowserFixture, "AssetBrowser - entry metadata correct",
                 "[editor][assetbrowser]") {
    AssetBrowser browser;
    browser.init(tmpDir.string().c_str());

    // Find player.png
    bool foundPng = false;
    for (const auto& e : browser.entries()) {
        if (e.name == "player.png") {
            foundPng = true;
            REQUIRE(e.isDirectory == false);
            REQUIRE(e.type == AssetType::Texture);
            REQUIRE(e.fileSize > 0);
            REQUIRE(e.path.filename().string() == "player.png");
        }
        if (e.name == "subdir") {
            REQUIRE(e.isDirectory == true);
            REQUIRE(e.type == AssetType::Unknown);
            REQUIRE(e.fileSize == 0);
        }
    }
    REQUIRE(foundPng == true);
}

// ============================================================================
// InspectorPanel Tests
// ============================================================================

TEST_CASE("InspectorPanel - Default state", "[editor][inspector]") {
    InspectorPanel panel;
    REQUIRE(panel.isOpen() == true);
}

TEST_CASE("InspectorPanel - close and open", "[editor][inspector]") {
    InspectorPanel panel;
    REQUIRE(panel.isOpen() == true);
    panel.close();
    REQUIRE(panel.isOpen() == false);
    panel.open();
    REQUIRE(panel.isOpen() == true);
}

// ============================================================================
// SceneViewport Tests
// ============================================================================

TEST_CASE("SceneViewport - Default state", "[editor][viewport]") {
    SceneViewport viewport;
    REQUIRE(viewport.isOpen() == true);
}

TEST_CASE("SceneViewport - close and open", "[editor][viewport]") {
    SceneViewport viewport;
    REQUIRE(viewport.isOpen() == true);
    viewport.close();
    REQUIRE(viewport.isOpen() == false);
    viewport.open();
    REQUIRE(viewport.isOpen() == true);
}

// ============================================================================
// SceneEditor Tests
// ============================================================================

TEST_CASE("SceneEditor - Default state", "[editor][sceneeditor]") {
    SceneEditor editor;
    REQUIRE(editor.isOpen() == true);
}

TEST_CASE("SceneEditor - close and open", "[editor][sceneeditor]") {
    SceneEditor editor;
    REQUIRE(editor.isOpen() == true);
    editor.close();
    REQUIRE(editor.isOpen() == false);
    editor.open();
    REQUIRE(editor.isOpen() == true);
}

TEST_CASE("SceneEditor - accessors return valid panels", "[editor][sceneeditor]") {
    SceneEditor editor;
    REQUIRE(editor.context().selectedEntity.isValid() == false);
    REQUIRE(editor.hierarchy().isOpen() == true);
    REQUIRE(editor.inspector().isOpen() == true);
    REQUIRE(editor.viewport().isOpen() == true);
    REQUIRE(editor.assetBrowser().isOpen() == true);
    REQUIRE(editor.console().isOpen() == true);
    REQUIRE(editor.profiler().isOpen() == true);
}

// ============================================================================
// SceneSerializer Tests
// ============================================================================

TEST_CASE("SceneSerializer - empty world roundtrip", "[editor][serializer]") {
    ECS::World world;
    Editor::SceneSerializer serializer(world);

    REQUIRE(serializer.serialize("_test_empty.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_empty.caf") == true);
    REQUIRE(loaded.entityCount() == 0);

    std::remove("_test_empty.caf");
}

TEST_CASE("SceneSerializer - single entity with Name and Position", "[editor][serializer]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Hero");
    world.add<ECS::Position2D>(e, 10.0f, 20.0f);

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_hero.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_hero.caf") == true);
    REQUIRE(loaded.entityCount() >= 1);

    // Verify entity has both Name and Position
    ECS::ComponentQuery q;
    q.with<ECS::Position2D>();
    bool found = false;
    loaded.forEach<ECS::Position2D>(q, [&](ECS::Entity ent, ECS::Position2D& pos) {
        REQUIRE(pos.x == 10.0f);
        REQUIRE(pos.y == 20.0f);
        const char* name = getEntityName(loaded, ent);
        REQUIRE(strcmp(name, "Hero") == 0);
        found = true;
    });
    REQUIRE(found == true);

    std::remove("_test_hero.caf");
}

TEST_CASE("SceneSerializer - entity with all component types", "[editor][serializer]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Complete");
    world.add<ECS::Position2D>(e, 1.0f, 2.0f);
    world.add<ECS::Velocity2D>(e, 3.0f, 4.0f);
    world.add<ECS::Acceleration2D>(e, 5.0f, 6.0f);
    world.add<ECS::Rotation>(e, 1.5f);
    world.add<ECS::Scale2D>(e, 2.0f, 2.0f);
    world.add<ECS::Health>(e, 50, 100);
    world.add<ECS::Tag>(e);

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_complete.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_complete.caf") == true);
    REQUIRE(loaded.entityCount() >= 1);

    ECS::ComponentQuery q;
    q.with<ECS::Position2D>();
    loaded.forEach<ECS::Position2D>(q, [&](ECS::Entity ent, ECS::Position2D& pos) {
        REQUIRE(pos.x == 1.0f);
        REQUIRE(pos.y == 2.0f);

        auto* vel = loaded.get<ECS::Velocity2D>(ent);
        REQUIRE(vel != nullptr);
        REQUIRE(vel->x == 3.0f);
        REQUIRE(vel->y == 4.0f);

        auto* accel = loaded.get<ECS::Acceleration2D>(ent);
        REQUIRE(accel != nullptr);
        REQUIRE(accel->x == 5.0f);
        REQUIRE(accel->y == 6.0f);

        auto* rot = loaded.get<ECS::Rotation>(ent);
        REQUIRE(rot != nullptr);
        REQUIRE(rot->angle == 1.5f);

        auto* scale = loaded.get<ECS::Scale2D>(ent);
        REQUIRE(scale != nullptr);
        REQUIRE(scale->x == 2.0f);
        REQUIRE(scale->y == 2.0f);

        auto* hp = loaded.get<ECS::Health>(ent);
        REQUIRE(hp != nullptr);
        REQUIRE(hp->current == 50);
        REQUIRE(hp->max == 100);

        REQUIRE(loaded.has<ECS::Tag>(ent) == true);

        const char* name = getEntityName(loaded, ent);
        REQUIRE(strcmp(name, "Complete") == 0);
    });

    std::remove("_test_complete.caf");
}

TEST_CASE("SceneSerializer - Sprite component serialization", "[editor][serializer]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "SpriteObj");
    world.add<ECS::Sprite>(e, "hero_sprite.png", 0);

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_sprite.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_sprite.caf") == true);

    ECS::ComponentQuery q;
    q.with<ECS::Sprite>();
    bool found = false;
    loaded.forEach<ECS::Sprite>(q, [&](ECS::Entity ent, ECS::Sprite& sprite) {
        (void)ent;
        REQUIRE(sprite.name == "hero_sprite.png");
        REQUIRE(sprite.frameIndex == 0);
        found = true;
    });
    REQUIRE(found == true);

    std::remove("_test_sprite.caf");
}

TEST_CASE("SceneSerializer - multiple entities roundtrip", "[editor][serializer]") {
    ECS::World world;
    ECS::Entity e1 = world.create();
    setEntityName(world, e1, "Player");
    world.add<ECS::Position2D>(e1, 0.0f, 0.0f);

    ECS::Entity e2 = world.create();
    setEntityName(world, e2, "Enemy");
    world.add<ECS::Position2D>(e2, 100.0f, 200.0f);
    world.add<ECS::Health>(e2, 30, 30);

    ECS::Entity e3 = world.create();
    setEntityName(world, e3, "Coin");
    world.add<ECS::Tag>(e3);

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_multi.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_multi.caf") == true);
    REQUIRE(loaded.entityCount() == 3);

    // Verify all three entities exist and have correct data
    ECS::ComponentQuery q;
    q.with<ECS::Position2D>();
    int posCount = 0;
    loaded.forEach<ECS::Position2D>(q, [&](ECS::Entity ent, ECS::Position2D& pos) {
        ++posCount;
        const char* name = getEntityName(loaded, ent);
        if (strcmp(name, "Player") == 0) {
            REQUIRE(pos.x == 0.0f);
            REQUIRE(pos.y == 0.0f);
        } else if (strcmp(name, "Enemy") == 0) {
            REQUIRE(pos.x == 100.0f);
            REQUIRE(pos.y == 200.0f);
            auto* hp = loaded.get<ECS::Health>(ent);
            REQUIRE(hp != nullptr);
            REQUIRE(hp->current == 30);
        }
    });
    REQUIRE(posCount == 2);

    // Check Tag entity
    int tagCount = 0;
    ECS::ComponentQuery tagQ;
    tagQ.with<ECS::Tag>();
    loaded.forEach<ECS::Tag>(tagQ, [&](ECS::Entity ent, ECS::Tag&) {
        ++tagCount;
        const char* name = getEntityName(loaded, ent);
        REQUIRE(strcmp(name, "Coin") == 0);
    });
    REQUIRE(tagCount == 1);

    std::remove("_test_multi.caf");
}

TEST_CASE("SceneSerializer - deserialize from non-existent file", "[editor][serializer]") {
    ECS::World world;
    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.deserialize("_non_existent_file.caf") == false);
}

// ============================================================================
// DragDropSystem Tests
// ============================================================================

TEST_CASE("DragDropSystem - payload constants are defined", "[editor][dragdrop]") {
    REQUIRE(kPayloadAssetPath != nullptr);
    REQUIRE(kPayloadEntityDrag != nullptr);
    REQUIRE(std::strlen(kPayloadAssetPath) > 0);
    REQUIRE(std::strlen(kPayloadEntityDrag) > 0);
    REQUIRE(std::string(kPayloadAssetPath) != std::string(kPayloadEntityDrag));
}

TEST_CASE("DragDropSystem - AssetDropPayload size", "[editor][dragdrop]") {
    REQUIRE(sizeof(AssetDropPayload) >= 512 + sizeof(AssetType));
    REQUIRE(offsetof(AssetDropPayload, type) < sizeof(AssetDropPayload));
}

TEST_CASE("DragDropSystem - DragDropManager constants", "[editor][dragdrop]") {
    // Verify that the static methods exist and compile (they are no-ops without ImGui)
    // These test the API surface compiles correctly
    const char* testPath = "test.png";
    AssetType testType = AssetType::Texture;

    // Source methods return false without ImGui context
    bool sourceResult = DragDropManager::SourceAsset(testPath, testType, "test");
    REQUIRE(sourceResult == false);

    u32 entityResult = DragDropManager::AcceptEntityDrop();
    REQUIRE(entityResult == u32_max);

    const auto* assetResult = DragDropManager::AcceptAssetDrop();
    REQUIRE(assetResult == nullptr);
}

// ============================================================================
// ProjectManager tests
// ============================================================================

namespace {

struct ProjectManagerFixture {
    std::filesystem::path tmpDir;

    ProjectManagerFixture()
        : tmpDir(std::filesystem::temp_directory_path() / "caffeine_pm_test")
    {
        std::filesystem::remove_all(tmpDir);
    }

    ~ProjectManagerFixture() {
        std::filesystem::remove_all(tmpDir);
    }
};

} // anonymous namespace

TEST_CASE("ProjectManager - CreateNewProject creates directories and project.caffeine", "[editor]") {
    ProjectManagerFixture fix;
    ProjectManager pm;

    ProjectConfig cfg;
    cfg.Name     = "TestProject";
    cfg.RootPath = fix.tmpDir / "MyGame";

    bool ok = pm.CreateNewProject(cfg);
    REQUIRE(ok);

    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame"));
    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame" / "project.caffeine"));
    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame" / "assets" / "raw"));
    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame" / "assets" / "processed"));
    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame" / "scripts"));
    REQUIRE(std::filesystem::exists(fix.tmpDir / "MyGame" / "build"));
}

TEST_CASE("ProjectManager - CreateNewProject with empty name fails", "[editor]") {
    ProjectManagerFixture fix;
    ProjectManager pm;

    ProjectConfig cfg;
    cfg.Name     = "";
    cfg.RootPath = fix.tmpDir / "Empty";

    bool ok = pm.CreateNewProject(cfg);
    REQUIRE_FALSE(ok);
}

TEST_CASE("ProjectManager - OpenProject loads saved project correctly", "[editor]") {
    ProjectManagerFixture fix;
    ProjectManager pm;

    {
        ProjectConfig cfg;
        cfg.Name     = "ReloadTest";
        cfg.Version  = "1.0.0";
        cfg.RootPath = fix.tmpDir / "ReloadTest";
        cfg.AssetRawPath = "custom/raw";
        cfg.AssetProcessedPath = "custom/processed";
        cfg.ScriptsPath = "my_scripts";
        cfg.LastScene = "scenes/level1.scene";

        bool ok = pm.CreateNewProject(cfg);
        REQUIRE(ok);
    }

    ProjectManager pm2;
    auto projectFile = fix.tmpDir / "ReloadTest" / "project.caffeine";
    bool ok = pm2.OpenProject(projectFile);
    REQUIRE(ok);

    const auto& loaded = pm2.GetCurrentProject();
    REQUIRE(loaded.Name == "ReloadTest");
    REQUIRE(loaded.Version == "1.0.0");
    REQUIRE(loaded.AssetRawPath.generic_string() == "custom/raw");
    REQUIRE(loaded.AssetProcessedPath.generic_string() == "custom/processed");
    REQUIRE(loaded.ScriptsPath.generic_string() == "my_scripts");
    REQUIRE(loaded.LastScene == "scenes/level1.scene");
}

TEST_CASE("ProjectManager - OpenProject with non-existent path fails", "[editor]") {
    ProjectManager pm;
    bool ok = pm.OpenProject(std::filesystem::temp_directory_path() / "nonexistent" / "project.caffeine");
    REQUIRE_FALSE(ok);
    REQUIRE(pm.GetCurrentProject().Name.empty());
}

TEST_CASE("ProjectManager - GetRecentProjects updates after CreateNewProject", "[editor]") {
    ProjectManagerFixture fix;
    ProjectManager pm;
    pm.SetRecentProjectsPath(fix.tmpDir / "recent.txt");

    REQUIRE(pm.GetRecentProjects().empty());

    ProjectConfig cfg;
    cfg.Name     = "RecentTest";
    cfg.RootPath = fix.tmpDir / "RecentTest";

    pm.CreateNewProject(cfg);
    REQUIRE(pm.GetRecentProjects().size() == 1);
    REQUIRE(pm.GetRecentProjects()[0].string().find("project.caffeine") != std::string::npos);
}

TEST_CASE("ProjectManager - recent projects persist across instances", "[editor]") {
    ProjectManagerFixture fix;

    {
        ProjectManager pm;
        pm.SetRecentProjectsPath(fix.tmpDir / "recent.txt");

        ProjectConfig cfg;
        cfg.Name     = "Persist1";
        cfg.RootPath = fix.tmpDir / "Persist1";
        pm.CreateNewProject(cfg);

        cfg.Name     = "Persist2";
        cfg.RootPath = fix.tmpDir / "Persist2";
        pm.CreateNewProject(cfg);

        REQUIRE(pm.GetRecentProjects().size() == 2);
    }

    {
        ProjectManager pm;
        pm.SetRecentProjectsPath(fix.tmpDir / "recent.txt");
        REQUIRE(pm.GetRecentProjects().size() == 2);
    }
}

TEST_CASE("ProjectManager - DefaultRecentPath returns a non-empty path", "[editor]") {
    auto path = ProjectManager::DefaultRecentPath();
    REQUIRE_FALSE(path.empty());
}

TEST_CASE("ProjectManager - GetCurrentProject returns empty config initially", "[editor]") {
    ProjectManager pm;
    const auto& cfg = pm.GetCurrentProject();
    REQUIRE(cfg.Name.empty());
    REQUIRE(cfg.Version == "0.2.0");
}

// ============================================================================
// SceneTabManager Tests (data layer only, no ImGui)
// ============================================================================

TEST_CASE("SceneTabManager - default state has no tabs", "[editor][scenetab]") {
    SceneTabManager mgr;
    REQUIRE(mgr.tabCount() == 0);
    REQUIRE(mgr.activeTabIndex() < 0);
    REQUIRE(mgr.activeWorld() == nullptr);
}

TEST_CASE("SceneTabManager - newScene creates a tab", "[editor][scenetab]") {
    SceneTabManager mgr;
    int idx = mgr.newScene("TestScene");
    REQUIRE(idx >= 0);
    REQUIRE(mgr.tabCount() == 1);
    REQUIRE(mgr.activeTabIndex() == idx);
    REQUIRE(mgr.activeWorld() != nullptr);
    REQUIRE(mgr.activeTab().name == "TestScene");
    REQUIRE(mgr.activeTab().isDirty == false);
    REQUIRE(mgr.activeTab().selectedEntity.isValid() == false);
}

TEST_CASE("SceneTabManager - addTab adds a tab with existing world", "[editor][scenetab]") {
    SceneTabManager mgr;
    auto world = std::make_unique<ECS::World>();
    ECS::World* worldPtr = world.get();
    int idx = mgr.addTab("LoadedScene", std::move(world));
    REQUIRE(idx >= 0);
    REQUIRE(mgr.tabCount() == 1);
    REQUIRE(mgr.activeWorld() == worldPtr);
}

TEST_CASE("SceneTabManager - multiple tabs and switching", "[editor][scenetab]") {
    EditorContext ctx;
    SceneTabManager mgr;

    int tab1 = mgr.newScene("Tab1");
    REQUIRE(tab1 >= 0);

    auto world2 = std::make_unique<ECS::World>();
    int tab2 = mgr.addTab("Tab2", std::move(world2));

    REQUIRE(mgr.tabCount() == 2);
    REQUIRE(mgr.activeTabIndex() == tab1);
    REQUIRE(mgr.activeTab().name == "Tab1");

    // Switch to tab2
    mgr.setActiveTab(tab2, ctx);
    REQUIRE(mgr.activeTabIndex() == tab2);
    REQUIRE(mgr.activeTab().name == "Tab2");

    // Switch back to tab1
    mgr.setActiveTab(tab1, ctx);
    REQUIRE(mgr.activeTabIndex() == tab1);
}

TEST_CASE("SceneTabManager - closeScene removes a tab", "[editor][scenetab]") {
    EditorContext ctx;
    SceneTabManager mgr;

    mgr.newScene("Tab1");
    auto world2 = std::make_unique<ECS::World>();
    int tab2 = mgr.addTab("Tab2", std::move(world2));

    REQUIRE(mgr.tabCount() == 2);

    // Close tab2
    mgr.closeScene(tab2);
    REQUIRE(mgr.tabCount() == 1);
    REQUIRE(mgr.activeTab().name == "Tab1");
}

TEST_CASE("SceneTabManager - closeScene on last tab creates replacement", "[editor][scenetab]") {
    SceneTabManager mgr;
    mgr.newScene("OnlyTab");
    REQUIRE(mgr.tabCount() == 1);

    mgr.closeScene(0);
    REQUIRE(mgr.tabCount() == 1);  // replacement created
    REQUIRE(mgr.activeTab().name == "Untitled");
}

// ============================================================================
// AudioEmitter serialization tests
// ============================================================================

TEST_CASE("SceneSerializer - AudioEmitter component roundtrip",
          "[editor][serializer][audio]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "Speaker");
    auto& emitter = world.add<Audio::AudioEmitter>(e);
    emitter.clipPath = "sfx/explosion.wav";
    emitter.volume = 0.8f;
    emitter.maxDistance = 500.0f;
    emitter.loop = true;
    emitter.playOnSpawn = false;
    emitter.spatial = true;

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_audio.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_audio.caf") == true);

    ECS::ComponentQuery q;
    q.with<Audio::AudioEmitter>();
    bool found = false;
    loaded.forEach<Audio::AudioEmitter>(q, [&](ECS::Entity ent, Audio::AudioEmitter& ae) {
        found = true;
        REQUIRE(ae.clipPath == "sfx/explosion.wav");
        REQUIRE(ae.volume == 0.8f);
        REQUIRE(ae.maxDistance == 500.0f);
        REQUIRE(ae.loop == true);
        REQUIRE(ae.playOnSpawn == false);
        REQUIRE(ae.spatial == true);
    });
    REQUIRE(found == true);

    std::remove("_test_audio.caf");
}

TEST_CASE("SceneSerializer - AudioEmitter default values roundtrip",
          "[editor][serializer][audio]") {
    ECS::World world;
    ECS::Entity e = world.create();
    setEntityName(world, e, "DefaultAudio");
    world.add<Audio::AudioEmitter>(e);  // default values

    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serialize("_test_audio_default.caf") == true);

    ECS::World loaded;
    Editor::SceneSerializer loader(loaded);
    REQUIRE(loader.deserialize("_test_audio_default.caf") == true);

    ECS::ComponentQuery q;
    q.with<Audio::AudioEmitter>();
    bool found = false;
    loaded.forEach<Audio::AudioEmitter>(q, [&](ECS::Entity ent, Audio::AudioEmitter& ae) {
        found = true;
        REQUIRE(ae.clipPath.empty());
        REQUIRE(ae.volume == 1.0f);
        REQUIRE(ae.maxDistance == 500.0f);
        REQUIRE(ae.loop == false);
        REQUIRE(ae.playOnSpawn == true);
        REQUIRE(ae.spatial == true);
    });
    REQUIRE(found == true);

    std::remove("_test_audio_default.caf");
}

TEST_CASE("SceneTabManager - captureContext and applyContext sync state", "[editor][scenetab]") {
    EditorContext ctx;
    SceneTabManager mgr;
    mgr.newScene("Test");

    // Set some state in EditorContext
    ctx.isDirty = true;

    // Capture into the tab
    mgr.captureContext(ctx);
    REQUIRE(mgr.activeTab().isDirty == true);

    // Modify ctx and verify apply restores
    ctx.isDirty = false;
    mgr.applyContext(ctx);
    REQUIRE(ctx.isDirty == true);
}
