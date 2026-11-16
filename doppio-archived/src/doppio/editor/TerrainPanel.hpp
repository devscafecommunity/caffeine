#pragma once

#include "core/Types.hpp"

#include <string>

namespace Caffeine::ECS {
class World;
}

namespace Caffeine::UIKit::Elements {
class Container;
}

namespace Caffeine::Editor {

class EditorContext;

/// Native terrain authoring panel. Sculpting, splat painting and .cterrain
/// export/import belong to the engine because terrain is a native component.
class TerrainPanel {
public:
    void render(ECS::World& world, EditorContext& ctx);

    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

private:
    void drawEditTools(ECS::World& world, EditorContext& ctx,
                       Caffeine::UIKit::Elements::Container& parent);
    void drawDataFile(ECS::World& world, EditorContext& ctx,
                      Caffeine::UIKit::Elements::Container& parent);

    bool m_open = true;
    std::string m_status;
    bool m_sculptMode = false;
    bool m_splatMode = false;
    int m_brushIndex = 0;
    int m_layerIndex = 0;
};

}  // namespace Caffeine::Editor
