#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentID.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"
#include "containers/HashMap.hpp"
#include <functional>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstdio>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class InspectorPanel {
public:
    using ComponentDrawer = std::function<void(void* componentData)>;

    InspectorPanel() = default;

    void registerDrawer(u32 componentTypeId, ComponentDrawer drawer);
    void render(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void drawTransform(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawSprite(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawCamera(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawRigidBody2D(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx);
#endif

    bool m_open = true;
    HashMap<u32, ComponentDrawer> m_drawers;
};

} // namespace Caffeine::Editor
