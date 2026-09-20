#pragma once

#include "core/Types.hpp"

namespace Caffeine::Editor {

// ============================================================================
// Plugin export macros
// ============================================================================

#if defined(_WIN32)
    #ifdef CAFFEINE_PLUGIN_EXPORTS
        #define CAFFEINE_PLUGIN_API __declspec(dllexport)
    #else
        #define CAFFEINE_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define CAFFEINE_PLUGIN_API __attribute__((visibility("default")))
#endif

// ============================================================================
// IPlugin — contract implemented by every plugin
// ============================================================================

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

// ============================================================================
// PluginHostApi — C-style callbacks passed to plugins (ABI-friendly surface)
// ============================================================================

struct PluginHostApi {
    void* editorContext = nullptr;

    void (*logInfo)(void* ctx, const char* message) = nullptr;
    void (*logError)(void* ctx, const char* message) = nullptr;

    bool (*registerPanel)(void* ctx, const char* pluginName, const char* title,
                          void (*renderFn)(void* userData), void* userData) = nullptr;
    bool (*registerMenuAction)(void* ctx, const char* pluginName, const char* menuPath,
                               void (*actionFn)(void* userData), void* userData) = nullptr;
    bool (*registerComponentDrawer)(void* ctx, const char* pluginName, u32 componentTypeId,
                                    void (*drawFn)(void* componentData, void* userData),
                                    void* userData) = nullptr;

    // Registers presets from an entity_presets.json manifest (package/plugin format).
    bool (*registerEntityPresetManifest)(void* ctx, const char* pluginName,
                                         const char* manifestPath) = nullptr;

    // Stable component type id from the host (safe across plugin DLL boundaries).
    u32 (*getComponentTypeId)(void* ctx, const char* componentName) = nullptr;
};

using CreatePluginFn = IPlugin* (*)(const PluginHostApi* host);
using DestroyPluginFn = void (*)(IPlugin* plugin);

}  // namespace Caffeine::Editor

extern "C" {
    CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
        const Caffeine::Editor::PluginHostApi* host);
    CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin);
}
