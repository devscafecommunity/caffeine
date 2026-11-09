#include "editor/PluginSystem.hpp"

#include "caffeine/ui/PanelRegistry.hpp"
#include "caffeine/ui/UI.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/CommandPalette.hpp"
#include "scene/EditorContext.hpp"
#include "editor/EntityPresetPackages.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/ComponentTypeRegistry.hpp"
#include "editor/EditorPaths.hpp"
#include "editor/PluginServiceRegistry.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Caffeine::Editor {

namespace {

bool isPluginLibrary(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return false;
    const auto ext = path.extension().string();
#if defined(_WIN32)
    return ext == ".dll";
#else
    return ext == ".so";
#endif
}

std::filesystem::file_time_type fileTimestamp(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::last_write_time(path, ec);
}

std::string sanitizePanelId(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '.' || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "plugin.panel" : out;
}

}  // namespace

PluginManager& PluginManager::instance() {
    static PluginManager manager;
    return manager;
}

void PluginManager::initialize(const std::filesystem::path& pluginsDirectory,
                               InspectorPanel* inspector,
                               CommandPalette* commandPalette,
                               EditorContext* editorContext,
                               const std::filesystem::path& bundledPluginsDirectory) {
    if (m_initialized) return;

    m_pluginsDirectory = pluginsDirectory;
    m_bundledPluginsDirectory = bundledPluginsDirectory;
    m_inspector = inspector;
    m_commandPalette = commandPalette;
    m_editorContext = editorContext;

    m_hostApi.editorContext = m_editorContext;
    m_hostApi.logInfo = &PluginManager::hostLogInfo;
    m_hostApi.logError = &PluginManager::hostLogError;
    m_hostApi.registerPanel = &PluginManager::hostRegisterPanel;
    m_hostApi.registerPanelEx = &PluginManager::hostRegisterPanelEx;
    m_hostApi.openPanel = &PluginManager::hostOpenPanel;
    m_hostApi.registerMenuAction = &PluginManager::hostRegisterMenuAction;
    m_hostApi.registerComponentDrawer = &PluginManager::hostRegisterComponentDrawer;
    m_hostApi.registerEntityPresetManifest = &PluginManager::hostRegisterEntityPresetManifest;
    m_hostApi.getComponentTypeId = &PluginManager::hostGetComponentTypeId;
    m_hostApi.getActiveWorld = &PluginManager::hostGetActiveWorld;
    m_hostApi.getSelectedEntityId = &PluginManager::hostGetSelectedEntityId;
    m_hostApi.markSceneDirty = &PluginManager::hostMarkSceneDirty;
    m_hostApi.setSelectedEntityId = &PluginManager::hostSetSelectedEntityId;
    m_hostApi.getCurrentScenePath = &PluginManager::hostGetCurrentScenePath;
    m_hostApi.markAssetBrowserDirty = &PluginManager::hostMarkAssetBrowserDirty;
    m_hostApi.getTerrainBrush = &PluginManager::hostGetTerrainBrush;
    m_hostApi.setTerrainBrush = &PluginManager::hostSetTerrainBrush;
    m_hostApi.invokeService = &PluginManager::hostInvokeService;
    m_hostApi.registerService = &PluginManager::hostRegisterService;
    m_hostApi.getProjectRootPath = &PluginManager::hostGetProjectRootPath;

    std::error_code ec;
    std::filesystem::create_directories(m_pluginsDirectory, ec);

    scanPluginsDirectory();
    ensureWatcher();

    // Plugins are opt-in. Discovery only populates the Plugin Manager; a
    // library is loaded when the user enables it, and that choice is persisted.
    loadPluginPreferences();
    loadEnabledPlugins();

    m_initialized = true;
}

void PluginManager::shutdown() {
    if (!m_initialized) return;

    std::vector<std::string> paths;
    for (const auto& [path, handle] : m_loadedPlugins) {
        paths.push_back(path);
    }
    for (const auto& path : paths) {
        unloadPluginByPath(path);
    }

    m_watcher.stop();
    m_watcherStarted = false;
    m_initialized = false;
}

void PluginManager::hostLogInfo(void* ctx, const char* message) {
    if (!message) return;
    std::cout << "[Plugin] " << message << '\n';
    (void)ctx;
}

void PluginManager::hostLogError(void* ctx, const char* message) {
    if (!message) return;
    std::cerr << "[Plugin] ERROR: " << message << '\n';
    (void)ctx;
}

bool PluginManager::hostRegisterPanel(void* ctx, const char* pluginName, const char* title,
                                      void (*renderFn)(void* userData), void* userData) {
    PluginPanelInfo info;
    info.title = title;
    info.dock = PluginDockArea::Bottom;
    info.size = 0.24f;
    info.defaultOpen = true;
    return hostRegisterPanelEx(ctx, pluginName, &info, renderFn, userData);
}

bool PluginManager::hostRegisterPanelEx(void* ctx, const char* pluginName,
                                        const PluginPanelInfo* info,
                                        void (*renderFn)(void* userData), void* userData) {
    if (!info || !info->title || !renderFn) return false;
    (void)ctx;
    auto& manager = PluginManager::instance();
    const char* id = manager.m_hostApi.pluginId ? manager.m_hostApi.pluginId : pluginName;
    if (!id || id[0] == '\0') return false;
    const std::string panelId = info->id ? info->id : std::string();
    const std::string icon = info->icon ? info->icon : std::string();
    return manager.registerPanel(id, info->title,
                                 [renderFn, userData]() { renderFn(userData); }, panelId,
                                 static_cast<int>(info->dock), info->size, info->defaultOpen,
                                 icon);
}

bool PluginManager::hostOpenPanel(void* ctx, const char* title) {
    (void)ctx;
    if (!title || title[0] == '\0') return false;
    return PluginManager::instance().openPanel(title);
}

bool PluginManager::hostRegisterMenuAction(void* ctx, const char* pluginName, const char* menuPath,
                                           void (*actionFn)(void* userData), void* userData) {
    if (!menuPath || !actionFn) return false;
    (void)ctx;
    auto& manager = PluginManager::instance();
    const char* id = manager.m_hostApi.pluginId ? manager.m_hostApi.pluginId : pluginName;
    if (!id || id[0] == '\0') return false;
    return manager.registerMenuAction(id, menuPath,
                                    [actionFn, userData]() { actionFn(userData); });
}

bool PluginManager::hostRegisterComponentDrawer(void* ctx, const char* pluginName, u32 componentTypeId,
                                                void (*drawFn)(void* componentData, void* userData),
                                                void* userData) {
    if (!drawFn) return false;
    (void)ctx;
    auto& manager = PluginManager::instance();
    const char* id = manager.m_hostApi.pluginId ? manager.m_hostApi.pluginId : pluginName;
    if (!id || id[0] == '\0') return false;
    return manager.registerComponentDrawer(id, componentTypeId,
                                           [drawFn, userData](void* data) { drawFn(data, userData); });
}

bool PluginManager::hostRegisterEntityPresetManifest(void* ctx, const char* pluginName,
                                                     const char* manifestPath) {
    if (!manifestPath) return false;
    auto& manager = PluginManager::instance();
    const char* id = manager.m_hostApi.pluginId ? manager.m_hostApi.pluginId : pluginName;
    if (!id || id[0] == '\0') return false;
    auto loaded = loadEntityPresetPackage(manifestPath);
    if (!loaded.presets.empty()) {
        for (auto& preset : loaded.presets) {
            preset.source = id;
            preset.plugin = id;
            EntityPresetRegistry::instance().registerPreset(std::move(preset));
        }
        return true;
    }
    if (!loaded.error.empty()) {
        hostLogError(nullptr, loaded.error.c_str());
    }
    return false;
}

u32 PluginManager::hostGetComponentTypeId(void* ctx, const char* componentName) {
    (void)ctx;
    return ComponentTypeRegistry::instance().lookup(componentName);
}

void* PluginManager::hostGetActiveWorld(void* editorContext) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    return ctx ? static_cast<void*>(ctx->activeWorld) : nullptr;
}

CaffeinePluginU32 PluginManager::hostGetSelectedEntityId(void* editorContext) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    return (ctx && ctx->selectedEntity.isValid()) ? ctx->selectedEntity.id() : u32_max;
}

void PluginManager::hostMarkSceneDirty(void* editorContext) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (ctx) ctx->markDirty();
}

void PluginManager::hostSetSelectedEntityId(void* editorContext, CaffeinePluginU32 entityId) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (!ctx || !ctx->activeWorld) return;
    if (entityId == 0 || entityId == u32_max) {
        ctx->clearSelection();
        return;
    }
    ctx->selectEntity(ECS::Entity(entityId, ctx->activeWorld));
}

bool PluginManager::hostGetCurrentScenePath(void* editorContext, char* buffer,
                                            std::size_t bufferSize) {
    if (!buffer || bufferSize == 0) return false;
    buffer[0] = '\0';
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (!ctx || ctx->currentScenePath.empty()) return false;
    std::strncpy(buffer, ctx->currentScenePath.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

void PluginManager::hostMarkAssetBrowserDirty(void* editorContext) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (ctx) ctx->assetBrowserDirty = true;
}

bool PluginManager::hostGetTerrainBrush(void* editorContext, PluginTerrainBrush* out) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (!ctx || !out) return false;
    out->editMode = static_cast<int>(ctx->terrainEditMode);
    out->brushMode = static_cast<int>(ctx->terrainBrushMode);
    out->splatLayer = ctx->terrainSplatLayer;
    out->radius = ctx->terrainBrushRadius;
    out->strength = ctx->terrainBrushStrength;
    out->gizmoMode = static_cast<int>(ctx->gizmoMode);
    return true;
}

bool PluginManager::hostSetTerrainBrush(void* editorContext, const PluginTerrainBrush* brush) {
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (!ctx || !brush) return false;
    switch (brush->editMode) {
        case 1: ctx->terrainEditMode = EditorContext::TerrainEditMode::Sculpt; break;
        case 2: ctx->terrainEditMode = EditorContext::TerrainEditMode::Splat; break;
        default: ctx->terrainEditMode = EditorContext::TerrainEditMode::None; break;
    }
    switch (brush->brushMode) {
        case 1: ctx->terrainBrushMode = EditorContext::TerrainBrushMode::Lower; break;
        case 2: ctx->terrainBrushMode = EditorContext::TerrainBrushMode::Smooth; break;
        case 3: ctx->terrainBrushMode = EditorContext::TerrainBrushMode::Flatten; break;
        case 4: ctx->terrainBrushMode = EditorContext::TerrainBrushMode::Noise; break;
        default: ctx->terrainBrushMode = EditorContext::TerrainBrushMode::Raise; break;
    }
    ctx->terrainSplatLayer = brush->splatLayer;
    ctx->terrainBrushRadius = brush->radius;
    ctx->terrainBrushStrength = brush->strength;
    switch (brush->gizmoMode) {
        case 0: ctx->gizmoMode = EditorContext::GizmoMode::None; break;
        case 2: ctx->gizmoMode = EditorContext::GizmoMode::Rotate; break;
        case 3: ctx->gizmoMode = EditorContext::GizmoMode::Scale; break;
        default: ctx->gizmoMode = EditorContext::GizmoMode::Translate; break;
    }
    return true;
}

bool PluginManager::hostInvokeService(void* ctx, const char* serviceName, const void* request,
                                      std::size_t requestSize, void* response,
                                      std::size_t responseSize) {
    (void)ctx;
    auto& manager = PluginManager::instance();
    if (!serviceName || !manager.m_editorContext) return false;
    return PluginServiceRegistry::instance().invoke(serviceName, &manager.m_hostApi, request,
                                                    requestSize, response, responseSize);
}

bool PluginManager::hostRegisterService(void* ctx, const char* pluginName, const char* serviceName,
                                        PluginServiceHandler handler) {
    (void)ctx;
    if (!serviceName || !handler) return false;
    auto& manager = PluginManager::instance();
    const char* id = manager.m_hostApi.pluginId ? manager.m_hostApi.pluginId : pluginName;
    if (!id || id[0] == '\0') return false;
    return PluginServiceRegistry::instance().registerService(id, serviceName, handler);
}

bool PluginManager::hostGetProjectRootPath(void* editorContext, char* buffer,
                                           std::size_t bufferSize) {
    if (!buffer || bufferSize == 0) return false;
    buffer[0] = '\0';
    auto* ctx = static_cast<EditorContext*>(editorContext);
    if (!ctx || ctx->currentScenePath.empty()) return false;

    const std::filesystem::path root =
        std::filesystem::path(ctx->currentScenePath).parent_path().parent_path();
    if (root.empty()) return false;

    const std::string path = root.string();
    std::strncpy(buffer, path.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

bool PluginManager::registerPanel(const std::string& pluginName, const std::string& title,
                                  std::function<void()> renderFunc,
                                  const std::string& panelId, int dock, float size,
                                  bool defaultOpen, const std::string& icon) {
    if (title.empty() || !renderFunc) return false;

    for (auto& existing : m_panels) {
        if (existing.pluginName == pluginName && existing.title == title) {
            existing.render = std::move(renderFunc);
            // Hot reload replaces the callback but must not reopen a panel the
            // user closed or reset its dock metadata.
            existing.dock = dock;
            existing.size = size;
            existing.icon = icon;
            return true;
        }
    }

    PluginPanel panel;
    panel.pluginName = pluginName;
    panel.title = title;
    panel.panelId = panelId.empty()
                        ? "plugin." + sanitizePanelId(pluginName) + "." +
                              sanitizePanelId(title)
                        : panelId;
    panel.commandId = std::string("plugin_panel_") + pluginName + "_" + title;
    panel.dock = dock;
    panel.size = size;
    panel.icon = icon;
    panel.defaultOpen = defaultOpen;
    panel.render = std::move(renderFunc);
    panel.open = defaultOpen;
    m_panels.push_back(panel);

    UIKit::PanelDesc desc;
    desc.id = panel.panelId;
    desc.title = title;
    desc.icon = icon;
    desc.category = "Plugins";
    desc.owner = pluginName;
    desc.dock = static_cast<UIKit::DockArea>(dock);
    desc.size = size;
    desc.defaultOpen = defaultOpen;
    // Rendered by PluginManager::renderPanels, but the registry owns layout,
    // visibility and the View > Panels menu entry.
    desc.open = [title]() { PluginManager::instance().openPanel(title); };
    desc.close = [title]() { PluginManager::instance().closePanel(title); };
    desc.isOpen = [title]() { return PluginManager::instance().isPanelOpen(title); };
    UIKit::PanelRegistry::instance().add(std::move(desc));

    if (m_commandPalette) {
        FixedString<64> id(panel.commandId.c_str());
        m_commandPalette->registerCommand(
            id, FixedString<128>(title.c_str()), FixedString<128>("Plugins"),
            [this, title]() { openPanel(title); }, true, pluginName.c_str());
    }
    return true;
}

bool PluginManager::openPanel(const std::string& title) {
    for (auto& panel : m_panels) {
        if (panel.title == title) {
            panel.open = true;
            return true;
        }
    }
    return false;
}

bool PluginManager::closePanel(const std::string& title) {
    for (auto& panel : m_panels) {
        if (panel.title == title) {
            panel.open = false;
            return true;
        }
    }
    return false;
}

bool PluginManager::isPanelOpen(const std::string& title) const {
    for (const auto& panel : m_panels) {
        if (panel.title == title) return panel.open;
    }
    return false;
}

bool PluginManager::registerMenuAction(const std::string& pluginName, const std::string& menuPath,
                                       std::function<void()> action) {
    if (menuPath.empty() || !action) return false;
    m_menuActions.push_back({pluginName, menuPath, std::move(action)});
    return true;
}

bool PluginManager::registerComponentDrawer(const std::string& pluginName, u32 componentTypeId,
                                            std::function<void(void*)> drawer) {
    if (!m_inspector || !drawer) return false;

    m_componentDrawers.push_back({pluginName, componentTypeId, std::move(drawer)});
    m_inspector->registerDrawer(componentTypeId, m_componentDrawers.back().draw);
    return true;
}

void PluginManager::unregisterPluginResources(const std::string& pluginName) {
    if (m_commandPalette) {
        for (const auto& panel : m_panels) {
            if (panel.pluginName == pluginName && !panel.commandId.empty()) {
                m_commandPalette->unregisterCommand(FixedString<64>(panel.commandId.c_str()));
            }
        }
    }

    m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(),
                                  [&](const PluginPanel& p) { return p.pluginName == pluginName; }),
                   m_panels.end());
    UIKit::PanelRegistry::instance().removeOwner(pluginName);

    m_menuActions.erase(std::remove_if(m_menuActions.begin(), m_menuActions.end(),
                                       [&](const PluginMenuAction& a) {
                                           return a.pluginName == pluginName;
                                       }),
                        m_menuActions.end());

    if (m_inspector) {
        for (const auto& drawer : m_componentDrawers) {
            if (drawer.pluginName == pluginName) {
                m_inspector->unregisterDrawer(drawer.componentTypeId);
            }
        }
    }

    m_componentDrawers.erase(
        std::remove_if(m_componentDrawers.begin(), m_componentDrawers.end(),
                       [&](const PluginComponentDrawer& d) { return d.pluginName == pluginName; }),
        m_componentDrawers.end());
}

std::filesystem::path PluginManager::pluginsDirectory() const {
    return m_pluginsDirectory;
}

std::filesystem::path PluginManager::pluginPreferencesPath() const {
    return EditorPaths::userConfigDirectory() / "plugins.cfg";
}

void PluginManager::loadPluginPreferences() {
    m_enabledPlugins.clear();
    std::ifstream in(pluginPreferencesPath());
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        if (line.empty() || line[0] == '#') continue;
        m_enabledPlugins.insert(std::filesystem::path(line).stem().string());
    }
}

void PluginManager::savePluginPreferences() const {
    const std::filesystem::path path = pluginPreferencesPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "# Doppio plugins enabled by the user (one library stem per line).\n";
    out << "# Edit from Plugins > Plugin Manager; nothing here is preloaded implicitly.\n";
    std::vector<std::string> names(m_enabledPlugins.begin(), m_enabledPlugins.end());
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        out << name << '\n';
    }
}

void PluginManager::loadEnabledPlugins() {
    for (const auto& path : m_discoveredPaths) {
        const std::string stem = std::filesystem::path(path).stem().string();
        if (m_enabledPlugins.contains(stem) && !m_loadedPlugins.contains(path)) {
            loadPluginInternal(path, false);
        }
    }
}

void PluginManager::scanPluginsDirectory() {
    m_discoveredPaths.clear();

    const auto scanDir = [&](const std::filesystem::path& dir) {
        if (dir.empty() || !std::filesystem::exists(dir)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!isPluginLibrary(entry.path())) continue;
            const std::string path = entry.path().string();
            if (std::find(m_discoveredPaths.begin(), m_discoveredPaths.end(), path) ==
                m_discoveredPaths.end()) {
                m_discoveredPaths.push_back(path);
            }
        }
    };

    scanDir(m_pluginsDirectory);
    if (!m_bundledPluginsDirectory.empty() &&
        m_bundledPluginsDirectory != m_pluginsDirectory) {
        scanDir(m_bundledPluginsDirectory);
    }

    std::sort(m_discoveredPaths.begin(), m_discoveredPaths.end());
}

void PluginManager::ensureWatcher() {
    if (m_watcherStarted || m_pluginsDirectory.empty()) return;
    if (!std::filesystem::exists(m_pluginsDirectory)) return;
    if (m_watcher.start(m_pluginsDirectory, false)) {
        m_watcherStarted = true;
    }
}

bool PluginManager::loadPlugin(const std::string& path) {
    return loadPluginInternal(path, false);
}

bool PluginManager::loadPluginInternal(const std::string& path, bool fromHotReload) {
    if (path.empty()) return false;

    const std::filesystem::path libPath(path);
    if (!isPluginLibrary(libPath)) {
        return false;
    }

#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        std::cerr << "PluginManager: failed to load " << path << '\n';
        return false;
    }

    auto createFn = reinterpret_cast<CreatePluginFn>(GetProcAddress(handle, "CreatePlugin"));
    auto destroyFn = reinterpret_cast<DestroyPluginFn>(GetProcAddress(handle, "DestroyPlugin"));
    if (!createFn || !destroyFn) {
        std::cerr << "PluginManager: missing CreatePlugin/DestroyPlugin in " << path << '\n';
        FreeLibrary(handle);
        return false;
    }
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "PluginManager: dlopen failed for " << path << ": " << dlerror() << '\n';
        return false;
    }

    auto createFn = reinterpret_cast<CreatePluginFn>(dlsym(handle, "CreatePlugin"));
    auto destroyFn = reinterpret_cast<DestroyPluginFn>(dlsym(handle, "DestroyPlugin"));
    if (!createFn || !destroyFn) {
        std::cerr << "PluginManager: missing exports in " << path << ": " << dlerror() << '\n';
        dlclose(handle);
        return false;
    }
#endif

    m_hostApi.editorContext = m_editorContext;
    IPlugin* plugin = createFn(&m_hostApi);
    if (!plugin) {
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return false;
    }

    PluginHandle entry;
    entry.libraryHandle = handle;
    entry.instance = plugin;
    entry.path = path;
    entry.name = libPath.stem().string();
    const char* reportedName = plugin->GetName();
    entry.version = plugin->GetVersion() ? plugin->GetVersion() : "0.0.0";
    entry.description = plugin->GetDescription() ? plugin->GetDescription() : "";
    if (reportedName && reportedName[0] != '\0') {
        std::cout << "[Plugin] Loaded: " << reportedName << " [" << entry.name << "] (" << path
                  << ")\n";
    } else {
        std::cout << "[Plugin] Loaded: " << entry.name << " (" << path << ")\n";
    }
    entry.lastWriteTime = fileTimestamp(libPath);
    entry.status = PluginStatus::Loaded;

    if (m_loadedPlugins.contains(entry.path) && !fromHotReload) {
        unloadPluginByPath(entry.path);
    }

    m_hostApi.pluginId = entry.name.c_str();

    try {
        plugin->OnLoad();
        entry.status = PluginStatus::Active;
    } catch (const std::exception& e) {
        entry.status = PluginStatus::Failed;
        entry.lastError = e.what();
        destroyFn(plugin);
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        m_hostApi.pluginId = nullptr;
        m_loadedPlugins[entry.path] = std::move(entry);
        return false;
    }

    m_hostApi.pluginId = nullptr;
    m_loadedPlugins[entry.path] = std::move(entry);
    return true;
}

bool PluginManager::isContributionEnabled(const std::string& pluginName) const {
    if (pluginName.empty()) return true;
    if (const PluginHandle* handle = findLoadedPluginByStem(pluginName)) return handle->enabled;
    const std::filesystem::path asPath(pluginName);
    if (const PluginHandle* handle = findLoadedPluginByStem(asPath.stem().string())) {
        return handle->enabled;
    }
    return false;
}

bool PluginManager::isComponentDrawerEnabled(u32 componentTypeId) const {
    bool found = false;
    for (const auto& drawer : m_componentDrawers) {
        if (drawer.componentTypeId != componentTypeId) continue;
        found = true;
        if (isContributionEnabled(drawer.pluginName)) return true;
    }
    return !found;
}

const PluginHandle* PluginManager::findLoadedPluginByStem(
    const std::string& stem) const {
    for (const auto& [path, handle] : m_loadedPlugins) {
        if (handle.name == stem || path == stem) {
            return &handle;
        }
    }
    return nullptr;
}

bool PluginManager::unloadPluginByPath(const std::string& path) {
    auto it = m_loadedPlugins.find(path);
    if (it == m_loadedPlugins.end()) return false;

    PluginHandle& handle = it->second;
    if (handle.instance) {
        try {
            handle.instance->OnUnload();
        } catch (...) {
        }
    }

    unregisterPluginResources(handle.name);
    PluginServiceRegistry::instance().unregisterPlugin(handle.name);

#if defined(_WIN32)
    auto destroyFn = reinterpret_cast<DestroyPluginFn>(
        GetProcAddress(static_cast<HMODULE>(handle.libraryHandle), "DestroyPlugin"));
    if (destroyFn && handle.instance) {
        destroyFn(handle.instance);
    }
    if (handle.libraryHandle) {
        FreeLibrary(static_cast<HMODULE>(handle.libraryHandle));
    }
#else
    auto destroyFn = reinterpret_cast<DestroyPluginFn>(dlsym(handle.libraryHandle, "DestroyPlugin"));
    if (destroyFn && handle.instance) {
        destroyFn(handle.instance);
    }
    if (handle.libraryHandle) {
        dlclose(handle.libraryHandle);
    }
#endif

    m_loadedPlugins.erase(it);
    return true;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    for (const auto& [path, handle] : m_loadedPlugins) {
        if (handle.name == name || path == name) {
            return unloadPluginByPath(path);
        }
    }
    return false;
}

bool PluginManager::reloadPlugin(const std::string& name) {
    for (const auto& [path, handle] : m_loadedPlugins) {
        if (handle.name == name || path == name) {
            unloadPluginByPath(path);
            return loadPluginInternal(handle.path, true);
        }
    }
    return false;
}

bool PluginManager::isPluginEnabled(const std::string& name) const {
    if (name.empty()) return false;
    const std::string stem = std::filesystem::path(name).stem().string();
    return m_enabledPlugins.contains(stem);
}

bool PluginManager::setPluginEnabled(const std::string& name, bool enabled) {
    if (name.empty()) return false;
    const std::string stem = std::filesystem::path(name).stem().string();

    if (!enabled) {
        m_enabledPlugins.erase(stem);
        for (const auto& [path, handle] : m_loadedPlugins) {
            if (handle.name == name || handle.name == stem || path == name) {
                unloadPluginByPath(path);
                break;
            }
        }
        savePluginPreferences();
        return true;
    }

    std::string path;
    for (const auto& discovered : m_discoveredPaths) {
        if (std::filesystem::path(discovered).stem().string() == stem) {
            path = discovered;
            break;
        }
    }
    if (path.empty()) return false;

    m_enabledPlugins.insert(stem);
    bool loaded = false;
    for (const auto& [loadedPath, handle] : m_loadedPlugins) {
        if (handle.name == stem || loadedPath == path) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        loaded = loadPluginInternal(path, false);
        if (!loaded) m_enabledPlugins.erase(stem);
    }
    savePluginPreferences();
    return loaded;
}

void PluginManager::refreshPluginsDirectory() {
    scanPluginsDirectory();
    // Re-sync only what the user enabled. New libraries stay disabled until
    // they are explicitly loaded from the Plugin Manager.
    loadEnabledPlugins();
}

void PluginManager::refreshPlugins(float dt) {
    m_refreshTimer += dt;
    if (m_refreshTimer < 0.5f) return;
    m_refreshTimer = 0.0f;

    ensureWatcher();
    if (!m_watcherStarted) return;

    const auto changed = m_watcher.poll();
    for (const auto& path : changed) {
        if (!isPluginLibrary(path)) continue;

        std::string pluginPath;
        for (auto& [loadedPath, handle] : m_loadedPlugins) {
            if (handle.path == path.string()) {
                pluginPath = loadedPath;
                break;
            }
        }

        if (!pluginPath.empty()) {
            unloadPluginByPath(pluginPath);
            loadPluginInternal(pluginPath, true);
        } else if (m_enabledPlugins.contains(
                       std::filesystem::path(path).stem().string())) {
            // A library the user enabled appeared or was replaced: load it.
            // Everything else is deliberately left alone.
            loadPluginInternal(path.string(), true);
        }
    }

    for (auto& [name, handle] : m_loadedPlugins) {
        if (!handle.enabled || handle.status != PluginStatus::Active || !handle.instance) continue;
        try {
            handle.instance->OnUpdate(0.5f);
        } catch (...) {
            handle.status = PluginStatus::Failed;
            handle.lastError = "OnUpdate threw an exception";
        }
    }
}

#ifdef CF_HAS_IMGUI

void PluginManager::renderPanels() {
    for (auto& panel : m_panels) {
        if (!panel.open) continue;

        if (const PluginHandle* handle = findLoadedPluginByStem(panel.pluginName)) {
            if (!handle->enabled) continue;
        }

        bool open = panel.open;
        if (UIKit::BeginPanel(panel.title.c_str(), &open)) {
            if (panel.render) panel.render();
        }
        UIKit::EndPanel();
        panel.open = open;
    }
}

bool PluginManager::hasMenuItems(const char* topLevelMenu) const {
    if (!topLevelMenu) return false;
    for (const auto& action : m_menuActions) {
        if (!isContributionEnabled(action.pluginName)) continue;
        const auto slash = action.menuPath.find('/');
        if (slash == std::string::npos) continue;
        if (action.menuPath.compare(0, slash, topLevelMenu) == 0) return true;
    }
    return false;
}

void PluginManager::renderMenuItems(const char* topLevelMenu) {
    if (!topLevelMenu) return;

    for (const auto& action : m_menuActions) {
        if (!isContributionEnabled(action.pluginName)) continue;
        const auto slash = action.menuPath.find('/');
        if (slash == std::string::npos) continue;

        const std::string menu = action.menuPath.substr(0, slash);
        if (menu != topLevelMenu) continue;

        const std::string label = action.menuPath.substr(slash + 1);
        if (ImGui::MenuItem(label.c_str())) {
            if (action.action) action.action();
        }
    }
}

void PluginManager::renderMenuExtensions() {
    bool any = false;
    for (const auto& panel : m_panels) {
        if (isContributionEnabled(panel.pluginName)) {
            any = true;
            break;
        }
    }
    if (!any) return;
    if (ImGui::BeginMenu("Window")) {
        for (auto& panel : m_panels) {
            if (!isContributionEnabled(panel.pluginName)) continue;
            ImGui::MenuItem(panel.title.c_str(), nullptr, &panel.open);
        }
        ImGui::EndMenu();
    }
}

void PluginManager::renderManagerUI() {
#ifdef CF_HAS_IMGUI
    if (!m_managerOpen) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Plugin Manager", "Enable optional Doppio extensions", "beer",
               &m_managerOpen);
    UIContext ui;

    auto& info = root.emplace<Card>("plugin_info");
    info.emplace<Label>("Project plugins: " + m_pluginsDirectory.string(),
                        TextTone::Muted)
        .setWrap(true);
    if (!m_bundledPluginsDirectory.empty()) {
        info.emplace<Label>("Bundled plugins: " + m_bundledPluginsDirectory.string(),
                            TextTone::Muted)
            .setWrap(true);
    }
    info.emplace<Label>("Enable list: " + pluginPreferencesPath().string(),
                        TextTone::Muted)
        .setWrap(true);

    auto& actions = root.emplace<Row>("plugin_actions");
    actions.emplace<Button>("Refresh Directory").setOnClick([this] {
        refreshPluginsDirectory();
    });
    actions.emplace<Button>("Open Plugins Folder").setOnClick([this] {
        std::error_code ec;
        std::filesystem::create_directories(m_pluginsDirectory, ec);
    });

    root.emplace<Elements::Paragraph>(
        "Plugins are disabled until you enable them. Enabling loads the library now and "
        "remembers the choice for future sessions.");

    struct PluginRow {
        std::string stem;
        std::string path;
    };
    std::vector<PluginRow> rows;
    const auto addRow = [&rows](const std::string& path) {
        const std::string stem = std::filesystem::path(path).stem().string();
        for (const auto& row : rows) {
            if (row.stem == stem) return;
        }
        rows.push_back({stem, path});
    };
    for (const auto& path : m_discoveredPaths) addRow(path);
    for (const auto& [path, handle] : m_loadedPlugins) addRow(handle.path);
    std::sort(rows.begin(), rows.end(),
              [](const PluginRow& a, const PluginRow& b) { return a.stem < b.stem; });

    if (rows.empty()) {
        root.emplace<Label>("No plugins found. Place .so/.dll files in the plugins directory.",
                            TextTone::Muted);
    }

    for (const PluginRow& row : rows) {
        const bool enabled = m_enabledPlugins.contains(row.stem);
        const PluginHandle* handle = findLoadedPluginByStem(row.stem);
        if (!handle) {
            for (const auto& [path, candidate] : m_loadedPlugins) {
                if (candidate.path == row.path) {
                    handle = &candidate;
                    break;
                }
            }
        }

        auto& card = root.emplace<Card>("plugin_" + row.stem);
        auto& header = card.emplace<Row>("plugin_" + row.stem + "_header");
        header.emplace<Label>(handle ? handle->name : row.stem, TextTone::Accent)
            .setHeading(true);
        header.emplace<Label>(handle ? handle->version : "-", TextTone::Muted);

        const char* statusLabel = "Disabled";
        TextTone statusTone = TextTone::Disabled;
        if (handle) {
            switch (handle->status) {
                case PluginStatus::Active:
                    statusLabel = "Active";
                    statusTone = TextTone::Success;
                    break;
                case PluginStatus::Loaded:
                    statusLabel = "Loaded";
                    statusTone = TextTone::Accent;
                    break;
                case PluginStatus::Failed:
                    statusLabel = "Failed";
                    statusTone = TextTone::Danger;
                    break;
                case PluginStatus::Discovered:
                    statusLabel = "Discovered";
                    statusTone = TextTone::Muted;
                    break;
            }
        }
        header.emplace<Label>(statusLabel, statusTone);

        auto& buttons = card.emplace<Row>("plugin_" + row.stem + "_actions");
        if (enabled) {
            auto& disable = buttons.emplace<Button>("Disable", ButtonVariant::Danger);
            disable.setOnClick([this, stem = row.stem] { setPluginEnabled(stem, false); });
            if (handle) {
                auto& reload = buttons.emplace<Button>("Reload");
                reload.setOnClick([this, stem = row.stem] { reloadPlugin(stem); });
            }
        } else {
            auto& enable = buttons.emplace<Button>("Enable", ButtonVariant::Primary);
            enable.setOnClick([this, stem = row.stem] { setPluginEnabled(stem, true); });
            if (handle) {
                auto& unload = buttons.emplace<Button>("Unload");
                unload.setOnClick([this, stem = row.stem] { setPluginEnabled(stem, false); });
            }
        }

        if (handle && !handle->description.empty()) {
            card.emplace<Label>(handle->description, TextTone::Muted).setWrap(true);
        }
        if (handle && !handle->lastError.empty()) {
            card.emplace<Label>(handle->lastError, TextTone::Danger).setWrap(true);
        }
    }

    ui.draw(root);
#endif
}

#else

void PluginManager::renderPanels() {}
void PluginManager::renderMenuExtensions() {}
void PluginManager::renderMenuItems(const char*) {}
void PluginManager::renderManagerUI() {}

#endif

}  // namespace Caffeine::Editor
