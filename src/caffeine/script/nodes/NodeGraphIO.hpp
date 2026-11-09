#pragma once

#include "script/nodes/NodeGraph.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Script::Nodes {

struct LoadResult {
    bool ok = false;
    std::string error;
    NodeGraph graph;
};

std::filesystem::path nodeJsonPathForScript(const std::filesystem::path& scriptPath);

LoadResult loadFromJsonFile(const std::filesystem::path& path);
LoadResult loadFromJsonString(const std::string& json);

NodeGraph defaultGraphForScript(const std::filesystem::path& scriptPath);

} // namespace Caffeine::Script::Nodes
