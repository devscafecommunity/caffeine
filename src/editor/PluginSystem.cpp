#include "editor/PluginSystem.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/CommandPalette.hpp"
#include "editor/EditorContext.hpp"
#include "editor/EntityPresetPackages.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/ComponentTypeRegistry.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
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

    m_hostApi.editorContext = this;
    m_hostApi.logInfo = &PluginManager::hostLogInfo;
    m_hostApi.logError = &PluginManager::hostLogError;
    m_hostApi.registerPanel = &PluginManager::hostRegisterPanel;
    m_hostApi.registerMenuAction = &PluginManager::hostRegisterMenuAction;
    m_hostApi.registerComponentDrawer = &PluginManager::hostRegisterComponentDrawer;
    m_hostApi.registerEntityPresetManifest = &PluginManager::hostRegisterEntityPresetManifest;
    m_hostApi.getComponentTypeId = &PluginManager::hostGetComponentTypeId;

    std::error_code ec;
    std::filesystem::create_directories(m_pluginsDirectory, ec);

    scanPluginsDirectory();
    ensureWatcher();

    for (const auto& path : m_discoveredPaths) {
        loadPluginInternal(path, false);
    }

    m_initialized = true;
}

void PluginManager::shutdown() {
    if (!m_initialized) return;

    std::vector<std::string> names;
    for (const auto& [name, handle] : m_loadedPlugins) {
        names.push_back(name);
    }
    for (const auto& name : names) {
        unloadPlugin(name);
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
    if (!ctx || !pluginName || !title || !renderFn) return false;
    auto* manager = static_cast<PluginManager*>(ctx);
    return manager->registerPanel(pluginName, title, [renderFn, userData]() { renderFn(userData); });
}

bool PluginManager::hostRegisterMenuAction(void* ctx, const char* pluginName, const char* menuPath,
                                           void (*actionFn)(void* userData), void* userData) {
    if (!ctx || !pluginName || !menuPath || !actionFn) return false;
    auto* manager = static_cast<PluginManager*>(ctx);
    return manager->registerMenuAction(pluginName, menuPath,
                                     [actionFn, userData]() { actionFn(userData); });
}

bool PluginManager::hostRegisterComponentDrawer(void* ctx, const char* pluginName, u32 componentTypeId,
                                                void (*drawFn)(void* componentData, void* userData),
                                                void* userData) {
    if (!ctx || !pluginName || !drawFn) return false;
    auto* manager = static_cast<PluginManager*>(ctx);
    return manager->registerComponentDrawer(pluginName, componentTypeId,
                                          [drawFn, userData](void* data) { drawFn(data, userData); });
}

bool PluginManager::hostRegisterEntityPresetManifest(void* ctx, const char* pluginName,
                                                     const char* manifestPath) {
    if (!ctx || !pluginName || !manifestPath) return false;
    auto* manager = static_cast<PluginManager*>(ctx);
    auto loaded = loadEntityPresetPackage(manifestPath);
    if (!loaded.presets.empty()) {
        for (auto& preset : loaded.presets) {
            preset.source = pluginName;
            EntityPresetRegistry::instance().registerPreset(std::move(preset));
        }
        return true;
    }
    if (!loaded.error.empty()) {
        hostLogError(manager, loaded.error.c_str());
    }
    return false;
}

u32 PluginManager::hostGetComponentTypeId(void* ctx, const char* componentName) {
    (void)ctx;
    return ComponentTypeRegistry::instance().lookup(componentName);
}

bool PluginManager::registerPanel(const std::string& pluginName, const std::string& title,
                                  std::function<void()> renderFunc) {
    if (title.empty() || !renderFunc) return false;

    PluginPanel panel;
    panel.pluginName = pluginName;
    panel.title = title;
    panel.render = std::move(renderFunc);
    panel.open = true;
    m_panels.push_back(std::move(panel));

    if (m_commandPalette) {
        FixedString<64> id("plugin_panel_");
        id.append(pluginName.c_str());
        id.append("_");
        id.append(title.c_str());
        m_commandPalette->registerCommand(
            id, FixedString<128>(title.c_str()), FixedString<128>("Plugins"),
            [this, title]() {
                for (auto& p : m_panels) {
                    if (p.title == title) {
                        p.open = true;
                        break;
                    }
                }
            });
    }
    return true;
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
    m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(),
                                  [&](const PluginPanel& p) { return p.pluginName == pluginName; }),
                   m_panels.end());

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

    m_hostApi.editorContext = this;
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
    entry.name = plugin->GetName() ? plugin->GetName() : libPath.stem().string();
    entry.version = plugin->GetVersion() ? plugin->GetVersion() : "0.0.0";
    entry.description = plugin->GetDescription() ? plugin->GetDescription() : "";
    entry.lastWriteTime = fileTimestamp(libPath);
    entry.status = PluginStatus::Loaded;

    if (m_loadedPlugins.contains(entry.name) && !fromHotReload) {
        unloadPlugin(entry.name);
    }

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
        m_loadedPlugins[entry.name] = std::move(entry);
        return false;
    }

    m_loadedPlugins[entry.name] = std::move(entry);
    return true;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    auto it = m_loadedPlugins.find(name);
    if (it == m_loadedPlugins.end()) return false;

    PluginHandle& handle = it->second;
    if (handle.instance) {
        try {
            handle.instance->OnUnload();
        } catch (...) {
        }
    }

    unregisterPluginResources(name);

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

bool PluginManager::reloadPlugin(const std::string& name) {
    auto it = m_loadedPlugins.find(name);
    if (it == m_loadedPlugins.end()) return false;

    const std::string path = it->second.path;
    unloadPlugin(name);
    return loadPluginInternal(path, true);
}

void PluginManager::refreshPluginsDirectory() {
    scanPluginsDirectory();
    for (const auto& path : m_discoveredPaths) {
        const std::filesystem::path p(path);
        const std::string stem = p.stem().string();
        bool alreadyLoaded = false;
        for (const auto& [name, handle] : m_loadedPlugins) {
            if (handle.path == path) {
                alreadyLoaded = true;
                break;
            }
        }
        if (!alreadyLoaded) {
            loadPluginInternal(path, false);
        }
    }
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

        std::string pluginName;
        for (auto& [name, handle] : m_loadedPlugins) {
            if (handle.path == path.string()) {
                pluginName = name;
                break;
            }
        }

        if (!pluginName.empty()) {
            reloadPlugin(pluginName);
        } else {
            loadPluginInternal(path.string(), true);
        }
    }

    for (auto& [name, handle] : m_loadedPlugins) {
        if (handle.status != PluginStatus::Active || !handle.instance) continue;
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

        const auto pluginIt = m_loadedPlugins.find(panel.pluginName);
        if (pluginIt != m_loadedPlugins.end() && !pluginIt->second.enabled) continue;

        bool open = panel.open;
        if (ImGui::Begin(panel.title.c_str(), &open)) {
            if (panel.render) panel.render();
        }
        ImGui::End();
        panel.open = open;
    }
}

void PluginManager::renderMenuItems(const char* topLevelMenu) {
    if (!topLevelMenu) return;

    for (const auto& action : m_menuActions) {
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
    if (!m_panels.empty() && ImGui::BeginMenu("Window")) {
        for (auto& panel : m_panels) {
            ImGui::MenuItem(panel.title.c_str(), nullptr, &panel.open);
        }
        ImGui::EndMenu();
    }
}

void PluginManager::renderManagerUI() {
    if (!m_managerOpen) return;

    if (ImGui::Begin("Plugin Manager", &m_managerOpen)) {
        ImGui::TextDisabled("Directory: %s", m_pluginsDirectory.string().c_str());

        if (ImGui::Button("Refresh Directory")) {
            refreshPluginsDirectory();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Plugins Folder")) {
            std::error_code ec;
            std::filesystem::create_directories(m_pluginsDirectory, ec);
        }

        ImGui::Separator();

        if (m_loadedPlugins.empty() && m_discoveredPaths.empty()) {
            ImGui::TextDisabled("No plugins found. Place .so/.dll files in the plugins directory.");
        }

        if (ImGui::BeginTable("plugins", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Enabled");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Version");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();

            for (auto& [name, handle] : m_loadedPlugins) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox(("##en" + name).c_str(), &handle.enabled);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(name.c_str());
                if (!handle.description.empty()) {
                    ImGui::TextDisabled("%s", handle.description.c_str());
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(handle.version.c_str());

                ImGui::TableSetColumnIndex(3);
                const char* statusLabel = "Unknown";
                switch (handle.status) {
                    case PluginStatus::Active: statusLabel = "Active"; break;
                    case PluginStatus::Loaded: statusLabel = "Loaded"; break;
                    case PluginStatus::Failed: statusLabel = "Failed"; break;
                    case PluginStatus::Discovered: statusLabel = "Discovered"; break;
                }
                ImGui::TextUnformatted(statusLabel);
                if (!handle.lastError.empty()) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", handle.lastError.c_str());
                }

                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton(("Reload##" + name).c_str())) {
                    reloadPlugin(name);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(("Unload##" + name).c_str())) {
                    unloadPlugin(name);
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Discovered files:");
        for (const auto& path : m_discoveredPaths) {
            const auto filename = std::filesystem::path(path).filename().string();
            bool loaded = false;
            for (const auto& [name, handle] : m_loadedPlugins) {
                if (handle.path == path) {
                    loaded = true;
                    break;
                }
            }
            ImGui::BulletText("%s %s", filename.c_str(), loaded ? "(loaded)" : "");
            if (!loaded && ImGui::SmallButton(("Load##" + filename).c_str())) {
                loadPlugin(path);
            }
        }
    }
    ImGui::End();
}

#else

void PluginManager::renderPanels() {}
void PluginManager::renderMenuExtensions() {}
void PluginManager::renderMenuItems(const char*) {}
void PluginManager::renderManagerUI() {}

#endif

}  // namespace Caffeine::Editor
