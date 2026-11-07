#pragma once

#include "editor/PluginAPI.hpp"
#include "core/Types.hpp"
#include "core/io/FileWatcher.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef CF_HAS_IMGUI
struct ImGuiContext;
#endif

namespace Caffeine::RHI {
class RenderDevice;
}

namespace Caffeine::Editor {

class InspectorPanel;
class CommandPalette;
class EditorContext;

struct PluginPanel {
    std::string pluginName;
    std::string title;
    std::string panelId;
    std::string commandId;
    std::string icon;
    int dock = 2;  // Caffeine::UIKit::DockArea::Bottom
    float size = 0.24f;
    bool defaultOpen = true;
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
                    const std::filesystem::path& bundledPluginsDirectory = {},
                    RHI::RenderDevice* renderDevice = nullptr);
    void shutdown();

    bool loadPlugin(const std::string& path);
    bool unloadPlugin(const std::string& name);
    bool reloadPlugin(const std::string& name);
    /// Load/unload a plugin and persist the choice for the next session.
    /// Nothing is preloaded at startup: a plugin only runs after it is enabled.
    bool setPluginEnabled(const std::string& name, bool enabled);
    bool isPluginEnabled(const std::string& name) const;
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
    /// Empty name is always on. Missing or disabled plugins return false.
    bool isContributionEnabled(const std::string& pluginName) const;
    /// True when no plugin owns the drawer, or the owning plugin is enabled.
    bool isComponentDrawerEnabled(u32 componentTypeId) const;
    bool hasMenuItems(const char* topLevelMenu) const;

    // Editor-side registration (also used by host API callbacks)
    bool registerPanel(const std::string& pluginName, const std::string& title,
                       std::function<void()> renderFunc,
                       const std::string& panelId = std::string(), int dock = 2,
                       float size = 0.24f, bool defaultOpen = true,
                       const std::string& icon = std::string());
    bool openPanel(const std::string& title);
    bool closePanel(const std::string& title);
    bool isPanelOpen(const std::string& title) const;
    bool registerMenuAction(const std::string& pluginName, const std::string& menuPath,
                            std::function<void()> action);
    bool registerComponentDrawer(const std::string& pluginName, u32 componentTypeId,
                                 std::function<void(void*)> drawer);

    void unregisterPluginResources(const std::string& pluginName);

private:
    PluginManager() = default;

    bool loadPluginInternal(const std::string& path, bool fromHotReload);
    bool unloadPluginByPath(const std::string& path);
    const struct PluginHandle* findLoadedPluginByStem(const std::string& stem) const;
    void scanPluginsDirectory();
    void ensureWatcher();
    std::filesystem::path pluginsDirectory() const;
    std::filesystem::path pluginPreferencesPath() const;
    void loadPluginPreferences();
    void savePluginPreferences() const;
    void loadEnabledPlugins();

    static void hostLogInfo(void* ctx, const char* message);
    static void hostLogError(void* ctx, const char* message);
    static bool hostRegisterPanel(void* ctx, const char* pluginName, const char* title,
                                  void (*renderFn)(void* userData), void* userData);
    static bool hostRegisterPanelEx(void* ctx, const char* pluginName,
                                    const PluginPanelInfo* info,
                                    void (*renderFn)(void* userData), void* userData);
    static bool hostOpenPanel(void* ctx, const char* title);
    static bool hostRegisterMenuAction(void* ctx, const char* pluginName, const char* menuPath,
                                       void (*actionFn)(void* userData), void* userData);
    static bool hostRegisterComponentDrawer(void* ctx, const char* pluginName, u32 componentTypeId,
                                              void (*drawFn)(void* componentData, void* userData),
                                              void* userData);
    static bool hostRegisterEntityPresetManifest(void* ctx, const char* pluginName,
                                                 const char* manifestPath);
    static u32 hostGetComponentTypeId(void* ctx, const char* componentName);
    static void* hostGetActiveWorld(void* editorContext);
    static CaffeinePluginU32 hostGetSelectedEntityId(void* editorContext);
    static void hostMarkSceneDirty(void* editorContext);
    static void hostSetSelectedEntityId(void* editorContext, CaffeinePluginU32 entityId);
    static bool hostGetCurrentScenePath(void* editorContext, char* buffer, std::size_t bufferSize);
    static void hostMarkAssetBrowserDirty(void* editorContext);
    static bool hostGetTerrainBrush(void* editorContext, PluginTerrainBrush* out);
    static bool hostSetTerrainBrush(void* editorContext, const PluginTerrainBrush* brush);
    static bool hostInvokeService(void* ctx, const char* serviceName, const void* request,
                                  std::size_t requestSize, void* response, std::size_t responseSize);
    static bool hostRegisterService(void* ctx, const char* pluginName, const char* serviceName,
                                    PluginServiceHandler handler);
    static bool hostGetProjectRootPath(void* editorContext, char* buffer, std::size_t bufferSize);
    static bool hostBakeAmbientOcclusion(void* ctx, const PluginAOBakeDesc* desc,
                                         float* outAO);

    std::filesystem::path m_pluginsDirectory;
    std::filesystem::path m_bundledPluginsDirectory;
    std::unordered_map<std::string, PluginHandle> m_loadedPlugins;
    std::vector<std::string> m_discoveredPaths;
    std::unordered_set<std::string> m_enabledPlugins;

    std::vector<PluginPanel> m_panels;
    std::vector<PluginMenuAction> m_menuActions;
    std::vector<PluginComponentDrawer> m_componentDrawers;

    InspectorPanel* m_inspector = nullptr;
    CommandPalette* m_commandPalette = nullptr;
    EditorContext* m_editorContext = nullptr;
    RHI::RenderDevice* m_renderDevice = nullptr;

    PluginHostApi m_hostApi {};
    IO::FileWatcher m_watcher;
    bool m_watcherStarted = false;
    bool m_initialized = false;
    bool m_managerOpen = false;
    float m_refreshTimer = 0.0f;
};

}  // namespace Caffeine::Editor
