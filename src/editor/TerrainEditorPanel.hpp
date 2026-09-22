#pragma once

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"

namespace Caffeine::Editor {

class TerrainEditorPanel {
public:
    void render(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void drawEditTools(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
    void drawDataFile(ECS::World& world, ECS::Entity entity, EditorContext& ctx);
#endif

    bool m_open = true;
};

}  // namespace Caffeine::Editor
