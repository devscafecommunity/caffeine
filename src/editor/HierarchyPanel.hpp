#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstring>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class HierarchyPanel {
public:
    explicit HierarchyPanel(EditorContext* context) : m_context(context) {}

    void render(ECS::World& world, EditorContext& ctx);
    void onImGuiRender();

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

    EditorContext* context() const { return m_context; }
    void setContext(EditorContext* ctx) { m_context = ctx; }
    void setWorld(ECS::World* world) { m_world = world; }

private:
    void renderSearchBar();
    void renderToolbar();
    void renderEntityNode(ECS::Entity entity);
    void renderEmptyContextMenu();
    void handleDeleteKey();
    void createEntityWithType(ECS::World& world, const char* name, const char* componentType);

    bool hasChildren(ECS::Entity entity) const;
    void renderChildren(ECS::Entity parent);

    EditorContext* m_context = nullptr;
    ECS::World*    m_world   = nullptr;

    bool m_open = true;
    char m_searchFilter[256] = "";

    static constexpr u32 MAX_VISIBLE = 4096;
    ECS::Entity m_entities[MAX_VISIBLE];
    u32 m_entityCount = 0;
    ECS::Entity m_renaming = ECS::Entity::INVALID;
};

} // namespace Caffeine::Editor
