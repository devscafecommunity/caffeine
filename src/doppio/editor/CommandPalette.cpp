#include "editor/CommandPalette.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/PluginSystem.hpp"
#include <algorithm>
#include <cstring>

namespace Caffeine::Editor {

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
    if (!m_open) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.22f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 280.0f), ImVec2(display.x * 0.9f, display.y * 0.9f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Quick Search", &m_open, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    bool scrollToSelection = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        nextItem();
        scrollToSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        previousItem();
        scrollToSelection = true;
    }

    if (m_focusSearch) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputTextWithHint("##search", "Panels, actions, plugins...", m_searchBuffer,
                                                    sizeof(m_searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_focusSearch) m_focusSearch = false;
    filterResults(m_searchBuffer);
    if (submitted) {
        executeSelected();
        close();
    }

    ImGui::Spacing();
    ImGui::BeginChild("results", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

    const float rowH = ImGui::GetFrameHeight() + 10.0f;
    auto categoryColor = [](const char* category) -> ImU32 {
        if (std::strcmp(category, "Scene") == 0) return IM_COL32(48, 96, 156, 230);
        if (std::strcmp(category, "Animation") == 0) return IM_COL32(156, 92, 48, 230);
        if (std::strcmp(category, "Content") == 0) return IM_COL32(46, 122, 96, 230);
        if (std::strcmp(category, "Debug") == 0) return IM_COL32(122, 72, 156, 230);
        if (std::strcmp(category, "Tools") == 0) return IM_COL32(42, 118, 138, 230);
        if (std::strcmp(category, "Actions") == 0) return IM_COL32(168, 132, 42, 230);
        if (std::strcmp(category, "Plugins") == 0) return IM_COL32(156, 64, 96, 230);
        if (std::strcmp(category, "Panels") == 0) return IM_COL32(48, 96, 156, 230);
        unsigned hash = 2166136261u;
        for (const char* c = category; *c; ++c) hash = (hash ^ static_cast<unsigned char>(*c)) * 16777619u;
        return IM_COL32(70 + (hash & 70), 70 + ((hash >> 8) & 70), 90 + ((hash >> 16) & 80), 230);
    };
    auto iconFor = [](const Item* item) -> const char* {
        const char* id = item->id.cStr();
        if (std::strstr(id, "hierarchy")) return EditorIcon::Hierarchy;
        if (std::strstr(id, "inspector")) return EditorIcon::Inspector;
        if (std::strstr(id, "console")) return EditorIcon::Console;
        if (std::strstr(id, "profiler")) return EditorIcon::Profiler;
        if (std::strstr(id, "debugger")) return EditorIcon::EntityDebugger;
        if (std::strstr(id, "plugin")) return EditorIcon::Plugins;
        if (std::strstr(id, "asset")) return EditorIcon::Assets;
        if (std::strstr(id, "animator")) return EditorIcon::Animator;
        if (std::strstr(id, "animation") || std::strstr(id, "timeline")) return EditorIcon::Animation;
        if (std::strstr(id, "viewport") || std::strstr(id, "camera") || std::strstr(id, "gameplay")) return EditorIcon::Viewport;
        if (std::strstr(id, "settings")) return EditorIcon::Search;
        if (std::strstr(id, "save")) return EditorIcon::Save;
        if (std::strstr(id, "new")) return EditorIcon::NewScene;
        if (std::strstr(id, "build") || std::strstr(id, "toolbox")) return EditorIcon::Build;
        if (std::strcmp(item->category.cStr(), "Plugins") == 0) return EditorIcon::Plugins;
        if (std::strcmp(item->category.cStr(), "Actions") == 0) return EditorIcon::Build;
        return EditorIcon::Folder;
    };
    for (usize i = 0; i < m_filteredResults.size(); ++i) {
        const Item* item = m_filteredResults[i];
        if (!item) continue;
        const bool isSelected = i == m_selectedIndex;
        ImGui::PushID(static_cast<int>(i));
        const float rowW = ImGui::GetContentRegionAvail().x;
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            m_selectedIndex = i;
            if (item->callback) item->callback();
            close();
        }
        if (isSelected && scrollToSelection) ImGui::SetScrollHereY(0.5f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 rowMax(rowPos.x + rowW, rowPos.y + rowH);
        if (isSelected || hovered) {
            draw->AddRectFilled(rowPos, rowMax,
                                isSelected ? IM_COL32(58, 78, 108, 255) : IM_COL32(48, 52, 60, 255), 4.0f);
        }
        const float iconSize = ImGui::GetFontSize();
        const char* icon = iconFor(item);
        if (EditorIcons::hasIcon(icon)) {
            const ImTextureRef tex = EditorIcons::get(icon);
            if (tex._TexData != nullptr || tex._TexID != ImTextureID_Invalid) {
                const ImVec2 iconPos(rowPos.x + 8.0f, rowPos.y + (rowH - iconSize) * 0.5f);
                draw->AddImage(tex, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize));
            }
        }
        const char* category = item->category.cStr();
        const ImVec2 categorySize = ImGui::CalcTextSize(category);
        const float pillW = std::clamp(categorySize.x + 16.0f, 72.0f, 130.0f);
        const ImVec2 pillMin(rowPos.x + 8.0f + iconSize + 8.0f, rowPos.y + 6.0f);
        const ImVec2 pillMax(pillMin.x + pillW, rowPos.y + rowH - 6.0f);
        draw->AddRectFilled(pillMin, pillMax, categoryColor(category), 4.0f);
        draw->AddText(ImVec2(pillMin.x + (pillW - categorySize.x) * 0.5f, pillMin.y + 2.0f),
                      IM_COL32(244, 246, 250, 255), category);
        draw->AddText(ImVec2(pillMax.x + 12.0f, rowPos.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_Text), item->label.cStr());
        ImGui::PopID();
    }
    if (m_filteredResults.empty()) {
        ImGui::TextDisabled("Nothing found.");
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Drag this window.  Enter runs   ↑↓ navigate   Esc closes");
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}

#endif