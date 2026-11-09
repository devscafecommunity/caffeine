#pragma once

#include "core/Types.hpp"

#include <string>

namespace Caffeine::ECS {
class World;
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
    void drawEditTools(ECS::World& world, EditorContext& ctx);
    void drawDataFile(ECS::World& world, EditorContext& ctx);

    bool m_open = true;
    std::string m_status;
};

}  // namespace Caffeine::Editor
