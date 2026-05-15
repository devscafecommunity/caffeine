#include "editor/SceneTabManager.hpp"

namespace Caffeine::Editor {

int SceneTabManager::newScene(const char* name) {
    auto tab = std::make_unique<SceneTab>();
    tab->name  = name ? name : "Untitled";
    tab->world = std::make_unique<ECS::World>();
    m_tabs.push_back(std::move(tab));
    int idx = static_cast<int>(m_tabs.size() - 1);
    if (m_activeTabIndex < 0) m_activeTabIndex = idx;
    return idx;
}

int SceneTabManager::addTab(const char* name, std::unique_ptr<ECS::World> world) {
    auto tab = std::make_unique<SceneTab>();
    tab->name  = name ? name : "Untitled";
    tab->world = std::move(world);
    m_tabs.push_back(std::move(tab));
    int idx = static_cast<int>(m_tabs.size() - 1);
    if (m_activeTabIndex < 0) m_activeTabIndex = idx;
    return idx;
}

bool SceneTabManager::closeScene(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return false;
    m_tabs.erase(m_tabs.begin() + index);
    if (m_tabs.empty()) {
        newScene("Untitled");
        m_activeTabIndex = 0;
        return true;
    }
    if (index <= m_activeTabIndex) {
        m_activeTabIndex = std::max(0, m_activeTabIndex - 1);
    }
    if (m_activeTabIndex >= static_cast<int>(m_tabs.size())) {
        m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
    }
    return true;
}

void SceneTabManager::setActiveTab(int index, EditorContext& ctx) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || index == m_activeTabIndex) return;
    if (m_activeTabIndex >= 0) captureContext(ctx);
    m_activeTabIndex = index;
    applyContext(ctx);
}

ECS::World* SceneTabManager::activeWorld() {
    if (m_activeTabIndex < 0) return nullptr;
    return m_tabs[m_activeTabIndex]->world.get();
}

const ECS::World* SceneTabManager::activeWorld() const {
    if (m_activeTabIndex < 0) return nullptr;
    return m_tabs[m_activeTabIndex]->world.get();
}

SceneTab& SceneTabManager::activeTab() {
    return tab(m_activeTabIndex);
}

const SceneTab& SceneTabManager::activeTab() const {
    return tab(m_activeTabIndex);
}

SceneTab& SceneTabManager::tab(int index) {
    return *m_tabs[index];
}

const SceneTab& SceneTabManager::tab(int index) const {
    return *m_tabs[index];
}

void SceneTabManager::captureContext(const EditorContext& ctx) {
    if (m_activeTabIndex < 0) return;
    auto& t = *m_tabs[m_activeTabIndex];
    t.selectedEntity  = ctx.selectedEntity;
    t.isDirty         = ctx.isDirty;
    t.undoStack       = ctx.undoStack;
}

void SceneTabManager::applyContext(EditorContext& ctx) const {
    if (m_activeTabIndex < 0) return;
    const auto& t = *m_tabs[m_activeTabIndex];
    ctx.selectedEntity   = t.selectedEntity;
    ctx.isDirty          = t.isDirty;
    ctx.undoStack        = t.undoStack;
}

} // namespace Caffeine::Editor


#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

SceneTabManager::TabBarResult SceneTabManager::renderTabBar() {
    TabBarResult result;
    if (m_tabs.empty()) return result;

    ImGuiTabBarFlags tbFlags = ImGuiTabBarFlags_Reorderable
                             | ImGuiTabBarFlags_AutoSelectNewTabs
                             | ImGuiTabBarFlags_TabListPopupButton;

    if (!ImGui::BeginTabBar("SceneTabs", tbFlags)) return result;

    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        auto& tab = *m_tabs[i];
        std::string label = tab.name;
        if (tab.isDirty) label += " *";

        ImGuiTabItemFlags itemFlags = (i == m_activeTabIndex)
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;

        bool tabOpen = true;
        if (ImGui::BeginTabItem(label.c_str(), &tabOpen, itemFlags)) {
            if (i != m_activeTabIndex) {
                result.switchToIndex = i;
            }
            ImGui::EndTabItem();
        }

        if (!tabOpen) {
            result.closeCandidate = i;
        }
    }

    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip)) {
        result.newTabRequested = true;
    }

    ImGui::EndTabBar();
    return result;
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
