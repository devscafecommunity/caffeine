#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentID.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"
#include "script/ScriptTypes.hpp"
#include "containers/HashMap.hpp"
#include "ui/UIComponents.hpp"
#include "editor/InspectorWidgets.hpp"
#include "editor/ComponentRegistry.hpp"
#include <functional>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstdio>
#endif

namespace Caffeine::Editor {

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
    void drawCollider2D(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawAudioSource(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawScript(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawCppScript(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawPersistent(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawMeshFilter(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawUIWidget(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawUIButton(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawUILabel(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawUIProgressBar(ECS::World& world, ECS::Entity e, EditorContext& ctx);
    void drawUISlider(ECS::World& world, ECS::Entity e, EditorContext& ctx);

    std::filesystem::path resolveProjectRoot(const EditorContext& ctx) const;
#endif

    bool m_open = true;
    bool m_undoStarted = false;
    HashMap<u32, ComponentDrawer> m_drawers;
    char m_addComponentSearch[128] = {};
};

} // namespace Caffeine::Editor
