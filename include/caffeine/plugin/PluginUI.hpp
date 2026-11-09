#pragma once

/**
 * Caffeine Plugin UI — bridge between the plugin ABI, the public UI kit and the
 * opaque editor services exposed by the host.
 *
 * Plugin panels should include this header instead of reaching into Doppio's
 * internal EditorContext. The helpers below cover the common editor queries:
 * active world, selected entity, dirty state, scene path and project root.
 */

#include "caffeine/plugin/PluginAPI.hpp"
#include "caffeine/ui/Widgets.hpp"

#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Editor {

inline bool RegisterPluginPanel(const PluginHostApi* host, const char* pluginName,
                                const PluginPanelInfo& info,
                                void (*renderFn)(void* userData), void* userData) {
    if (!host || !info.title || !renderFn) return false;
    if (host->registerPanelEx) {
        return host->registerPanelEx(host->editorContext, pluginName, &info, renderFn, userData);
    }
    if (host->registerPanel) {
        return host->registerPanel(host->editorContext, pluginName, info.title, renderFn,
                                   userData);
    }
    return false;
}

inline ECS::World* PluginActiveWorld(const PluginHostApi* host) {
    if (!host || !host->getActiveWorld) return nullptr;
    return static_cast<ECS::World*>(host->getActiveWorld(host->editorContext));
}

inline ECS::Entity PluginSelectedEntity(const PluginHostApi* host, ECS::World* world) {
    if (!host || !host->getSelectedEntityId || !world) return ECS::Entity::INVALID;
    const CaffeinePluginU32 id = host->getSelectedEntityId(host->editorContext);
    if (id == 0 || id == 0xFFFFFFFFu) return ECS::Entity::INVALID;
    return ECS::Entity(id, world);
}

inline void PluginSelectEntity(const PluginHostApi* host, ECS::Entity entity) {
    if (!host || !host->setSelectedEntityId) return;
    host->setSelectedEntityId(host->editorContext, entity.isValid() ? entity.id() : 0);
}

inline void PluginMarkDirty(const PluginHostApi* host) {
    if (host && host->markSceneDirty) host->markSceneDirty(host->editorContext);
}

inline void PluginMarkAssetBrowserDirty(const PluginHostApi* host) {
    if (host && host->markAssetBrowserDirty) host->markAssetBrowserDirty(host->editorContext);
}

inline std::filesystem::path PluginProjectRoot(const PluginHostApi* host) {
    if (!host || !host->getProjectRootPath) return {};
    char buffer[1024] = {};
    if (!host->getProjectRootPath(host->editorContext, buffer, sizeof(buffer))) return {};
    return std::filesystem::path(buffer);
}

inline std::filesystem::path PluginScenePath(const PluginHostApi* host) {
    if (!host || !host->getCurrentScenePath) return {};
    char buffer[1024] = {};
    if (!host->getCurrentScenePath(host->editorContext, buffer, sizeof(buffer))) return {};
    return std::filesystem::path(buffer);
}

inline bool PluginGetTerrainBrush(const PluginHostApi* host, PluginTerrainBrush& out) {
    if (!host || !host->getTerrainBrush) return false;
    return host->getTerrainBrush(host->editorContext, &out);
}

inline bool PluginSetTerrainBrush(const PluginHostApi* host, const PluginTerrainBrush& brush) {
    if (!host || !host->setTerrainBrush) return false;
    return host->setTerrainBrush(host->editorContext, &brush);
}

}  // namespace Caffeine::Editor
