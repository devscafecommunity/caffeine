#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"

namespace Caffeine::Editor {

/// Visual editor for the retained-mode HUD (UIWidget). Behavior stays in Lua and C++.
class HudEditorPanel {
public:
    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void onImGuiRender(EditorContext& ctx);

private:
    bool m_open = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_undoOpen = false;
    u32 m_dragEntity = 0xFFFFFFFFu;
};

}  // namespace Caffeine::Editor
