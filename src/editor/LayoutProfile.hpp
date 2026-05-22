#pragma once
#include "core/Types.hpp"
#include <string>
#include <vector>

namespace Caffeine::Editor {

// ============================================================================
// LayoutProfile — represents a saved layout configuration for all panels.
// ============================================================================
struct LayoutProfile {
    std::string name;
    bool hierarchyOpen = true;
    bool inspectorOpen = true;
    bool viewportOpen = true;
    bool assetsOpen = true;
    bool consoleOpen = true;
    bool profilerOpen = false;
    bool animationTimelineOpen = false;
    bool animatorControllerOpen = false;
    bool tilemapEditorOpen = false;
    bool scriptEditorOpen = false;

    // Panel size hints (0-1 normalized, relative to window)
    f32 hierarchyWidth = 0.25f;
    f32 inspectorWidth = 0.2f;
    f32 viewportWidth = 0.5f;

    bool operator==(const LayoutProfile& other) const {
        return name == other.name
            && hierarchyOpen == other.hierarchyOpen
            && inspectorOpen == other.inspectorOpen
            && viewportOpen == other.viewportOpen
            && assetsOpen == other.assetsOpen
            && consoleOpen == other.consoleOpen
            && profilerOpen == other.profilerOpen
            && animationTimelineOpen == other.animationTimelineOpen
            && animatorControllerOpen == other.animatorControllerOpen
            && tilemapEditorOpen == other.tilemapEditorOpen
            && scriptEditorOpen == other.scriptEditorOpen;
    }

    static LayoutProfile defaultLayout() {
        return LayoutProfile{
            "Default",
            true,   // hierarchy
            false,  // inspector - HIDDEN BY DEFAULT
            true,   // viewport
            false,  // assets - HIDDEN BY DEFAULT
            false,  // console - HIDDEN BY DEFAULT
            false,  // profiler
            false,  // animation timeline
            false,  // tilemap
            false   // script editor
        };
    }

    static LayoutProfile verticalLayout() {
        return LayoutProfile{
            "Vertical",
            true,   // hierarchy (left)
            true,   // inspector (right)
            true,   // viewport (center)
            true,   // assets (bottom left)
            true,   // console (bottom)
            false,
            false,
            false,
            false
        };
    }

    static LayoutProfile horizontalLayout() {
        return LayoutProfile{
            "Horizontal",
            true,
            true,
            true,
            false,  // assets hidden
            true,
            false,
            false,
            false,
            false
        };
    }

    static LayoutProfile compactLayout() {
        return LayoutProfile{
            "Compact",
            true,   // hierarchy
            false,  // inspector hidden
            true,   // viewport
            false,  // assets hidden
            true,   // console
            false,
            false,
            false,
            false
        };
    }

    static LayoutProfile fullscreenLayout() {
        return LayoutProfile{
            "Fullscreen",
            false,  // only viewport
            false,
            true,
            false,
            false,
            false,
            false,
            false,
            false
        };
    }
};

} // namespace Caffeine::Editor
