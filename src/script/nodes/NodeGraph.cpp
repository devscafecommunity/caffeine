#include "script/nodes/NodeGraph.hpp"

namespace Caffeine::Script::Nodes {

const char* nodeKindToTypeName(NodeKind kind) {
    switch (kind) {
    case NodeKind::OnUpdate: return "OnUpdate";
    case NodeKind::GetPosition: return "GetPosition";
    case NodeKind::SetPosition: return "SetPosition";
    case NodeKind::Constant: return "Constant";
    case NodeKind::Branch: return "Branch";
    }
    return "OnUpdate";
}

bool nodeKindFromTypeName(const std::string& name, NodeKind& out) {
    if (name == "OnUpdate") {
        out = NodeKind::OnUpdate;
        return true;
    }
    if (name == "GetPosition") {
        out = NodeKind::GetPosition;
        return true;
    }
    if (name == "SetPosition") {
        out = NodeKind::SetPosition;
        return true;
    }
    if (name == "Constant") {
        out = NodeKind::Constant;
        return true;
    }
    if (name == "Branch") {
        out = NodeKind::Branch;
        return true;
    }
    return false;
}

const GraphNode* NodeGraph::findNode(const std::string& id) const {
    for (const auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

GraphNode* NodeGraph::findNode(const std::string& id) {
    for (auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

bool parsePinRef(const std::string& endpoint, PinRef& out) {
    const auto dot = endpoint.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size()) return false;
    out.nodeId = endpoint.substr(0, dot);
    out.pin = endpoint.substr(dot + 1);
    return true;
}

} // namespace Caffeine::Script::Nodes
