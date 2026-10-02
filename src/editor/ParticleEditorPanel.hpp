#pragma once

#include "core/Types.hpp"
#include "editor/EditorContext.hpp"

namespace Caffeine::Editor {

/// Authors basic 2D and 3D particle objects. Simulation stays on EffectComponent.
class ParticleEditorPanel {
public:
    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void onImGuiRender(EditorContext& ctx);

private:
    bool m_open = false;
};

}  // namespace Caffeine::Editor
