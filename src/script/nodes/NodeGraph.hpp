#pragma once

#include "core/Types.hpp"

#include <string>
#include <vector>

namespace Caffeine::Script::Nodes {

enum class NodeKind {
    OnUpdate,
    GetPosition,
    DeltaTime,
    SetPosition,
    Translate,
    SetScale,
    GetScale,
    Log,
    Constant,
    Branch,
};

const char* nodeKindToTypeName(NodeKind kind);
bool nodeKindFromTypeName(const std::string& name, NodeKind& out);

struct GraphNode {
    std::string id;
    NodeKind kind = NodeKind::OnUpdate;
    f32 x = 0.0f;
    f32 y = 0.0f;

    /// Constant: numeric value or 0/1 for bool.
    double numberValue = 0.0;
    bool boolValue = false;
    bool constantIsBool = false;

    /// Fallback values used when a data input has no incoming edge. Vector
    /// nodes use X/Y/Z; Branch uses the condition. This lets the canvas show
    /// editable fields without a Constant node per input.
    double inputX = 0.0;
    double inputY = 0.0;
    double inputZ = 0.0;
    bool inputCondition = false;

    /// Log: message written to the console. Kept last so existing aggregate
    /// initializers keep their field order.
    std::string textValue = "Hello from Dryad";
};

struct GraphEdge {
    std::string from;
    std::string to;
};

struct NodeGraph {
    int version = 1;
    std::string scriptFileName;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;

    const GraphNode* findNode(const std::string& id) const;
    GraphNode* findNode(const std::string& id);
};

struct PinRef {
    std::string nodeId;
    std::string pin;
};

bool parsePinRef(const std::string& endpoint, PinRef& out);

} // namespace Caffeine::Script::Nodes
