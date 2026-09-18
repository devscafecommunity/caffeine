#include "editor/ShaderGraph.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdarg>

namespace Caffeine::Editor {

static std::string str(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

uint32_t ShaderGraph::addNode(NodeType type) {
    return addNodeWithId(type, m_nextID);
}

uint32_t ShaderGraph::addNodeWithId(NodeType type, uint32_t id) {
    auto node = createNode(type, id);
    if (!node) return 0;
    m_nodes.push_back(std::move(node));
    m_nextID = std::max(m_nextID, id + 1);
    return id;
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

    for (uint32_t id : sorted) {
        std::vector<std::string> inputVars;
        const auto* node = getNode(id);
        if (!node) continue;

        for (size_t i = 0; i < node->inputs().size(); i++) {
            (void)i;
            inputVars.push_back("");
        }

        for (const auto& c : m_connections) {
            if (c.toNode == id && c.toPin >= 0 && c.toPin < static_cast<int>(inputVars.size())) {
                inputVars[c.toPin] = str("var_%u", c.fromNode);
            }
        }

        std::string code = node->generateCode(inputVars);
        body += code + "\n";

        if (node->type() == NodeType::OutputPBR) {
            outputVar = str("var_%u", id);
        }
    }

    std::string result = header;
    result += "void main() {\n";

    if (body.empty()) {
        result += "    fragColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);\n";
    } else {
        result += body;
        if (!outputVar.empty()) {
            result += str("    fragColor = %s;\n", outputVar.c_str());
        } else {
            result += "    fragColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);\n";
        }
    }

    result += "}\n";
    return result;
}

std::string ShaderGraph::compileHLSL() const {
    return compileGLSL();
}

namespace {

enum class ValueKind : u8 { None, Float, Vec4 };

struct GraphValue {
    ValueKind kind = ValueKind::None;
    f32 f = 0.0f;
    Vec4 v4{0.0f, 0.0f, 0.0f, 0.0f};
};

GraphValue defaultForPin(PinType type) {
    GraphValue v;
    if (type == PinType::Vec4) {
        v.kind = ValueKind::Vec4;
        v.v4 = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        v.kind = ValueKind::Float;
        v.f = (type == PinType::Float) ? 1.0f : 0.5f;
    }
    return v;
}

f32 asFloat(const GraphValue& v, f32 fallback = 0.0f) {
    if (v.kind == ValueKind::Float) return v.f;
    if (v.kind == ValueKind::Vec4) return (v.v4.x + v.v4.y + v.v4.z) / 3.0f;
    return fallback;
}

Vec4 asVec4(const GraphValue& v, const Vec4& fallback = Vec4(1.0f, 1.0f, 1.0f, 1.0f)) {
    if (v.kind == ValueKind::Vec4) return v.v4;
    if (v.kind == ValueKind::Float) return Vec4(v.f, v.f, v.f, 1.0f);
    return fallback;
}

}  // namespace

EvaluatedMaterial ShaderGraph::evaluateMaterial(f32 time) const {
    EvaluatedMaterial result;
    if (m_nodes.empty()) return result;

    std::vector<uint32_t> sorted;
    topologicalSort(sorted);

    std::unordered_map<uint32_t, GraphValue> values;
    auto inputValue = [&](uint32_t nodeId, int pin, PinType pinType) -> GraphValue {
        for (const auto& c : m_connections) {
            if (c.toNode == nodeId && c.toPin == pin) {
                auto it = values.find(c.fromNode);
                if (it != values.end()) return it->second;
            }
        }
        return defaultForPin(pinType);
    };

    for (uint32_t id : sorted) {
        const ShaderNode* node = getNode(id);
        if (!node) continue;

        GraphValue out;
        switch (node->type()) {
            case NodeType::ColorConstant: {
                const auto* n = static_cast<const ColorConstantNode*>(node);
                out.kind = ValueKind::Vec4;
                out.v4 = Vec4(n->color[0], n->color[1], n->color[2], n->color[3]);
                break;
            }
            case NodeType::FloatConstant: {
                const auto* n = static_cast<const FloatConstantNode*>(node);
                out.kind = ValueKind::Float;
                out.f = n->value;
                break;
            }
            case NodeType::Multiply: {
                const f32 a = asFloat(inputValue(id, 0, PinType::Float), 1.0f);
                const f32 b = asFloat(inputValue(id, 1, PinType::Float), 1.0f);
                out.kind = ValueKind::Float;
                out.f = a * b;
                break;
            }
            case NodeType::Add: {
                const f32 a = asFloat(inputValue(id, 0, PinType::Float), 0.0f);
                const f32 b = asFloat(inputValue(id, 1, PinType::Float), 0.0f);
                out.kind = ValueKind::Float;
                out.f = a + b;
                break;
            }
            case NodeType::Lerp: {
                const Vec4 from = asVec4(inputValue(id, 0, PinType::Vec4), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
                const Vec4 to = asVec4(inputValue(id, 1, PinType::Vec4), Vec4(1.0f, 1.0f, 1.0f, 1.0f));
                const f32 t = std::clamp(asFloat(inputValue(id, 2, PinType::Float), 0.5f), 0.0f, 1.0f);
                out.kind = ValueKind::Vec4;
                out.v4 = from * (1.0f - t) + to * t;
                break;
            }
            case NodeType::Time: {
                out.kind = ValueKind::Float;
                out.f = time;
                break;
            }
            case NodeType::TextureSample: {
                out.kind = ValueKind::Vec4;
                out.v4 = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
                break;
            }
            case NodeType::VertexPosition: {
                out.kind = ValueKind::Vec4;
                out.v4 = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
                break;
            }
            case NodeType::OutputPBR: {
                result.albedo = asVec4(inputValue(id, 0, PinType::Vec4));
                result.metallic = std::clamp(asFloat(inputValue(id, 2, PinType::Float), 0.0f), 0.0f, 1.0f);
                result.roughness = std::clamp(asFloat(inputValue(id, 3, PinType::Float), 0.5f), 0.0f, 1.0f);
                result.valid = true;
                out.kind = ValueKind::Vec4;
                out.v4 = result.albedo;
                break;
            }
        }
        values[id] = out;
    }

    if (!result.valid) {
        for (const auto& n : m_nodes) {
            if (n->type() == NodeType::ColorConstant) {
                const auto* colorNode = static_cast<const ColorConstantNode*>(n.get());
                result.albedo = Vec4(colorNode->color[0], colorNode->color[1],
                                     colorNode->color[2], colorNode->color[3]);
                result.valid = true;
                break;
            }
        }
    }
    return result;
}

} // namespace Caffeine::Editor
