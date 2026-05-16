#include "editor/ShaderGraph.hpp"
#include <algorithm>
#include <queue>
#include <set>
#include <fmt/format.h>

namespace Caffeine::Editor {

uint32_t ShaderGraph::addNode(NodeType type) {
    auto node = createNode(type, m_nextID);
    if (!node) return 0;
    m_nodes.push_back(std::move(node));
    return m_nextID++;
}

bool ShaderGraph::removeNode(uint32_t nodeID) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
        [nodeID](const auto& n) { return n->id() == nodeID; });
    if (it == m_nodes.end()) return false;

    m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
        [nodeID](const Connection& c) {
            return c.fromNode == nodeID || c.toNode == nodeID;
        }), m_connections.end());
    m_nodes.erase(it);
    return true;
}

bool ShaderGraph::connect(uint32_t fromNode, int fromPin, uint32_t toNode, int toPin) {
    auto* from = getNode(fromNode);
    auto* to   = getNode(toNode);
    if (!from || !to) return false;
    if (fromPin < 0 || fromPin >= static_cast<int>(from->outputs().size())) return false;
    if (toPin < 0 || toPin >= static_cast<int>(to->inputs().size())) return false;

    if (!canConnect(from->outputs()[fromPin].type, to->inputs()[toPin].type))
        return false;

    for (auto& c : m_connections) {
        if (c.toNode == toNode && c.toPin == toPin) {
            c.fromNode = fromNode;
            c.fromPin = fromPin;
            return true;
        }
    }

    m_connections.push_back({fromNode, fromPin, toNode, toPin});
    return true;
}

bool ShaderGraph::disconnect(uint32_t fromNode, int fromPin) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [fromNode, fromPin](const Connection& c) {
            return c.fromNode == fromNode && c.fromPin == fromPin;
        });
    if (it == m_connections.end()) return false;
    m_connections.erase(it);
    return true;
}

void ShaderGraph::clear() {
    m_nodes.clear();
    m_connections.clear();
    m_nextID = 1;
}

ShaderNode* ShaderGraph::getNode(uint32_t nodeID) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
        [nodeID](const auto& n) { return n->id() == nodeID; });
    return it != m_nodes.end() ? it->get() : nullptr;
}

const ShaderNode* ShaderGraph::getNode(uint32_t nodeID) const {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
        [nodeID](const auto& n) { return n->id() == nodeID; });
    return it != m_nodes.end() ? it->get() : nullptr;
}

void ShaderGraph::getNodeConnections(uint32_t nodeID,
    std::vector<std::pair<int, int>>& inputs,
    std::vector<std::pair<int, int>>& outputs) const
{
    for (const auto& c : m_connections) {
        if (c.toNode == nodeID)   inputs.push_back({static_cast<int>(c.fromNode), c.fromPin});
        if (c.fromNode == nodeID) outputs.push_back({static_cast<int>(c.toNode), c.toPin});
    }
}

bool ShaderGraph::canConnect(PinType fromType, PinType toType) const {
    if (fromType == toType) return true;
    if (fromType == PinType::Float) return true;
    return false;
}

void ShaderGraph::topologicalSort(std::vector<uint32_t>& sortedIDs) const {
    std::unordered_map<uint32_t, int> inDegree;
    for (const auto& n : m_nodes) inDegree[n->id()] = 0;
    for (const auto& c : m_connections) inDegree[c.toNode]++;

    std::queue<uint32_t> q;
    for (const auto& pair : inDegree) {
        if (pair.second == 0) q.push(pair.first);
    }

    while (!q.empty()) {
        uint32_t id = q.front(); q.pop();
        sortedIDs.push_back(id);
        for (const auto& c : m_connections) {
            if (c.fromNode == id) {
                inDegree[c.toNode]--;
                if (inDegree[c.toNode] == 0) q.push(c.toNode);
            }
        }
    }
}

std::string ShaderGraph::compileGLSL() const {
    std::vector<uint32_t> sorted;
    topologicalSort(sorted);

    std::string header =
        "#version 460 core\n\n"
        "uniform float u_time;\n"
        "uniform sampler2D u_texture;\n"
        "in vec2 texCoord;\n"
        "in vec3 v_position;\n"
        "out vec4 fragColor;\n\n";

    std::string body;
    std::string outputVar;

    // Gather input vars for each node from connections
    std::unordered_map<uint32_t, std::vector<std::string>> nodeInputVars;

    // For each node in topological order, collect which connected vars feed its inputs
    for (uint32_t id : sorted) {
        std::vector<std::string> inputVars;
        const auto* node = getNode(id);
        if (!node) continue;

        // Initialize with empty strings (defaults used in generateCode)
        for (size_t i = 0; i < node->inputs().size(); i++) {
            (void)i;
            inputVars.push_back("");
        }

        // Fill in actual connected vars
        for (const auto& c : m_connections) {
            if (c.toNode == id && c.toPin >= 0 && c.toPin < static_cast<int>(inputVars.size())) {
                inputVars[c.toPin] = fmt::format("var_{}", c.fromNode);
            }
        }

        std::string code = node->generateCode(inputVars);
        body += code + "\n";

        if (node->type() == NodeType::OutputPBR) {
            outputVar = fmt::format("var_{}", id);
        }
    }

    std::string result = header;
    result += "void main() {\n";

    if (body.empty()) {
        result += "    fragColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);\n";
    } else {
        result += body;
        if (!outputVar.empty()) {
            result += fmt::format("    fragColor = {};\n", outputVar);
        } else {
            result += "    fragColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);\n";
        }
    }

    result += "}\n";
    return result;
}

std::string ShaderGraph::compileHLSL() const {
    // HLSL generation follows same topological pattern
    // For the MVP, delegate to GLSL
    return compileGLSL();
}

} // namespace Caffeine::Editor
