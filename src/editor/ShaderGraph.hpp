#pragma once
#include "editor/ShaderNode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Caffeine::Editor {

class ShaderGraph {
public:
    uint32_t addNode(NodeType type);
    bool removeNode(uint32_t nodeID);

    bool connect(uint32_t fromNode, int fromPin, uint32_t toNode, int toPin);
    bool disconnect(uint32_t fromNode, int fromPin);
    void clear();

    ShaderNode* getNode(uint32_t nodeID);
    const ShaderNode* getNode(uint32_t nodeID) const;

    void getNodeConnections(uint32_t nodeID,
        std::vector<std::pair<int, int>>& inputs,
        std::vector<std::pair<int, int>>& outputs) const;

    std::string compileGLSL() const;
    std::string compileHLSL() const;

    std::vector<std::unique_ptr<ShaderNode>>& nodes() { return m_nodes; }
    const std::vector<std::unique_ptr<ShaderNode>>& nodes() const { return m_nodes; }

    bool empty() const { return m_nodes.empty(); }
    size_t nodeCount() const { return m_nodes.size(); }
    size_t connectionCount() const { return m_connections.size(); }

    struct Connection {
        uint32_t fromNode;
        int fromPin;
        uint32_t toNode;
        int toPin;
    };
    const std::vector<Connection>& connections() const { return m_connections; }

private:
    uint32_t m_nextID = 1;
    std::vector<std::unique_ptr<ShaderNode>> m_nodes;
    std::vector<Connection> m_connections;

    bool canConnect(PinType fromType, PinType toType) const;
    void topologicalSort(std::vector<uint32_t>& sortedIDs) const;
};

} // namespace Caffeine::Editor
