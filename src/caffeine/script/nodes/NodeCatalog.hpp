#pragma once

#include "script/nodes/NodeGraph.hpp"

#include <string>
#include <vector>

namespace Caffeine::Script::Nodes {

/// Metadata shared by the canvas, the Dryad Nodes palette and the emitter.
/// The category is a display path such as "Language / Flow".
struct NodeCatalogEntry {
    NodeKind kind = NodeKind::OnUpdate;
    const char* typeName = "";
    const char* displayName = "";
    const char* category = "";
    const char* description = "";
};

const std::vector<NodeCatalogEntry>& nodeCatalog();
const NodeCatalogEntry* findNodeCatalogEntry(const std::string& typeName);

} // namespace Caffeine::Script::Nodes
