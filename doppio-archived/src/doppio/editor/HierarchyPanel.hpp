#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/EditorContext.hpp"
#include "physics/PhysicsComponents2D.hpp"

#include <functional>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstring>
#endif

namespace Caffeine::UIKit::Elements {
class Container;
class Dialog;
}

namespace Caffeine::Editor {

class HierarchyPanel {
public:
    explicit HierarchyPanel(EditorContext* context) : m_context(context) {}
    ~HierarchyPanel();

    void render(ECS::World& world, EditorContext& ctx);
    void onImGuiRender();

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

    EditorContext* context() const { return m_context; }
    void setContext(EditorContext* ctx) { m_context = ctx; }
    void setWorld(ECS::World* world) { m_world = world; }

    void duplicateEntity(ECS::World& world, ECS::Entity src);

    void setOpenPresetsCallback(std::function<void()> callback) {
        m_openPresets = std::move(callback);
    }

private:
    void renderToolbar(Caffeine::UIKit::Elements::Container& parent);
    void addEntityItem(Caffeine::UIKit::Elements::Container& parent, ECS::Entity entity,
                       bool recurse);
    void handleEntityDrop(ECS::Entity entity);
    void renderEntityContextMenu(ECS::Entity entity);
    void openRename(ECS::Entity entity);
    void openPlacePrefab();
    void renderDialogs();
    void applyRename();
    void loadPrefab();
    void deleteSelected();
    void renderCreateEntityMenuItems();
    void handleDeleteKey();
    void createEntityWithType(ECS::World& world, const char* name, const char* componentType);

    bool hasChildren(ECS::Entity entity) const;

    EditorContext* m_context = nullptr;
    ECS::World*    m_world   = nullptr;

    bool m_open = true;
    char m_searchFilter[256] = "";

    ECS::Entity m_renaming = ECS::Entity::INVALID;
    ECS::Entity m_lastScrollTarget = ECS::Entity::INVALID;
    ECS::Entity m_expandEntity = ECS::Entity::INVALID;
    std::function<void()> m_openPresets;

    Caffeine::UIKit::Elements::Dialog* m_renameDialog = nullptr;
    Caffeine::UIKit::Elements::Dialog* m_placePrefabDialog = nullptr;
    char m_renameBuffer[64] = {};
    char m_prefabPath[256] = {};
    bool m_renameFocus = false;
    bool m_prefabFocus = false;
    bool m_openCreateMenu = false;
    std::string m_prefabError;
};

} // namespace Caffeine::Editor
