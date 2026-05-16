#include "../Catch2/catch.hpp"
#include "../../src/editor/AssetBrowser.hpp"
#include "../../src/editor/EditorContext.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <filesystem>
#include <fstream>

extern bool g_gpuAvailable;

using namespace Caffeine;
using namespace Caffeine::Editor;

void PumpPanelFrame(AssetBrowser& panel, EditorContext& ctx) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
    
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    
    panel.render(ctx);
    
    ImGui::Render();
}

class AssetBrowserTestFixture {
public:
    AssetBrowserTestFixture() {
        testDir = std::filesystem::temp_directory_path() / "caffeine_test_assets";
        std::filesystem::create_directories(testDir);
    }
    
    ~AssetBrowserTestFixture() {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }
    
    void createTestFile(const std::string& name, const std::string& content = "") {
        std::filesystem::path filePath = testDir / name;
        std::ofstream file(filePath);
        file << content;
        file.close();
    }
    
    void createTestDirectory(const std::string& name) {
        std::filesystem::path dirPath = testDir / name;
        std::filesystem::create_directories(dirPath);
    }
    
    std::filesystem::path testDir;
};

TEST_CASE("AssetBrowser - init and render without crashing", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("test.png");
    fixture.createTestFile("test.wav");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    PumpPanelFrame(browser, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("AssetBrowser - displays files in directory", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("texture.png", "fake png data");
    fixture.createTestFile("sound.wav", "fake wav data");
    fixture.createTestFile("model.obj", "fake obj data");
    fixture.createTestDirectory("subdir");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    PumpPanelFrame(browser, ctx);
    
    const auto& entries = browser.entries();
    REQUIRE(entries.size() == 4);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - grid view renders entries", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("image1.png");
    fixture.createTestFile("image2.jpg");
    fixture.createTestFile("audio.ogg");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    browser.setViewMode(AssetBrowser::ViewMode::Grid);
    
    REQUIRE(browser.viewMode() == AssetBrowser::ViewMode::Grid);
    
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(browser, ctx);
    }
    
    const auto& entries = browser.entries();
    REQUIRE(entries.size() == 3);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}

TEST_CASE("AssetBrowser - list view renders entries", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("document.txt");
    fixture.createTestFile("script.lua");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    browser.setViewMode(AssetBrowser::ViewMode::List);
    
    REQUIRE(browser.viewMode() == AssetBrowser::ViewMode::List);
    
    for (int i = 0; i < 3; ++i) {
        PumpPanelFrame(browser, ctx);
    }
    
    const auto& entries = browser.entries();
    REQUIRE(entries.size() == 2);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - search filter works", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("player_sprite.png");
    fixture.createTestFile("enemy_sprite.png");
    fixture.createTestFile("background.jpg");
    fixture.createTestFile("music.wav");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    browser.setSearchFilter("sprite");
    
    PumpPanelFrame(browser, ctx);
    
    const auto& filteredEntries = browser.entries();
    REQUIRE(filteredEntries.size() == 2);
    
    for (const auto& entry : filteredEntries) {
        REQUIRE(entry.name.find("sprite") != std::string::npos);
    }
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - search filter case insensitive", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("UPPERCASE.PNG");
    fixture.createTestFile("lowercase.png");
    fixture.createTestFile("MixedCase.PNG");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    browser.setSearchFilter("png");
    
    PumpPanelFrame(browser, ctx);
    
    const auto& filteredEntries = browser.entries();
    REQUIRE(filteredEntries.size() == 3);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - thumbnail size can be adjusted", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestFile("test.png");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    REQUIRE(browser.thumbnailSize() == 64);
    
    browser.setThumbnailSize(128);
    
    REQUIRE(browser.thumbnailSize() == 128);
    
    PumpPanelFrame(browser, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - navigation to subdirectory", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestDirectory("textures");
    fixture.createTestFile("textures/sprite.png");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    std::filesystem::path subdir = fixture.testDir / "textures";
    browser.navigateTo(subdir);
    
    REQUIRE(browser.currentPath() == subdir);
    REQUIRE(browser.canGoBack());
    
    PumpPanelFrame(browser, ctx);
    
    const auto& entries = browser.entries();
    REQUIRE(entries.size() == 1);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - navigate back works", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    fixture.createTestDirectory("subdir");
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    std::filesystem::path originalPath = browser.currentPath();
    std::filesystem::path subdir = fixture.testDir / "subdir";
    
    browser.navigateTo(subdir);
    REQUIRE(browser.currentPath() == subdir);
    REQUIRE(browser.canGoBack());
    
    browser.navigateBack();
    REQUIRE(browser.currentPath() == originalPath);
    REQUIRE(!browser.canGoBack());
    
    PumpPanelFrame(browser, ctx);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
}

TEST_CASE("AssetBrowser - empty directory renders", "[editor][ui][assetbrowser]") {
    if (!g_gpuAvailable) {
        WARN("Skipping test - GPU not available");
        return;
    }
    
    AssetBrowserTestFixture fixture;
    
    EditorContext ctx;
    AssetBrowser browser;
    browser.init(fixture.testDir.string().c_str());
    
    PumpPanelFrame(browser, ctx);
    
    const auto& entries = browser.entries();
    REQUIRE(entries.size() == 0);
    
    ImGuiWindow* win = ImGui::FindWindowByName("Asset Browser");
    REQUIRE(win != nullptr);
    REQUIRE(win->Active);
}
