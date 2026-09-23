#pragma once

/**
 * Caffeine Plugin SDK — public API for editor plugins (.so / .dll).
 *
 * Plugins are loaded dynamically from the plugins/ directory at runtime.
 * They link only against this header + ImGui; the host (Doppio) provides callbacks.
 *
 * See docs/editor/plugin-sdk.md
 */

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
    #ifdef CAFFEINE_PLUGIN_EXPORTS
        #define CAFFEINE_PLUGIN_API __declspec(dllexport)
    #else
        #define CAFFEINE_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define CAFFEINE_PLUGIN_API __attribute__((visibility("default")))
#endif

using CaffeinePluginU32 = std::uint32_t;

namespace Caffeine::Editor {

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual void OnLoad() = 0;
    virtual void OnUnload() = 0;
    virtual void OnUpdate(float dt) = 0;
    virtual const char* GetName() const = 0;
    virtual const char* GetVersion() const = 0;
    virtual const char* GetDescription() const { return ""; }
};

using PluginServiceHandler = bool (*)(void* editorContext, const void* request, std::size_t requestSize,
                                      void* response, std::size_t responseSize);

struct PluginHostApi {
    void* editorContext = nullptr;
    /// Stable plugin id (library stem). Set by the host before OnLoad().
    const char* pluginId = nullptr;

    void (*logInfo)(void* ctx, const char* message) = nullptr;
    void (*logError)(void* ctx, const char* message) = nullptr;

    void* (*getActiveWorld)(void* editorContext) = nullptr;
    CaffeinePluginU32 (*getSelectedEntityId)(void* editorContext) = nullptr;
    void (*markSceneDirty)(void* editorContext) = nullptr;

    /// Fills buffer with open project's root path (parent of scenes/). Returns false if no project.
    bool (*getProjectRootPath)(void* editorContext, char* buffer, std::size_t bufferSize) = nullptr;

    bool (*registerPanel)(void* ctx, const char* pluginName, const char* title,
                          void (*renderFn)(void* userData), void* userData) = nullptr;
    /// Opens a panel previously registered with registerPanel (matches title).
    bool (*openPanel)(void* ctx, const char* title) = nullptr;
    bool (*registerMenuAction)(void* ctx, const char* pluginName, const char* menuPath,
                               void (*actionFn)(void* userData), void* userData) = nullptr;
    bool (*registerComponentDrawer)(void* ctx, const char* pluginName, CaffeinePluginU32 componentTypeId,
                                    void (*drawFn)(void* componentData, void* userData),
                                    void* userData) = nullptr;
    bool (*registerEntityPresetManifest)(void* ctx, const char* pluginName,
                                         const char* manifestPath) = nullptr;

    CaffeinePluginU32 (*getComponentTypeId)(void* ctx, const char* componentName) = nullptr;

    /// Generic service dispatch — plugins call host capabilities by name (see plugin-sdk.md).
    bool (*invokeService)(void* ctx, const char* serviceName, const void* request,
                          std::size_t requestSize, void* response, std::size_t responseSize) = nullptr;

    /// Plugins may register additional services (for aggregator / store plugins).
    bool (*registerService)(void* ctx, const char* pluginName, const char* serviceName,
                            PluginServiceHandler handler) = nullptr;
};

using CreatePluginFn = IPlugin* (*)(const PluginHostApi* host);
using DestroyPluginFn = void (*)(IPlugin* plugin);

}  // namespace Caffeine::Editor

extern "C" {
    CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
        const Caffeine::Editor::PluginHostApi* host);
    CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin);
}
