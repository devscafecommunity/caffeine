#include "catch.hpp"

#include "caffeine/ui/PanelRegistry.hpp"
#include "caffeine/ui/Theme.hpp"
#include "caffeine/ui/Workspace.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Caffeine::UIKit;

namespace {

std::string writeTemp(const char* name, const std::string& contents) {
    std::ofstream out(name, std::ios::binary);
    out << contents;
    return name;
}

}  // namespace

TEST_CASE("UI theme JSON round-trips tokens", "[ui]") {
    resetTheme(true);
    tokens().accent = ImVec4(0.10f, 0.20f, 0.30f, 1.0f);
    tokens().rounding = 9.0f;

    const std::string path = "ui_theme_roundtrip.json";
    REQUIRE(saveTheme(path));

    resetTheme(false);
    REQUIRE(tokens().dark == false);

    std::string error;
    REQUIRE(loadTheme(path, &error));
    REQUIRE(tokens().dark == true);
    REQUIRE(tokens().accent.x == Approx(0.10f));
    REQUIRE(tokens().accent.y == Approx(0.20f));
    REQUIRE(tokens().accent.z == Approx(0.30f));
    REQUIRE(tokens().rounding == Approx(9.0f));

    std::remove(path.c_str());
}

TEST_CASE("UI panel registry owns panels by stable id and owner", "[ui]") {
    PanelRegistry& registry = PanelRegistry::instance();
    registry.clear();

    PanelDesc first;
    first.id = "core.test";
    first.title = "Test";
    first.category = "Core";
    first.defaultOpen = true;
    REQUIRE(registry.add(first));

    PanelDesc second;
    second.id = "plugin.sample.panel";
    second.title = "Sample";
    second.owner = "sample";
    second.category = "Plugins";
    second.defaultOpen = false;
    REQUIRE(registry.add(second));

    REQUIRE(registry.find("core.test") != nullptr);
    REQUIRE(registry.isVisible("core.test"));
    REQUIRE_FALSE(registry.isVisible("plugin.sample.panel"));

    registry.setVisible("plugin.sample.panel", true);
    REQUIRE(registry.isVisible("plugin.sample.panel"));

    registry.removeOwner("sample");
    REQUIRE(registry.find("plugin.sample.panel") == nullptr);

    registry.remove("core.test");
    REQUIRE(registry.find("core.test") == nullptr);

    registry.clear();
}

TEST_CASE("UI workspace parses areas and plugin defaults", "[ui]") {
    const std::string json = R"JSON({
        "version": 1,
        "left": { "size": 0.18, "enabled": true, "panels": ["core.hierarchy"] },
        "right": { "size": 0.24, "enabled": true },
        "bottom": { "size": 0.30, "enabled": false },
        "plugins": { "dock": "right", "size": 0.21 },
        "assignments": { "core.console": "right" }
    })JSON";

    const std::string path = writeTemp("ui_workspace_test.json", json);
    Workspace workspace;
    std::string error;
    REQUIRE(workspace.load(path, &error));

    REQUIRE(workspace.config().leftSize == Approx(0.18f));
    REQUIRE(workspace.config().rightSize == Approx(0.24f));
    REQUIRE(workspace.config().bottomSize == Approx(0.30f));
    REQUIRE_FALSE(workspace.config().bottomEnabled);
    REQUIRE(workspace.config().pluginDock == DockArea::Right);
    REQUIRE(workspace.config().pluginSize == Approx(0.21f));
    REQUIRE(workspace.config().assignments.at("core.hierarchy") == DockArea::Left);
    REQUIRE(workspace.config().assignments.at("core.console") == DockArea::Right);

    std::remove(path.c_str());
}

TEST_CASE("UI workspace reset restores defaults", "[ui]") {
    Workspace workspace;
    workspace.config().leftSize = 0.5f;
    workspace.reset();
    REQUIRE(workspace.config().leftSize == Approx(0.22f));
    REQUIRE_FALSE(workspace.loaded());
}

TEST_CASE("UI workspace saveTo persists under a user path", "[ui]") {
    const std::string dir = "ui_workspace_saveto";
    const std::string path = dir + "/workspace.json";
    std::filesystem::remove_all(dir);

    Workspace workspace;
    workspace.config().leftSize = 0.31f;
    workspace.config().pluginDock = DockArea::Right;
    REQUIRE(workspace.saveTo(path));
    REQUIRE(workspace.path() == path);
    REQUIRE(workspace.loaded());
    REQUIRE(std::filesystem::exists(path));

    Workspace reloaded;
    std::string error;
    REQUIRE(reloaded.load(path, &error));
    REQUIRE(reloaded.config().leftSize == Approx(0.31f));
    REQUIRE(reloaded.config().pluginDock == DockArea::Right);

    std::filesystem::remove_all(dir);
}

TEST_CASE("UI workspace user config path is stable", "[ui]") {
    const std::string first = Workspace::userConfigPath();
    const std::string second = Workspace::userConfigPath();
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);
    REQUIRE(first.find("workspace.json") != std::string::npos);
}
