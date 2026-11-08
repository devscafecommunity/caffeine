#include "script/nodes/NodeRoundTrip.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace Caffeine::Script::Nodes {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string endpoint(const std::string& nodeId, const char* pin) { return nodeId + "." + pin; }

void addEdge(NodeGraph& graph, const std::string& from, const std::string& to) {
    for (const auto& e : graph.edges) {
        if (e.from == from && e.to == to) return;
    }
    graph.edges.push_back({from, to});
}

GraphNode* ensureNode(NodeGraph& graph, const std::string& id, NodeKind kind) {
    if (GraphNode* existing = graph.findNode(id)) return existing;
    GraphNode node;
    node.id = id;
    node.kind = kind;
    graph.nodes.push_back(node);
    return graph.findNode(id);
}

class DydImporter {
public:
    explicit DydImporter(const std::string& src) : m_src(src) {}

    bool run(const std::string& scriptFileName, std::string& err) {
        m_graph = NodeGraph{};
        m_graph.version = 1;
        m_graph.scriptFileName = scriptFileName;

        skipWs();
        std::string onUpdateId;
        if (!readNodeMarker(onUpdateId)) {
            err = "missing OnUpdate @node marker";
            return false;
        }
        ensureNode(m_graph, onUpdateId, NodeKind::OnUpdate);

        if (!consume("function")) return fail(err, "expected function onUpdate");
        if (!consume("onUpdate")) return fail(err, "expected onUpdate");
        if (!consume("(")) return fail(err, "expected (");
        if (!consume("entity")) return fail(err, "expected entity");
        if (!consume(",")) return fail(err, "expected ,");
        if (!consume("dt")) return fail(err, "expected dt");
        if (!consume(")")) return fail(err, "expected )");
        if (!consume("{")) return fail(err, "expected {");

        std::string execOut = endpoint(onUpdateId, "out");
        if (!parseStatementList(execOut, execOut, err)) return false;

        if (!consume("}")) return fail(err, "expected closing }");
        skipWs();
        if (m_pos < m_src.size()) return fail(err, "unexpected trailing input");
        return true;
    }

    NodeGraph graph() const { return m_graph; }

private:
    const std::string& m_src;
    size_t m_pos = 0;
    NodeGraph m_graph;
    int m_constSerial = 0;
    std::string m_getPositionId;
    std::string m_deltaTimeId;
    std::unordered_map<std::string, std::string> m_exprToConst;

    static bool fail(std::string& err, const char* msg) {
        err = msg;
        return false;
    }

    char peek() const { return m_pos < m_src.size() ? m_src[m_pos] : '\0'; }

    void skipWs() {
        while (m_pos < m_src.size()) {
            if (std::isspace(static_cast<unsigned char>(m_src[m_pos]))) {
                ++m_pos;
                continue;
            }
            if (m_pos + 1 < m_src.size() && m_src[m_pos] == '/' && m_src[m_pos + 1] == '/') {
                if (m_src.compare(m_pos, 8, "// @node") == 0) break;
                while (m_pos < m_src.size() && m_src[m_pos] != '\n') ++m_pos;
                continue;
            }
            break;
        }
    }

    bool consume(const char* lit) {
        skipWs();
        const size_t len = std::strlen(lit);
        if (m_src.compare(m_pos, len, lit) != 0) return false;
        m_pos += len;
        return true;
    }

    bool readNodeMarker(std::string& nodeId) {
        skipWs();
        if (!consume("//")) return false;
        skipWs();
        if (!consume("@node")) return false;
        skipWs();
        nodeId.clear();
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos];
            if (std::isspace(static_cast<unsigned char>(c))) break;
            nodeId.push_back(c);
            ++m_pos;
        }
        return !nodeId.empty();
    }

    bool parseStatementList(const std::string& execIn, std::string& execOut, std::string& err) {
        execOut = execIn;
        while (true) {
            skipWs();
            if (peek() == '}') return true;

            const size_t mark = m_pos;
            std::string nodeId;
            if (!readNodeMarker(nodeId)) {
                m_pos = mark;
                return true;
            }

            if (consume("caffeine_set_position")) {
                if (!parseVectorCall(nodeId, NodeKind::SetPosition, execOut, err)) return false;
                continue;
            }

            if (consume("caffeine_translate")) {
                if (!parseVectorCall(nodeId, NodeKind::Translate, execOut, err)) return false;
                continue;
            }

            if (consume("if")) {
                if (!parseBranch(nodeId, execOut, err)) return false;
                continue;
            }

            return fail(err, "unsupported statement after @node marker");
        }
    }

    bool parseVectorCall(const std::string& nodeId, NodeKind kind, std::string& execOut,
                         std::string& err) {
        ensureNode(m_graph, nodeId, kind);
        if (execOut.empty()) return fail(err, "vector node without exec input");
        addEdge(m_graph, execOut, endpoint(nodeId, "in"));

        if (!consume("(")) return fail(err, "expected ( after vector call");
        if (!consume("entity")) return fail(err, "expected entity");
        if (!consume(",")) return fail(err, "expected ,");
        std::string xExpr, yExpr, zExpr;
        if (!readCommaSeparatedArg(xExpr, err)) return false;
        if (!readCommaSeparatedArg(yExpr, err)) return false;
        if (!readCommaSeparatedArg(zExpr, err)) return false;
        if (!consume(")")) return fail(err, "expected )");
        if (!consume(";")) return fail(err, "expected ;");

        if (!wireDataExpr(xExpr, nodeId, "x", err)) return false;
        if (!wireDataExpr(yExpr, nodeId, "y", err)) return false;
        if (!wireDataExpr(zExpr, nodeId, "z", err)) return false;

        execOut = endpoint(nodeId, "out");
        return true;
    }

    bool parseBranch(const std::string& nodeId, std::string& execOut, std::string& err) {
        ensureNode(m_graph, nodeId, NodeKind::Branch);
        if (execOut.empty()) return fail(err, "Branch without exec input");
        addEdge(m_graph, execOut, endpoint(nodeId, "in"));

        if (!consume("(")) return fail(err, "expected ( after if");
        const size_t condStart = m_pos;
        int depth = 1;
        while (m_pos < m_src.size() && depth > 0) {
            const char c = m_src[m_pos++];
            if (c == '(') ++depth;
            else if (c == ')') --depth;
        }
        if (depth != 0) return fail(err, "unbalanced if condition");
        const std::string cond = trim(m_src.substr(condStart, m_pos - condStart - 1));
        if (!wireDataExpr(cond, nodeId, "condition", err)) return false;

        if (!consume("{")) return fail(err, "expected { after if condition");
        std::string thenOut;
        if (!parseStatementList(endpoint(nodeId, "then"), thenOut, err)) return false;
        if (!consume("}")) return fail(err, "expected } after then branch");
        if (!consume("else")) return fail(err, "expected else");
        if (!consume("{")) return fail(err, "expected { after else");
        std::string elseOut;
        if (!parseStatementList(endpoint(nodeId, "else"), elseOut, err)) return false;
        if (!consume("}")) return fail(err, "expected } after else branch");

        execOut = endpoint(nodeId, "out");
        return true;
    }

    bool readCommaSeparatedArg(std::string& out, std::string& err) {
        skipWs();
        const size_t start = m_pos;
        int depth = 0;
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos];
            if (c == '(') ++depth;
            else if (c == ')') {
                if (depth == 0) break;
                --depth;
            } else if (c == ',' && depth == 0) {
                out = trim(m_src.substr(start, m_pos - start));
                ++m_pos;
                return true;
            }
            ++m_pos;
        }
        if (depth != 0) return fail(err, "unbalanced parentheses in argument");
        out = trim(m_src.substr(start, m_pos - start));
        return !out.empty();
    }

    const std::string& getPositionNode() {
        if (m_getPositionId.empty()) {
            m_getPositionId = "import_g" + std::to_string(m_constSerial++);
            ensureNode(m_graph, m_getPositionId, NodeKind::GetPosition);
        }
        return m_getPositionId;
    }

    bool wireDataExpr(const std::string& expr, const std::string& toNode, const char* pin,
                      std::string& err) {
        const std::string e = trim(expr);
        if (e == "caffeine_get_position_x(entity)") {
            addEdge(m_graph, endpoint(getPositionNode(), "x"), endpoint(toNode, pin));
            return true;
        }
        if (e == "caffeine_get_position_y(entity)") {
            addEdge(m_graph, endpoint(getPositionNode(), "y"), endpoint(toNode, pin));
            return true;
        }
        if (e == "caffeine_get_position_z(entity)") {
            addEdge(m_graph, endpoint(getPositionNode(), "z"), endpoint(toNode, pin));
            return true;
        }

        if (e == "dt") {
            if (m_deltaTimeId.empty()) {
                m_deltaTimeId = "import_dt" + std::to_string(m_constSerial++);
                ensureNode(m_graph, m_deltaTimeId, NodeKind::DeltaTime);
            }
            addEdge(m_graph, endpoint(m_deltaTimeId, "value"), endpoint(toNode, pin));
            return true;
        }

        auto it = m_exprToConst.find(e);
        if (it != m_exprToConst.end()) {
            addEdge(m_graph, endpoint(it->second, "value"), endpoint(toNode, pin));
            return true;
        }

        GraphNode* constant = nullptr;
        std::string constId = "import_c" + std::to_string(m_constSerial++);
        if (e == "true" || e == "false") {
            constant = ensureNode(m_graph, constId, NodeKind::Constant);
            constant->constantIsBool = true;
            constant->boolValue = e == "true";
        } else {
            char* end = nullptr;
            const double v = std::strtod(e.c_str(), &end);
            if (end == e.c_str() || !trim(std::string(end)).empty()) {
                return fail(err, "unsupported data expression");
            }
            constant = ensureNode(m_graph, constId, NodeKind::Constant);
            constant->numberValue = v;
        }
        m_exprToConst.emplace(e, constId);
        addEdge(m_graph, endpoint(constId, "value"), endpoint(toNode, pin));
        return true;
    }
};

} // namespace

bool dydSourceHasNodeMarkers(const std::string& dydSource) {
    return dydSource.find("// @node ") != std::string::npos;
}

void mergeGraphLayout(NodeGraph& graph, const NodeGraph& layoutHint) {
    f32 orphanY = 200.0f;
    for (auto& node : graph.nodes) {
        if (const GraphNode* hint = layoutHint.findNode(node.id)) {
            node.x = hint->x;
            node.y = hint->y;
            continue;
        }
        if (startsWith(node.id, "import_")) {
            node.x = 40.0f;
            node.y = orphanY;
            orphanY += 48.0f;
        }
    }
}

ImportResult importFromDyd(const std::string& dydSource, const std::string& scriptFileName,
                           const NodeGraph* layoutHint) {
    ImportResult result;
    if (!dydSourceHasNodeMarkers(dydSource)) {
        result.status = ImportStatus::NoMarkers;
        result.message = "No // @node markers in script.";
        return result;
    }

    DydImporter importer(dydSource);
    std::string err;
    if (!importer.run(scriptFileName, err)) {
        result.status = ImportStatus::Failed;
        result.error = err;
        result.message = "Could not rebuild graph from .dyd: " + err;
        return result;
    }

    result.graph = importer.graph();
    if (layoutHint) {
        mergeGraphLayout(result.graph, *layoutHint);
    }
    result.status = ImportStatus::Success;
    result.message = "Canvas rebuilt from // @node markers.";
    return result;
}

} // namespace Caffeine::Script::Nodes
