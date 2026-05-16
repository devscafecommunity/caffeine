#include "editor/CommandPalette.hpp"
#include <algorithm>
#include <cstring>

namespace Caffeine::Editor {

void CommandPalette::registerCommand(const FixedString<64>& id,
                                     const FixedString<128>& label,
                                     const FixedString<128>& category,
                                     std::function<void()> callback,
                                     bool enabled) {
    for (auto& cmd : m_commands) {
        if (cmd.id == id) return;
    }

    m_commands.push_back({id, label, category, std::move(callback), enabled});
}

void CommandPalette::unregisterCommand(const FixedString<64>& id) {
    std::erase_if(m_commands, [&id](const Item& cmd) {
        return cmd.id == id;
    });
}

void CommandPalette::open() {
    m_open = true;
    m_selectedIndex = 0;
    m_searchBuffer[0] = '\0';
    filterResults("");
}

void CommandPalette::close() {
    m_open = false;
}

void CommandPalette::toggle() {
    if (m_open) close();
    else open();
}

bool CommandPalette::handleInput() {
    return false;
}

void CommandPalette::clearResults() {
    m_filteredResults.clear();
}

void CommandPalette::filterResults(const char* query) {
    m_filteredResults.clear();

    if (!query || query[0] == '\0') {
        for (const auto& cmd : m_commands) {
            if (cmd.enabled) {
                m_filteredResults.push_back(&cmd);
            }
        }
    } else {
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (const auto& cmd : m_commands) {
            if (!cmd.enabled) continue;

            std::string lowerLabel = cmd.label.cStr();
            std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), ::tolower);

            std::string lowerCategory = cmd.category.cStr();
            std::transform(lowerCategory.begin(), lowerCategory.end(), lowerCategory.begin(), ::tolower);

            if (lowerLabel.find(lowerQuery) != std::string::npos ||
                lowerCategory.find(lowerQuery) != std::string::npos) {
                m_filteredResults.push_back(&cmd);
            }
        }
    }

    if (m_selectedIndex >= m_filteredResults.size()) {
        m_selectedIndex = m_filteredResults.empty() ? 0 : m_filteredResults.size() - 1;
    }
}

const CommandPaletteItem* CommandPalette::result(usize index) const {
    if (index < m_filteredResults.size()) {
        return m_filteredResults[index];
    }
    return nullptr;
}

void CommandPalette::setSelected(usize index) {
    if (index < m_filteredResults.size()) {
        m_selectedIndex = index;
    }
}

void CommandPalette::executeSelected() {
    if (m_selectedIndex < m_filteredResults.size()) {
        const Item* item = m_filteredResults[m_selectedIndex];
        if (item && item->callback && item->enabled) {
            item->callback();
        }
    }
}

void CommandPalette::nextItem() {
    if (m_filteredResults.empty()) return;
    m_selectedIndex = (m_selectedIndex + 1) % m_filteredResults.size();
}

void CommandPalette::previousItem() {
    if (m_filteredResults.empty()) return;
    if (m_selectedIndex == 0) {
        m_selectedIndex = m_filteredResults.size() - 1;
    } else {
        m_selectedIndex--;
    }
}

}

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void CommandPalette::render() {
    if (!m_open) return;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("Command Palette", nullptr, flags)) {
        ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 20);
        if (ImGui::InputText("##search", m_searchBuffer, sizeof(m_searchBuffer), 
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            executeSelected();
            close();
        }

        if (ImGui::IsItemActive() && !ImGui::IsItemEdited()) {
            filterResults(m_searchBuffer);
        }

        ImGui::Separator();

        ImGui::BeginChild("results", ImVec2(0, 300), false, ImGuiWindowFlags_NoNav);

        for (usize i = 0; i < m_filteredResults.size(); ++i) {
            const Item* item = m_filteredResults[i];
            if (!item) continue;

            bool isSelected = (i == m_selectedIndex);

            if (isSelected) {
                ImGui::SetNextItemOpen(true);
            }

            std::string label = std::string("[") + item->category.cStr() + "] " + item->label.cStr();

            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_None)) {
                m_selectedIndex = i;
                item->callback();
                close();
            }

            if (ImGui::IsItemHovered() && isSelected) {
                ImGui::SetTooltip("Press Enter to execute");
            }
        }

        ImGui::EndChild();

        ImGui::Separator();
        ImGui::TextDisabled("↑↓ Navigate  Enter Execute  Esc Close");
    }
    ImGui::End();
}

}

#endif