#pragma once

/**
 * Caffeine UI — declarative workspace layout.
 *
 * The layout is a small JSON document so it can be reloaded while Doppio is
 * running. Panels are referenced by their stable ids from PanelRegistry.
 */

#include "caffeine/ui/Panel.hpp"

#include <imgui.h>

#include <string>
#include <unordered_map>

namespace Caffeine::UIKit {

class PanelRegistry;

class Workspace {
public:
    struct Config {
        float leftSize = 0.22f;
        float rightSize = 0.25f;
        float bottomSize = 0.27f;
        bool leftEnabled = true;
        bool rightEnabled = true;
        bool bottomEnabled = true;

        /// Explicit area overrides by panel id. Unlisted panels use PanelDesc::dock.
        std::unordered_map<std::string, DockArea> assignments;

        /// Default area for panels owned by a plugin.
        DockArea pluginDock = DockArea::Bottom;
        float pluginSize = 0.24f;
    };

    /// Load a workspace document. Returns false on parse/open errors and keeps
    /// the previous config.
    bool load(const std::string& path, std::string* outError = nullptr);

    /// Built-in layout: areas derived from the registry's descriptors.
    void reset();

    /// Returns true when the file changed since the last load. Call once/frame.
    bool poll();

    /// Rebuild the ImGui dock nodes and dock every registered panel.
    /// `leftSize` / `rightSize` / `bottomSize` <= 0 keep the config values.
    void apply(ImGuiID dockspaceId, PanelRegistry& registry,
               float leftSize = -1.0f, float rightSize = -1.0f,
               float bottomSize = -1.0f);

    bool save(const std::string& path = std::string()) const;

    /// Write the layout to `path`, create parent directories and adopt the file
    /// as the active document. Used to persist the user layout under
    /// `~/.config/caffeine/ui/workspace.json`.
    bool saveTo(const std::string& path);

    /// Per-user layout path. Honours XDG_CONFIG_HOME on Linux and APPDATA on
    /// Windows, falling back to `~/.config/caffeine/ui/workspace.json`.
    static std::string userConfigPath();

    const Config& config() const { return m_config; }
    Config& config() { return m_config; }
    const std::string& path() const { return m_path; }
    bool loaded() const { return m_loaded; }

private:
    Config m_config;
    std::string m_path;
    long long m_mtime = 0;
    bool m_loaded = false;
};

}  // namespace Caffeine::UIKit
