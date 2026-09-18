#pragma once

#include "editor/PluginAPI.hpp"
#include "core/Types.hpp"
#include "core/io/FileWatcher.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef CF_HAS_IMGUI
struct ImGuiContext;
#endif

namespace Caffeine::Editor {

class InspectorPanel;
class CommandPalette;
class EditorContext;

struct PluginPanel {
    std::string pluginName;
    std::string title;
    std::function<void()> render;
    bool open = true;
};

struct PluginMenuAction {
    std::string pluginName;
    std::string menuPath;
    std::function<void()> action;
};

struct PluginComponentDrawer {
    std::string pluginName;
    u32 componentTypeId = u32_max;
    std::function<void(void*)> draw;
};

enum class PluginStatus : u8 {
    Discovered,
    Loaded,
    Active,
    Failed
};

struct PluginHandle {
    void* libraryHandle = nullptr;
    IPlugin* instance = nullptr;
    std::string path;
    std::string name;
    std::string version;
    std::string description;
    PluginStatus status = PluginStatus::Discovered;
    bool enabled = true;
    std::filesystem::file_time_type lastWriteTime {};
    std::string lastError;
};

class PluginManager {
public:
    static PluginManager& instance();

    void initialize(const std::filesystem::path& pluginsDirectory,
                    InspectorPanel* inspector,
                    CommandPalette* commandPalette,
                    EditorContext* editorContext,
                    const std::filesystem::path& bundledPluginsDirectory = {});
    void shutdown();

    bool loadPlugin(const std::string& path);
    bool unloadPlugin(const std::string& name);
    bool reloadPlugin(const std::string& name);
    void refreshPluginsDirectory();
    void refreshPlugins(float dt);

    void renderPanels();
    void renderMenuExtensions();
    void renderMenuItems(const char* topLevelMenu);
    void renderManagerUI();

    bool isManagerOpen() const { return m_managerOpen; }
    void openManager() { m_managerOpen = true; }
    void closeManager() { m_managerOpen = false; }

    const std::unordered_map<std::string, PluginHandle>& plugins() const { return m_loadedPlugins; }
    const std::vector<PluginPanel>& panels() const { return m_panels; }

    // Editor-side registration (also used by host API callbacks)
    bool registerPanel(const std::string& pluginName, const std::string& title,
                       std::function<void()> renderFunc);
    bool registerMenuAction(const std::string& pluginName, const std::string& menuPath,
                            std::function<void()> action);
    bool registerComponentDrawer(const std::string& pluginName, u32 componentTypeId,
                                 std::function<void(void*)> drawer);

    void unregisterPluginResources(const std::string& pluginName);

private:
    PluginManager() = default;

    bool loadPluginInternal(const std::string& path, bool fromHotReload);
    void scanPluginsDirectory();
    void ensureWatcher();
    std::filesystem::path pluginsDirectory() const;

    static void hostLogInfo(void* ctx, const char* message);
    static void hostLogError(void* ctx, const char* message);
    static bool hostRegisterPanel(void* ctx, const char* pluginName, const char* title,
                                  void (*renderFn)(void* userData), void* userData);
    static bool hostRegisterMenuAction(void* ctx, const char* pluginName, const char* menuPath,
                                       void (*actionFn)(void* userData), void* userData);
    static bool hostRegisterComponentDrawer(void* ctx, const char* pluginName, u32 componentTypeId,
                                              void (*drawFn)(void* componentData, void* userData),
                                              void* userData);

    std::filesystem::path m_pluginsDirectory;
    std::filesystem::path m_bundledPluginsDirectory;
    std::unordered_map<std::string, PluginHandle> m_loadedPlugins;
    std::vector<std::string> m_discoveredPaths;

    std::vector<PluginPanel> m_panels;
    std::vector<PluginMenuAction> m_menuActions;
    std::vector<PluginComponentDrawer> m_componentDrawers;

    InspectorPanel* m_inspector = nullptr;
    CommandPalette* m_commandPalette = nullptr;
    EditorContext* m_editorContext = nullptr;

    PluginHostApi m_hostApi {};
    IO::FileWatcher m_watcher;
    bool m_watcherStarted = false;
    bool m_initialized = false;
    bool m_managerOpen = false;
    float m_refreshTimer = 0.0f;
};

}  // namespace Caffeine::Editor
