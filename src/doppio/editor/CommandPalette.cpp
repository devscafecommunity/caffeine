#include "editor/CommandPalette.hpp"
#include "editor/PluginSystem.hpp"
#include "caffeine/ui/UI.hpp"
#include <algorithm>
#include <cstring>
#include <memory>

namespace Caffeine::Editor {

CommandPalette::~CommandPalette() = default;

void CommandPalette::registerCommand(const FixedString<64>& id,
                                     const FixedString<128>& label,
                                     const FixedString<128>& category,
                                     std::function<void()> callback,
                                     bool enabled,
                                     const char* plugin,
                                     const char* keywords) {
    for (auto& cmd : m_commands) {
        if (cmd.id == id) return;
    }

    Item item;
    item.id = id;
    item.label = label;
    item.category = category;
    item.callback = std::move(callback);
    item.enabled = enabled;
    if (plugin) item.plugin = plugin;
    if (keywords) item.keywords = keywords;
    m_commands.push_back(std::move(item));
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
    m_focusSearch = true;
    filterResults("");
    if (!m_dialog) {
        m_dialog = std::make_unique<Caffeine::UIKit::Elements::Dialog>("Quick Search");
        m_dialog->setInitialSize(ImVec2(560.0f, 460.0f));
        m_dialog->setCenterTop(0.22f);
        m_dialog->setOnClose([this] { m_open = false; });
    }
    m_dialog->open();
}

void CommandPalette::close() {
    m_open = false;
    if (m_dialog) m_dialog->close();
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

    auto visible = [](const Item& cmd) {
        if (!cmd.enabled) return false;
        return PluginManager::instance().isContributionEnabled(cmd.plugin);
    };

    if (!query || query[0] == '\0') {
        for (const auto& cmd : m_commands) {
            if (visible(cmd)) m_filteredResults.push_back(&cmd);
        }
    } else {
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (const auto& cmd : m_commands) {
            if (!visible(cmd)) continue;

            std::string haystack = std::string(cmd.label.cStr()) + " " + cmd.category.cStr() + " " +
                                   cmd.id.cStr() + " " + cmd.keywords;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);

            if (haystack.find(lowerQuery) != std::string::npos) {
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
    if (!m_open || !m_dialog) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nextItem();
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) previousItem();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        executeSelected();
        close();
        return;
    }

    filterResults(m_searchBuffer);
    m_dialog->clear();

    auto& search = m_dialog->emplace<Elements::SearchField>(
        m_searchBuffer, sizeof(m_searchBuffer), "Panels, actions, plugins...");
    search.setGrow(true);
    if (m_focusSearch) {
        search.focusNextFrame();
        m_focusSearch = false;
    }

    auto& results = m_dialog->emplace<ListBox>("command_results");
    results.setHeight(320.0f);
    for (usize i = 0; i < m_filteredResults.size(); ++i) {
        const Item* item = m_filteredResults[i];
        if (!item) continue;
        const std::string label =
            std::string("[") + item->category.cStr() + "] " + item->label.cStr();
        auto& row = results.emplace<ListItem>(label, i == m_selectedIndex);
        row.setId("command:" + std::string(item->id.cStr()));
        row.setScrollHere(i == m_selectedIndex);
        row.setOnClick([this, i] {
            setSelected(i);
            executeSelected();
            close();
        });
    }
    if (m_filteredResults.empty()) {
        results.emplace<Label>("Nothing found.", TextTone::Muted);
    }
    m_dialog->emplace<Label>("Enter runs · ↑↓ navigate · Esc closes", TextTone::Muted);

    UIContext ui;
    ui.draw(*m_dialog);
}
}

#endif