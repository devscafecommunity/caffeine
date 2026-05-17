#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"

#include <vector>
#include <string>
#include <memory>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

struct SceneTab {
    std::string                     name;
    std::string                     path;
    std::unique_ptr<ECS::World>     world;
    ECS::Entity                     selectedEntity = ECS::Entity::INVALID;
    bool                            isDirty        = false;
    UndoStack                       undoStack;
};

class SceneTabManager {
public:
    SceneTabManager() = default;

    int newScene(const char* name = "Untitled");
    int addTab(const char* name, std::unique_ptr<ECS::World> world);
    bool closeScene(int index);
    void setActiveTab(int index, EditorContext& ctx);

    int  activeTabIndex() const { return m_activeTabIndex; }
    int  tabCount()        const { return static_cast<int>(m_tabs.size()); }

    ECS::World*       activeWorld();
    const ECS::World* activeWorld() const;

    SceneTab&       activeTab();
    const SceneTab& activeTab() const;

    SceneTab&       tab(int index);
    const SceneTab& tab(int index) const;

    void captureContext(const EditorContext& ctx);
    void applyContext(EditorContext& ctx) const;

    #ifdef CF_HAS_IMGUI
    struct TabBarResult {
        int  closeCandidate   = -1;
        int  switchToIndex    = -1;
        bool newTabRequested  = false;
    };
    TabBarResult renderTabBar();
    #endif

private:
    std::vector<std::unique_ptr<SceneTab>> m_tabs;
    int m_activeTabIndex = -1;
};

} // namespace Caffeine::Editor
