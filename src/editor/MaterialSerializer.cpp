#include "editor/MaterialSerializer.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

namespace Caffeine::Editor {

namespace {

bool writeLine(std::ostream& out, const std::string& line) {
    out << line << '\n';
    return static_cast<bool>(out);
}

std::string nodeTypeName(NodeType type) {
    switch (type) {
        case NodeType::TextureSample: return "TextureSample";
        case NodeType::ColorConstant: return "Color";
        case NodeType::FloatConstant: return "Float";
        case NodeType::Multiply: return "Multiply";
        case NodeType::Add: return "Add";
        case NodeType::Lerp: return "Lerp";
        case NodeType::Time: return "Time";
        case NodeType::VertexPosition: return "VertexPosition";
        case NodeType::OutputPBR: return "OutputPBR";
    }
    return "Unknown";
}

NodeType nodeTypeFromName(const std::string& name) {
    if (name == "TextureSample") return NodeType::TextureSample;
    if (name == "Color" || name == "ColorConstant") return NodeType::ColorConstant;
    if (name == "Float" || name == "FloatConstant") return NodeType::FloatConstant;
    if (name == "Multiply") return NodeType::Multiply;
    if (name == "Add") return NodeType::Add;
    if (name == "Lerp") return NodeType::Lerp;
    if (name == "Time") return NodeType::Time;
    if (name == "VertexPosition") return NodeType::VertexPosition;
    if (name == "OutputPBR") return NodeType::OutputPBR;
    return NodeType::OutputPBR;
}

}  // namespace

bool MaterialSerializer::save(const std::filesystem::path& path, const MaterialDocument& doc,
                              const ShaderGraph& graph) {
    std::ofstream out(path);
    if (!out.is_open()) return false;

    writeLine(out, "CAFMAT1");
    writeLine(out, "name " + doc.name);
    writeLine(out, "albedo " + std::to_string(doc.properties.albedoColor.r) + " "
                        + std::to_string(doc.properties.albedoColor.g) + " "
                        + std::to_string(doc.properties.albedoColor.b) + " "
                        + std::to_string(doc.properties.albedoColor.a));
    writeLine(out, "roughness " + std::to_string(doc.properties.roughness));
    writeLine(out, "metallic " + std::to_string(doc.properties.metallic));
    writeLine(out, "node_count " + std::to_string(graph.nodeCount()));

    for (const auto& node : graph.nodes()) {
        std::ostringstream line;
        line << "node " << node->id() << ' ' << nodeTypeName(node->type());
        switch (node->type()) {
            case NodeType::ColorConstant: {
                const auto* n = static_cast<const ColorConstantNode*>(node.get());
                line << ' ' << n->color[0] << ' ' << n->color[1] << ' ' << n->color[2] << ' ' << n->color[3];
                break;
            }
            case NodeType::FloatConstant: {
                const auto* n = static_cast<const FloatConstantNode*>(node.get());
                line << ' ' << n->value;
                break;
            }
            case NodeType::TextureSample: {
                const auto* n = static_cast<const TextureSampleNode*>(node.get());
                line << ' ' << n->texturePath();
                break;
            }
            default:
                break;
        }
        writeLine(out, line.str());
    }

    writeLine(out, "link_count " + std::to_string(graph.connectionCount()));
    for (const auto& link : graph.connections()) {
        std::ostringstream line;
        line << "link " << link.fromNode << ' ' << link.fromPin << ' '
             << link.toNode << ' ' << link.toPin;
        writeLine(out, line.str());
    }
    return true;
}

bool MaterialSerializer::load(const std::filesystem::path& path, MaterialDocument& out, ShaderGraph& graph) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    graph.clear();
    out.name.clear();
    out.properties = Assets::Material3D{};

    std::string line;
    if (!std::getline(in, line) || line != "CAFMAT1") return false;

    uint32_t expectedNodes = 0;
    uint32_t expectedLinks = 0;
    uint32_t nodesRead = 0;
    uint32_t linksRead = 0;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "name") {
            std::getline(ss, out.name);
            if (!out.name.empty() && out.name[0] == ' ') out.name.erase(0, 1);
        } else if (tag == "albedo") {
            ss >> out.properties.albedoColor.r >> out.properties.albedoColor.g
               >> out.properties.albedoColor.b >> out.properties.albedoColor.a;
        } else if (tag == "roughness") {
            ss >> out.properties.roughness;
        } else if (tag == "metallic") {
            ss >> out.properties.metallic;
        } else if (tag == "node_count") {
            ss >> expectedNodes;
        } else if (tag == "link_count") {
            ss >> expectedLinks;
        } else if (tag == "node") {
            uint32_t id = 0;
            std::string typeName;
            ss >> id >> typeName;
            const NodeType type = nodeTypeFromName(typeName);
            graph.addNodeWithId(type, id);
            ShaderNode* node = graph.getNode(id);
            if (!node) continue;
            if (type == NodeType::ColorConstant) {
                auto* n = static_cast<ColorConstantNode*>(node);
                ss >> n->color[0] >> n->color[1] >> n->color[2] >> n->color[3];
            } else if (type == NodeType::FloatConstant) {
                auto* n = static_cast<FloatConstantNode*>(node);
                ss >> n->value;
            } else if (type == NodeType::TextureSample) {
                auto* n = static_cast<TextureSampleNode*>(node);
                std::string texPath;
                ss >> texPath;
                n->setTexturePath(texPath.c_str());
            }
            nodesRead++;
        } else if (tag == "link") {
            uint32_t fromNode = 0, fromPin = 0, toNode = 0, toPin = 0;
            ss >> fromNode >> fromPin >> toNode >> toPin;
            graph.connect(fromNode, static_cast<int>(fromPin), toNode, static_cast<int>(toPin));
            linksRead++;
        }
    }

    if (graph.empty()) {
        graph.addNode(NodeType::OutputPBR);
    }
    if (expectedNodes > 0 && nodesRead != expectedNodes) return false;
    if (expectedLinks > 0 && linksRead != expectedLinks) return false;
    return true;
}

}  // namespace Caffeine::Editor
