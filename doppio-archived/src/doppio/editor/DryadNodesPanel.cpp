#include "editor/DryadNodesPanel.hpp"

#include "caffeine/ui/UI.hpp"
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
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_open) return;

    Panel root("Dryad Nodes", "Drag nodes onto the Dryad canvas", "align-left", &m_open);
    UIContext ui;

    auto& search = root.emplace<Elements::SearchField>(m_search, sizeof(m_search), "Search nodes...");
    search.setGrow(true);

    auto& tree = root.emplace<TreeView>("dryad_node_tree");
    tree.setHeight(420.0f);

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

        auto& categoryItem = tree.emplace<TreeItem>(category, true);
        for (const auto* entry : matches) {
            auto& item = categoryItem.emplace<TreeItem>(entry->displayName, false);
            item.setId("node:" + std::string(entry->typeName));
            item.setTooltip(std::string(entry->description) + "\n\n" + entry->typeName);
            item.setDragPayload(kDryadNodePayload, entry->typeName, entry->displayName);
        }
    }

    if (filter.empty()) {
        root.emplace<Separator>(true);
        root.emplace<Label>("Drag a node onto the Dryad canvas to add it.",
                            TextTone::Muted);
    }

    ui.draw(root);
#endif
}

}  // namespace Caffeine::Editor
