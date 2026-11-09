#include "editor/DryadNodesPanel.hpp"

#include "caffeine/ui/Widgets.hpp"
#include "script/nodes/NodeCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

namespace {

std::string lowered(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

constexpr const char* kDryadNodePayload = "DRYAD_NODE";

}  // namespace

void DryadNodesPanel::render() {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;

    if (UIKit::BeginPanel("Dryad Nodes", &m_open)) {
        UIKit::PanelHeader("Dryad Nodes", nullptr, "align-left");

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##dryad_node_search", "Search nodes...", m_search,
                                 sizeof(m_search));

        const std::string filter = lowered(m_search);
        const auto& entries = Script::Nodes::nodeCatalog();

        std::vector<std::string> categories;
        for (const auto& entry : entries) {
            if (std::find(categories.begin(), categories.end(), entry.category) ==
                categories.end()) {
                categories.emplace_back(entry.category);
            }
        }

        for (const std::string& category : categories) {
            std::vector<const Script::Nodes::NodeCatalogEntry*> matches;
            for (const auto& entry : entries) {
                if (entry.category != category) continue;
                const std::string haystack =
                    lowered(std::string(entry.displayName) + " " + entry.typeName + " " +
                            entry.description);
                if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;
                matches.push_back(&entry);
            }
            if (matches.empty()) continue;

            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
            if (!ImGui::TreeNodeEx(category.c_str(),
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }
            for (const auto* entry : matches) {
                ImGui::PushID(entry->typeName);
                ImGui::Selectable(entry->displayName, false,
                                  ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n\n%s", entry->description, entry->typeName);
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    char typeName[64] = {};
                    std::strncpy(typeName, entry->typeName, sizeof(typeName) - 1);
                    ImGui::SetDragDropPayload(kDryadNodePayload, typeName,
                                              std::strlen(typeName) + 1);
                    ImGui::Text("Add %s", entry->displayName);
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (filter.empty()) {
            UIKit::SeparatorSpaced();
            UIKit::TextMuted("Drag a node onto the Dryad canvas to add it.");
        }
    }
    UIKit::EndPanel();
#endif
}

}  // namespace Caffeine::Editor
