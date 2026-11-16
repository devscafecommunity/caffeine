#pragma once

#include "core/Types.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class EditorSpinner {
public:
    // Lightweight animated spinner (no per-frame texture allocation).
    static void draw(const char* id, ImVec2 size = ImVec2(18, 18),
                     ImU32 color = IM_COL32(210, 80, 130, 255));

    // SVG-based ring spinner from assets/spinners (cached texture, rotated each frame).
    static void drawSvg(const char* id, ImVec2 size = ImVec2(20, 20),
                        ImU32 tint = IM_COL32(210, 80, 130, 255));
};

} // namespace Caffeine::Editor
