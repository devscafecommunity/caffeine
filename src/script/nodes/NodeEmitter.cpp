#include "script/nodes/NodeEmitter.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Caffeine::Script::Nodes {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

const GraphEdge* findEdgeTo(const NodeGraph& graph, const std::string& nodeId, const std::string& pin) {
    const std::string target = nodeId + "." + pin;
    for (const auto& edge : graph.edges) {
        if (edge.to == target) return &edge;
    }
    return nullptr;
}

const GraphEdge* findExecEdgeFrom(const NodeGraph& graph, const std::string& nodeId, const std::string& pin) {
    const std::string source = nodeId + "." + pin;
    for (const auto& edge : graph.edges) {
        if (edge.from == source) return &edge;
    }
    return nullptr;
}

void sortGraphForDeterminism(NodeGraph& graph) {
    std::sort(graph.nodes.begin(), graph.nodes.end(),
              [](const GraphNode& a, const GraphNode& b) { return a.id < b.id; });
    std::sort(graph.edges.begin(), graph.edges.end(), [](const GraphEdge& a, const GraphEdge& b) {
        if (a.from != b.from) return a.from < b.from;
        return a.to < b.to;
    });
}

std::string formatNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

class Codegen {
public:
    explicit Codegen(const NodeGraph& graph) : m_graph(graph) {}

    EmitResult run() {
        EmitResult result;
        const GraphNode* onUpdate = nullptr;
        for (const auto& node : m_graph.nodes) {
            if (node.kind == NodeKind::OnUpdate) {
                if (onUpdate) {
                    result.error = "graph must contain exactly one OnUpdate node";
                    return result;
                }
                onUpdate = &node;
            }
        }
        if (!onUpdate) {
            result.error = "graph is missing an OnUpdate node";
            return result;
        }
        if (m_graph.scriptFileName.empty()) {
            result.error = "script file name is required";
            return result;
        }

        std::ostringstream dyd;
        dyd << "// @node " << onUpdate->id << "\n";
        dyd << "function onUpdate(entity, dt) {\n";

        const GraphEdge* entry = findExecEdgeFrom(m_graph, onUpdate->id, "out");
        if (entry) {
            PinRef next{};
            if (!parsePinRef(entry->to, next)) {
                result.error = "invalid exec edge from OnUpdate";
                return result;
            }
            std::string bodyErr;
            if (!emitExecChain(next.nodeId, dyd, 1, bodyErr)) {
                result.error = bodyErr;
                return result;
            }
        }

        dyd << "}\n";

        result.dyd = dyd.str();
        result.nodeJson = serializeJson(m_graph);
        result.ok = true;
        return result;
    }

private:
    const NodeGraph& m_graph;
    std::unordered_map<std::string, std::string> m_dataExprCache;

    std::string indent(int depth) const {
        return std::string(static_cast<size_t>(depth) * 2, ' ');
    }

    void line(std::ostringstream& out, int depth, const std::string& text) {
        out << indent(depth) << text << "\n";
    }

    const GraphNode* node(const std::string& id) const { return m_graph.findNode(id); }

    bool emitExecChain(const std::string& nodeId, std::ostringstream& out, int depth, std::string& err) {
        const GraphNode* n = node(nodeId);
        if (!n) {
            err = "unknown node in exec chain: " + nodeId;
            return false;
        }

        switch (n->kind) {
        case NodeKind::SetPosition: {
            line(out, depth, "// @node " + n->id);
            std::string xExpr, yExpr, zExpr;
            if (!resolveDataInput(n->id, "x", xExpr, err)) return false;
            if (!resolveDataInput(n->id, "y", yExpr, err)) return false;
            if (!resolveDataInput(n->id, "z", zExpr, err)) return false;
            line(out, depth,
                 "caffeine_set_position(entity, " + xExpr + ", " + yExpr + ", " + zExpr + ");");

            if (const GraphEdge* next = findExecEdgeFrom(m_graph, n->id, "out")) {
                PinRef ref{};
                if (!parsePinRef(next->to, ref)) {
                    err = "invalid exec edge from SetPosition";
                    return false;
                }
                return emitExecChain(ref.nodeId, out, depth, err);
            }
            return true;
        }
        case NodeKind::Translate: {
            line(out, depth, "// @node " + n->id);
            std::string xExpr, yExpr, zExpr;
            if (!resolveDataInput(n->id, "x", xExpr, err)) return false;
            if (!resolveDataInput(n->id, "y", yExpr, err)) return false;
            if (!resolveDataInput(n->id, "z", zExpr, err)) return false;
            line(out, depth,
                 "caffeine_translate(entity, " + xExpr + ", " + yExpr + ", " + zExpr + ");");

            if (const GraphEdge* next = findExecEdgeFrom(m_graph, n->id, "out")) {
                PinRef ref{};
                if (!parsePinRef(next->to, ref)) {
                    err = "invalid exec edge from Translate";
                    return false;
                }
                return emitExecChain(ref.nodeId, out, depth, err);
            }
            return true;
        }
        case NodeKind::Branch: {
            line(out, depth, "// @node " + n->id);
            std::string cond;
            if (!resolveDataInput(n->id, "condition", cond, err)) return false;
            line(out, depth, "if (" + cond + ") {");
            if (const GraphEdge* thenEdge = findExecEdgeFrom(m_graph, n->id, "then")) {
                PinRef ref{};
                if (!parsePinRef(thenEdge->to, ref)) {
                    err = "invalid exec edge from Branch.then";
                    return false;
                }
                if (!emitExecChain(ref.nodeId, out, depth + 1, err)) return false;
            }
            line(out, depth, "} else {");
            if (const GraphEdge* elseEdge = findExecEdgeFrom(m_graph, n->id, "else")) {
                PinRef ref{};
                if (!parsePinRef(elseEdge->to, ref)) {
                    err = "invalid exec edge from Branch.else";
                    return false;
                }
                if (!emitExecChain(ref.nodeId, out, depth + 1, err)) return false;
            }
            line(out, depth, "}");

            if (const GraphEdge* next = findExecEdgeFrom(m_graph, n->id, "out")) {
                PinRef ref{};
                if (!parsePinRef(next->to, ref)) {
                    err = "invalid exec edge from Branch.out";
                    return false;
                }
                return emitExecChain(ref.nodeId, out, depth, err);
            }
            return true;
        }
        case NodeKind::OnUpdate:
            err = "OnUpdate cannot appear inside exec chain";
            return false;
        case NodeKind::GetPosition:
        case NodeKind::DeltaTime:
        case NodeKind::Constant:
            err = "data-only node in exec chain: " + nodeId;
            return false;
        }
        err = "unsupported node in exec chain";
        return false;
    }

    bool resolveDataInput(const std::string& nodeId, const std::string& pin, std::string& outExpr,
                          std::string& err) {
        const std::string key = nodeId + "." + pin;
        if (const GraphEdge* edge = findEdgeTo(m_graph, nodeId, pin)) {
            PinRef src{};
            if (!parsePinRef(edge->from, src)) {
                err = "invalid data edge to " + key;
                return false;
            }
            return resolveDataOutput(src.nodeId, src.pin, outExpr, err);
        }

        const GraphNode* n = node(nodeId);
        if (n) {
            if (pin == "x") {
                outExpr = formatNumber(n->inputX);
                return true;
            }
            if (pin == "y") {
                outExpr = formatNumber(n->inputY);
                return true;
            }
            if (pin == "z") {
                outExpr = formatNumber(n->inputZ);
                return true;
            }
            if (pin == "condition") {
                outExpr = n->inputCondition ? "true" : "false";
                return true;
            }
        }
        outExpr = "0";
        return true;
    }

    bool resolveDataOutput(const std::string& nodeId, const std::string& pin, std::string& outExpr,
                           std::string& err) {
        const std::string cacheKey = nodeId + "." + pin;
        const auto cached = m_dataExprCache.find(cacheKey);
        if (cached != m_dataExprCache.end()) {
            outExpr = cached->second;
            return true;
        }

        const GraphNode* n = node(nodeId);
        if (!n) {
            err = "unknown data node: " + nodeId;
            return false;
        }

        switch (n->kind) {
        case NodeKind::Constant:
            if (pin != "value") {
                err = "Constant only exposes pin 'value'";
                return false;
            }
            outExpr = n->constantIsBool ? (n->boolValue ? "true" : "false")
                                        : formatNumber(n->numberValue);
            m_dataExprCache.emplace(cacheKey, outExpr);
            return true;
        case NodeKind::DeltaTime:
            if (pin != "value") {
                err = "DeltaTime only exposes pin 'value'";
                return false;
            }
            outExpr = "dt";
            m_dataExprCache.emplace(cacheKey, outExpr);
            return true;
        case NodeKind::GetPosition:
            if (pin == "x") {
                outExpr = "caffeine_get_position_x(entity)";
            } else if (pin == "y") {
                outExpr = "caffeine_get_position_y(entity)";
            } else if (pin == "z") {
                outExpr = "caffeine_get_position_z(entity)";
            } else {
                err = "GetPosition exposes x, y, z pins";
                return false;
            }
            m_dataExprCache.emplace(cacheKey, outExpr);
            return true;
        default:
            err = "node " + nodeId + " is not a data source";
            return false;
        }
    }

    static std::string serializeJson(const NodeGraph& graph) {
        NodeGraph sorted = graph;
        sortGraphForDeterminism(sorted);

        std::ostringstream json;
        json << "{\n";
        json << "  \"version\": " << sorted.version << ",\n";
        json << "  \"script\": \"" << jsonEscape(sorted.scriptFileName) << "\",\n";
        json << "  \"nodes\": [\n";
        for (size_t i = 0; i < sorted.nodes.size(); ++i) {
            const auto& n = sorted.nodes[i];
            json << "    { \"id\": \"" << jsonEscape(n.id) << "\", \"type\": \""
                 << nodeKindToTypeName(n.kind) << "\", \"x\": " << n.x << ", \"y\": " << n.y;
            if (n.kind == NodeKind::Constant) {
                if (n.constantIsBool) {
                    json << ", \"valueKind\": \"bool\", \"value\": " << (n.boolValue ? "true" : "false");
                } else {
                    json << ", \"valueKind\": \"number\", \"value\": " << formatNumber(n.numberValue);
                }
            }
            if (n.kind == NodeKind::SetPosition || n.kind == NodeKind::Translate) {
                json << ", \"inputX\": " << formatNumber(n.inputX)
                     << ", \"inputY\": " << formatNumber(n.inputY)
                     << ", \"inputZ\": " << formatNumber(n.inputZ);
            }
            if (n.kind == NodeKind::Branch) {
                json << ", \"condition\": " << (n.inputCondition ? "true" : "false");
            }
            json << " }";
            if (i + 1 < sorted.nodes.size()) json << ",";
            json << "\n";
        }
        json << "  ],\n";
        json << "  \"edges\": [\n";
        for (size_t i = 0; i < sorted.edges.size(); ++i) {
            const auto& e = sorted.edges[i];
            json << "    { \"from\": \"" << jsonEscape(e.from) << "\", \"to\": \"" << jsonEscape(e.to)
                 << "\" }";
            if (i + 1 < sorted.edges.size()) json << ",";
            json << "\n";
        }
        json << "  ]\n";
        json << "}\n";
        return json.str();
    }
};

} // namespace

EmitResult NodeEmitter::emit(const NodeGraph& graph) {
    Codegen gen(graph);
    return gen.run();
}

bool NodeEmitter::writeSiblingFiles(const std::filesystem::path& scriptPath, const NodeGraph& graph,
                                    std::string* outError) {
    EmitResult emitted = emit(graph);
    if (!emitted.ok) {
        if (outError) *outError = emitted.error;
        return false;
    }

    const std::filesystem::path jsonPath =
        scriptPath.parent_path() / (scriptPath.stem().string() + ".node.json");

    {
        std::ofstream dydOut(scriptPath, std::ios::binary | std::ios::trunc);
        if (!dydOut) {
            if (outError) *outError = "failed to write " + scriptPath.string();
            return false;
        }
        dydOut << emitted.dyd;
    }
    {
        std::ofstream jsonOut(jsonPath, std::ios::binary | std::ios::trunc);
        if (!jsonOut) {
            if (outError) *outError = "failed to write " + jsonPath.string();
            return false;
        }
        jsonOut << emitted.nodeJson;
    }
    return true;
}

} // namespace Caffeine::Script::Nodes
