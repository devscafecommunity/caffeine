#include "caffeine/ui/PanelRegistry.hpp"

#include "caffeine/ui/Theme.hpp"

#include <imgui.h>

#include <algorithm>

namespace Caffeine::UIKit {

PanelRegistry& PanelRegistry::instance() {
    static PanelRegistry registry;
    return registry;
}


bool PanelRegistry::add(PanelDesc desc) {
    if (desc.id.empty()) return false;
    auto it = std::find_if(m_panels.begin(), m_panels.end(),
                           [&](const PanelDesc& panel) { return panel.id == desc.id; });
    if (it != m_panels.end()) {
        const bool wasVisible = it->visible;
        *it = std::move(desc);
        // A hot reload must not resurrect a panel the user closed.
        it->visible = wasVisible;
        ++m_revision;
        return true;
    }
    desc.visible = desc.defaultOpen;
    if (!desc.defaultOpen && desc.close) desc.close();
    m_panels.push_back(std::move(desc));
    ++m_revision;
    return true;
}

bool PanelRegistry::remove(const std::string& id) {
    auto it = std::find_if(m_panels.begin(), m_panels.end(),
                           [&](const PanelDesc& panel) { return panel.id == id; });
    if (it == m_panels.end()) return false;
    m_panels.erase(it);
    ++m_revision;
    return true;
}

void PanelRegistry::removeOwner(const std::string& owner) {
    if (owner.empty()) return;
    const auto before = m_panels.size();
    m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(),
                                  [&](const PanelDesc& panel) { return panel.owner == owner; }),
                   m_panels.end());
    if (m_panels.size() != before) ++m_revision;
}

void PanelRegistry::clear() {
    m_panels.clear();
    ++m_revision;
}

PanelDesc* PanelRegistry::find(const std::string& id) {
    for (auto& panel : m_panels) {
        if (panel.id == id) return &panel;
    }
    return nullptr;
}

const PanelDesc* PanelRegistry::find(const std::string& id) const {
    for (const auto& panel : m_panels) {
        if (panel.id == id) return &panel;
    }
    return nullptr;
}

bool PanelRegistry::isVisible(const std::string& id) const {
    const PanelDesc* panel = find(id);
    return panel && panel->visible;
}

void PanelRegistry::setVisible(const std::string& id, bool visible) {
    PanelDesc* panel = find(id);
    if (!panel) return;
    if (visible == panel->visible) return;
    panel->visible = visible;
    if (visible) {
        m_pendingFocus = id;
        if (panel->open) panel->open();
    } else if (panel->close) {
        panel->close();
    }
}

void PanelRegistry::toggleVisible(const std::string& id) {
    const PanelDesc* panel = find(id);
    if (panel) setVisible(id, !panel->visible);
}

void PanelRegistry::open(const std::string& id) { setVisible(id, true); }

void PanelRegistry::renderAll() {
    for (auto& panel : m_panels) {
        // Keep registry visibility in sync with the panel's own open flag so
        // legacy View-menu toggles and the registry menu cannot diverge.
        if (panel.isOpen) panel.visible = panel.isOpen();
        if (!panel.visible || !panel.render) continue;

        const bool focus = m_pendingFocus == panel.id;
        if (focus) {
            // Docked panels live in tabs. SetWindowFocus selects the tab as
            // well as raising the window; SetNextWindowFocus alone does not.
            ImGui::SetWindowFocus(panel.title.c_str());
            ImGui::SetNextWindowFocus();
            m_pendingFocus.clear();
        }
        const float minWidth = 240.0f * uiScale();
        const float minHeight = 120.0f * uiScale();
        ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, minHeight),
                                            ImVec2(FLT_MAX, FLT_MAX));
        panel.render();
    }
}

void PanelRegistry::applyDefaults() {
    for (auto& panel : m_panels) {
        panel.visible = panel.defaultOpen;
        if (panel.visible) {
            if (panel.open) panel.open();
        } else if (panel.close) {
            panel.close();
        }
    }
}

void PanelRegistry::drawMenu() {
    std::vector<std::string> categories;
    for (const auto& panel : m_panels) {
        if (std::find(categories.begin(), categories.end(), panel.category) == categories.end()) {
            categories.push_back(panel.category);
        }
    }

    for (const auto& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (auto& panel : m_panels) {
            if (panel.category != category) continue;
            bool visible = panel.visible;
            if (ImGui::MenuItem(panel.title.c_str(), nullptr, &visible)) {
                setVisible(panel.id, visible);
            }
        }
        ImGui::EndMenu();
    }
}

}  // namespace Caffeine::UIKit
