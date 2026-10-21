#include "script/nodes/NodeGraphIO.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Caffeine::Script::Nodes {

namespace {

usize skipWs(const std::string& json, usize pos) {
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    return pos;
}

bool readQuoted(const std::string& json, usize& pos, std::string& out) {
    pos = skipWs(json, pos);
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') return true;
        if (c == '\\' && pos < json.size()) {
            out.push_back(json[pos++]);
            continue;
        }
        out.push_back(c);
    }
    return false;
}

void skipValue(const std::string& json, usize& pos) {
    pos = skipWs(json, pos);
    if (pos >= json.size()) return;
    if (json[pos] == '"') {
        std::string tmp;
        readQuoted(json, pos, tmp);
        return;
    }
    if (json[pos] == '{' || json[pos] == '[') {
        const char open = json[pos];
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        while (pos < json.size()) {
            if (json[pos] == open) ++depth;
            else if (json[pos] == close) {
                --depth;
                ++pos;
                if (depth == 0) return;
                continue;
            }
            ++pos;
        }
        return;
    }
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') ++pos;
}

bool readNumberField(const std::string& object, const std::string& key, double& out) {
    usize pos = 0;
    while (pos < object.size()) {
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] == '}') break;
        std::string found;
        if (!readQuoted(object, pos, found)) break;
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] != ':') break;
        ++pos;
        pos = skipWs(object, pos);
        if (found == key) {
            const usize start = pos;
            while (pos < object.size() && object[pos] != ',' && object[pos] != '}') ++pos;
            const std::string num = object.substr(start, pos - start);
            try {
                out = std::stod(num);
                return true;
            } catch (...) {
                return false;
            }
        }
        skipValue(object, pos);
        pos = skipWs(object, pos);
        if (pos < object.size() && object[pos] == ',') ++pos;
    }
    return false;
}

bool readStringField(const std::string& object, const std::string& key, std::string& out) {
    usize pos = 0;
    while (pos < object.size()) {
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] == '}') break;
        std::string found;
        if (!readQuoted(object, pos, found)) break;
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] != ':') break;
        ++pos;
        if (found == key) return readQuoted(object, pos, out);
        skipValue(object, pos);
        pos = skipWs(object, pos);
        if (pos < object.size() && object[pos] == ',') ++pos;
    }
    return false;
}

bool readBoolField(const std::string& object, const std::string& key, bool& out) {
    usize pos = 0;
    while (pos < object.size()) {
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] == '}') break;
        std::string found;
        if (!readQuoted(object, pos, found)) break;
        pos = skipWs(object, pos);
        if (pos >= object.size() || object[pos] != ':') break;
        ++pos;
        pos = skipWs(object, pos);
        if (found == key) {
            if (object.compare(pos, 4, "true") == 0) {
                out = true;
                return true;
            }
            if (object.compare(pos, 5, "false") == 0) {
                out = false;
                return true;
            }
            return false;
        }
        skipValue(object, pos);
        pos = skipWs(object, pos);
        if (pos < object.size() && object[pos] == ',') ++pos;
    }
    return false;
}

bool extractTopLevelArray(const std::string& json, const std::string& key, std::string& outArray) {
    const std::string needle = "\"" + key + "\"";
    const usize keyPos = json.find(needle);
    if (keyPos == std::string::npos) return false;
    usize pos = keyPos + needle.size();
    pos = skipWs(json, pos);
    if (pos >= json.size() || json[pos] != ':') return false;
    ++pos;
    pos = skipWs(json, pos);
    if (pos >= json.size() || json[pos] != '[') return false;
    const usize start = pos;
    int depth = 0;
    while (pos < json.size()) {
        if (json[pos] == '[') ++depth;
        else if (json[pos] == ']') {
            --depth;
            if (depth == 0) {
                outArray = json.substr(start, pos - start + 1);
                return true;
            }
        }
        ++pos;
    }
    return false;
}

bool splitTopLevelObjects(const std::string& arrayJson, std::vector<std::string>& objects) {
    usize pos = skipWs(arrayJson, 0);
    if (pos >= arrayJson.size() || arrayJson[pos] != '[') return false;
    ++pos;
    while (pos < arrayJson.size()) {
        pos = skipWs(arrayJson, pos);
        if (pos < arrayJson.size() && arrayJson[pos] == ']') return true;
        if (arrayJson[pos] != '{') return false;
        const usize start = pos;
        int depth = 0;
        while (pos < arrayJson.size()) {
            if (arrayJson[pos] == '{') ++depth;
            else if (arrayJson[pos] == '}') {
                --depth;
                if (depth == 0) {
                    objects.push_back(arrayJson.substr(start, pos - start + 1));
                    ++pos;
                    break;
                }
            }
            ++pos;
        }
        pos = skipWs(arrayJson, pos);
        if (pos < arrayJson.size() && arrayJson[pos] == ',') ++pos;
    }
    return true;
}

bool parseNodeObject(const std::string& object, GraphNode& out) {
    std::string typeName;
    if (!readStringField(object, "id", out.id) || !readStringField(object, "type", typeName)) return false;
    if (!nodeKindFromTypeName(typeName, out.kind)) return false;
    double x = 0;
    double y = 0;
    if (readNumberField(object, "x", x)) out.x = static_cast<f32>(x);
    if (readNumberField(object, "y", y)) out.y = static_cast<f32>(y);
    std::string valueKind;
    if (readStringField(object, "valueKind", valueKind) && valueKind == "bool") {
        out.constantIsBool = true;
        readBoolField(object, "value", out.boolValue);
    } else if (out.kind == NodeKind::Constant) {
        readNumberField(object, "value", out.numberValue);
    }
    return true;
}

bool parseEdgeObject(const std::string& object, GraphEdge& out) {
    return readStringField(object, "from", out.from) && readStringField(object, "to", out.to);
}

} // namespace

std::filesystem::path nodeJsonPathForScript(const std::filesystem::path& scriptPath) {
    return scriptPath.parent_path() / (scriptPath.stem().string() + ".node.json");
}

NodeGraph defaultGraphForScript(const std::filesystem::path& scriptPath) {
    NodeGraph graph;
    graph.version = 1;
    graph.scriptFileName = scriptPath.filename().string();
    graph.nodes.push_back({"n1", NodeKind::OnUpdate, 40.0f, 40.0f});
    return graph;
}

LoadResult loadFromJsonString(const std::string& json) {
    LoadResult result;
    NodeGraph graph;
    double version = 1;
    readNumberField(json, "version", version);
    graph.version = static_cast<int>(version);
    readStringField(json, "script", graph.scriptFileName);

    std::string nodesArray;
    if (extractTopLevelArray(json, "nodes", nodesArray)) {
        std::vector<std::string> objects;
        if (!splitTopLevelObjects(nodesArray, objects)) {
            result.error = "invalid nodes array";
            return result;
        }
        for (const auto& object : objects) {
            GraphNode node;
            if (!parseNodeObject(object, node)) {
                result.error = "failed to parse node entry";
                return result;
            }
            graph.nodes.push_back(std::move(node));
        }
    }

    std::string edgesArray;
    if (extractTopLevelArray(json, "edges", edgesArray)) {
        std::vector<std::string> objects;
        if (!splitTopLevelObjects(edgesArray, objects)) {
            result.error = "invalid edges array";
            return result;
        }
        for (const auto& object : objects) {
            GraphEdge edge;
            if (!parseEdgeObject(object, edge)) {
                result.error = "failed to parse edge entry";
                return result;
            }
            graph.edges.push_back(std::move(edge));
        }
    }

    result.graph = std::move(graph);
    result.ok = true;
    return result;
}

LoadResult loadFromJsonFile(const std::filesystem::path& path) {
    LoadResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "could not open " + path.string();
        return result;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    result = loadFromJsonString(buffer.str());
    return result;
}

} // namespace Caffeine::Script::Nodes
