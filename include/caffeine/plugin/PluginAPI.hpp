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

/// Docking hint for a plugin panel. Mirrors Caffeine::UIKit::DockArea.
enum class PluginDockArea : int {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Center = 3,
    Floating = 4,
};

struct PluginPanelInfo {
    /// Stable panel id. When null, the host derives one from plugin + title.
    const char* id = nullptr;
    const char* title = nullptr;
    /// Semantic icon name resolved by the host, e.g. "arrow-right".
    const char* icon = nullptr;
    PluginDockArea dock = PluginDockArea::Bottom;
    /// Normalized split size hint (0..1).
    float size = 0.24f;
    bool defaultOpen = true;
};

/// Terrain authoring state owned by the host viewport. Plugins edit it through
/// opaque get/set callbacks instead of including EditorContext.
struct PluginTerrainBrush {
    /// 0 = none, 1 = sculpt, 2 = splat.
    int editMode = 0;
    /// 0 = raise, 1 = lower, 2 = smooth, 3 = flatten, 4 = noise.
    int brushMode = 0;
    unsigned int splatLayer = 0;
    float radius = 6.0f;
    float strength = 0.12f;
    /// 0 = none, 1 = translate, 2 = rotate, 3 = scale.
    int gizmoMode = 1;
};

/// GPU ambient-occlusion bake request. Triangle data is 9 floats per triangle
/// (a.xyz, b.xyz, c.xyz). Position/normal arrays are 4 floats per AO texel.
struct PluginAOBakeDesc {
    const float* triangles = nullptr;
    unsigned int triangleCount = 0;
    const float* positions = nullptr;
    const float* normals = nullptr;
    unsigned int texelCount = 0;
    unsigned int resolution = 0;
    unsigned int rays = 16;
    float maxDistance = 2.5f;
    float bias = 0.02f;
};

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

    /// GPU compute AO bake. Returns false when the device/compute path is not
    /// available; callers should fall back to their CPU baker.
    bool (*bakeAmbientOcclusion)(void* ctx, const PluginAOBakeDesc* desc,
                                 float* outAO) = nullptr;

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

    /// Register a panel with layout metadata so the Workspace can dock it.
    /// Preferred over registerPanel for new plugins.
    bool (*registerPanelEx)(void* ctx, const char* pluginName, const PluginPanelInfo* info,
                            void (*renderFn)(void* userData), void* userData) = nullptr;

    /// Opaque editor services. These keep plugins independent from Doppio's
    /// internal EditorContext type.
    void (*setSelectedEntityId)(void* ctx, CaffeinePluginU32 entityId) = nullptr;
    bool (*getCurrentScenePath)(void* ctx, char* buffer, std::size_t bufferSize) = nullptr;
    void (*markAssetBrowserDirty)(void* ctx) = nullptr;

    /// Viewport terrain authoring state (sculpt/paint brush and gizmo mode).
    bool (*getTerrainBrush)(void* ctx, PluginTerrainBrush* out) = nullptr;
    bool (*setTerrainBrush)(void* ctx, const PluginTerrainBrush* brush) = nullptr;
};

using CreatePluginFn = IPlugin* (*)(const PluginHostApi* host);
using DestroyPluginFn = void (*)(IPlugin* plugin);

}  // namespace Caffeine::Editor

extern "C" {
    CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
        const Caffeine::Editor::PluginHostApi* host);
    CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin);
}
