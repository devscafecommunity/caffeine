#pragma once

/**
 * Caffeine UI — panel descriptor.
 *
 * A panel declares its identity and preferred dock area once. Doppio and the
 * Workspace use descriptors to build the layout; plugins use the same shape
 * through the plugin host API.
 */

#include <functional>
#include <string>

namespace Caffeine::UIKit {

enum class DockArea : int {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Center = 3,
    Floating = 4,
};

struct PanelDesc {
    /// Stable machine id, e.g. "core.hierarchy" or "plugin.terrain.generator".
    std::string id;
    /// Window title. Must match the title passed to ImGui::Begin.
    std::string title;
    /// Semantic icon name resolved by the host icon provider.
    std::string icon;
    /// Menu grouping, e.g. "Core", "Plugins", "Debug".
    std::string category = "Core";
    /// Empty for core panels; plugin library stem for plugin contributions.
    std::string owner;

    DockArea dock = DockArea::Center;
    /// Normalized split size hint (0..1) used when the panel owns an area.
    float size = 0.22f;
    bool defaultOpen = true;
    bool visible = true;
    bool closable = true;
    int order = 0;

    std::function<void()> render;
    std::function<void()> open;
    std::function<void()> close;
    std::function<bool()> isOpen;
};

}  // namespace Caffeine::UIKit
