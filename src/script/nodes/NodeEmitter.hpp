#pragma once

#include "script/nodes/NodeGraph.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Script::Nodes {

struct EmitResult {
    bool ok = false;
    std::string error;
    std::string dyd;
    std::string nodeJson;
};

class NodeEmitter {
public:
    static EmitResult emit(const NodeGraph& graph);

    /// Writes `graph.scriptFileName` and sibling `.node.json` next to it.
    static bool writeSiblingFiles(const std::filesystem::path& scriptPath, const NodeGraph& graph,
                                  std::string* outError = nullptr);
};

} // namespace Caffeine::Script::Nodes
