#pragma once

#include "core/Types.hpp"
#include "editor/EditorContext.hpp"

namespace Caffeine::Editor {

/// Authors volumetric lights, fog, and the other scene effects.
class EffectEditorPanel {
public:
    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void onImGuiRender(EditorContext& ctx);

private:
    bool m_open = false;
    int m_fogStyle = 0;
    int m_volumeShape = 0;
};

}  // namespace Caffeine::Editor
