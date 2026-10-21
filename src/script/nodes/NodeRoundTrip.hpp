#pragma once

#include "script/nodes/NodeGraph.hpp"

#include <string>

namespace Caffeine::Script::Nodes {

enum class ImportStatus {
    NoMarkers,
    Success,
    Failed,
};

struct ImportResult {
    ImportStatus status = ImportStatus::Failed;
    std::string error;
    std::string message;
    NodeGraph graph;
};

bool dydSourceHasNodeMarkers(const std::string& dydSource);

/// Rebuilds graph topology from emitter-style `// @node` markers. Layout (`x`/`y`) is taken from
/// `layoutHint` when node ids match. Without markers, returns `NoMarkers` and an empty graph.
ImportResult importFromDyd(const std::string& dydSource, const std::string& scriptFileName,
                           const NodeGraph* layoutHint = nullptr);

void mergeGraphLayout(NodeGraph& graph, const NodeGraph& layoutHint);

} // namespace Caffeine::Script::Nodes
