#include "editor/DryadNodeScriptPanel.hpp"

#if defined(CF_HAS_IMGUI) && defined(CF_HAS_DRYAD)

#include "editor/EditorContext.hpp"
#include "script/nodes/NodeEmitter.hpp"
#include "script/nodes/NodeGraphIO.hpp"
#include "script/nodes/NodeRoundTrip.hpp"

#include <fstream>

#include <imgui.h>
#include <imnodes.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Caffeine::Editor {
namespace {

using namespace Script::Nodes;

enum PinSlot : int {
    OnUpdate_Out = 1,

    SetPosition_In = 10,
    SetPosition_X = 11,
    SetPosition_Y = 12,
    SetPosition_Z = 13,
    SetPosition_Out = 14,

    GetPosition_X = 20,
    GetPosition_Y = 21,
    GetPosition_Z = 22,

    Constant_ValueUi = 30,
    Constant_ValueOut = 31,

    Branch_In = 40,
    Branch_Condition = 41,
    Branch_Then = 42,
    Branch_Else = 43,
    Branch_Out = 44,
};

int imNodeIdFromString(const std::string& id) {
    uint32_t hash = 5381;
    for (unsigned char c : id) hash = ((hash << 5) + hash) + c;
    return static_cast<int>(hash & 0x3fffffff) + 1;
}

constexpr int kAttrStride = 64;

int attrId(int nodeImId, PinSlot slot) { return nodeImId * kAttrStride + static_cast<int>(slot); }

bool decodeAttr(int attributeId, int& nodeImId, PinSlot& slot) {
    if (attributeId <= 0) return false;
    nodeImId = attributeId / kAttrStride;
    slot = static_cast<PinSlot>(attributeId % kAttrStride);
    return true;
}

const char* pinName(PinSlot slot) {
    switch (slot) {
    case OnUpdate_Out: return "out";
    case SetPosition_In: return "in";
    case SetPosition_X: return "x";
    case SetPosition_Y: return "y";
    case SetPosition_Z: return "z";
    case SetPosition_Out: return "out";
    case GetPosition_X: return "x";
    case GetPosition_Y: return "y";
    case GetPosition_Z: return "z";
    case Constant_ValueOut: return "value";
    case Branch_In: return "in";
    case Branch_Condition: return "condition";
    case Branch_Then: return "then";
    case Branch_Else: return "else";
    case Branch_Out: return "out";
    }
    return "out";
}

bool isExecOutputPin(NodeKind kind, PinSlot slot) {
    switch (kind) {
    case NodeKind::OnUpdate: return slot == OnUpdate_Out;
    case NodeKind::SetPosition: return slot == SetPosition_Out;
    case NodeKind::GetPosition:
    case NodeKind::Constant: return false;
    case NodeKind::Branch:
        return slot == Branch_Then || slot == Branch_Else || slot == Branch_Out;
    }
    return false;
}

bool isExecInputPin(NodeKind kind, PinSlot slot) {
    switch (kind) {
    case NodeKind::OnUpdate: return false;
    case NodeKind::SetPosition: return slot == SetPosition_In;
    case NodeKind::GetPosition:
    case NodeKind::Constant: return false;
    case NodeKind::Branch: return slot == Branch_In;
    }
    return false;
}

bool isDataOutputPin(NodeKind kind, PinSlot slot) {
    switch (kind) {
    case NodeKind::GetPosition:
        return slot == GetPosition_X || slot == GetPosition_Y || slot == GetPosition_Z;
    case NodeKind::Constant: return slot == Constant_ValueOut;
    default: return false;
    }
}

bool isDataInputPin(NodeKind kind, PinSlot slot) {
    switch (kind) {
    case NodeKind::SetPosition:
        return slot == SetPosition_X || slot == SetPosition_Y || slot == SetPosition_Z;
    case NodeKind::Branch: return slot == Branch_Condition;
    default: return false;
    }
}

bool canConnectPins(const GraphNode* from, PinSlot fromSlot, const GraphNode* to, PinSlot toSlot) {
    if (!from || !to) return false;
    if (isExecOutputPin(from->kind, fromSlot) && isExecInputPin(to->kind, toSlot)) return true;
    if (isDataOutputPin(from->kind, fromSlot) && isDataInputPin(to->kind, toSlot)) return true;
    return false;
}

std::string endpoint(const std::string& nodeId, const char* pin) { return nodeId + "." + pin; }

int linkIdForEdge(const GraphEdge& edge) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : edge.from) hash = (hash ^ c) * 16777619u;
    hash ^= static_cast<uint32_t>('-');
    hash *= 16777619u;
    for (unsigned char c : edge.to) hash = (hash ^ c) * 16777619u;
    return static_cast<int>(hash & 0x3fffffff) + 1;
}

void drawNodeBody(const GraphNode& node, NodeGraph& graph, bool& graphDirty) {
    const int imId = imNodeIdFromString(node.id);
    ImNodes::BeginNode(imId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(nodeKindToTypeName(node.kind));
    ImNodes::EndNodeTitleBar();

    switch (node.kind) {
    case NodeKind::OnUpdate:
        ImNodes::BeginOutputAttribute(attrId(imId, OnUpdate_Out));
        ImGui::TextUnformatted("then");
        ImNodes::EndOutputAttribute();
        break;
    case NodeKind::SetPosition:
        ImNodes::BeginInputAttribute(attrId(imId, SetPosition_In));
        ImGui::TextUnformatted("in");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(attrId(imId, SetPosition_X));
        ImGui::TextUnformatted("x");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(attrId(imId, SetPosition_Y));
        ImGui::TextUnformatted("y");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(attrId(imId, SetPosition_Z));
        ImGui::TextUnformatted("z");
        ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, SetPosition_Out));
        ImGui::TextUnformatted("out");
        ImNodes::EndOutputAttribute();
        break;
    case NodeKind::GetPosition:
        ImNodes::BeginOutputAttribute(attrId(imId, GetPosition_X));
        ImGui::TextUnformatted("x");
        ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, GetPosition_Y));
        ImGui::TextUnformatted("y");
        ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, GetPosition_Z));
        ImGui::TextUnformatted("z");
        ImNodes::EndOutputAttribute();
        break;
    case NodeKind::Constant: {
        GraphNode* mutableNode = graph.findNode(node.id);
        ImNodes::BeginStaticAttribute(attrId(imId, Constant_ValueUi));
        if (mutableNode && mutableNode->constantIsBool) {
            if (ImGui::Checkbox("value", &mutableNode->boolValue)) graphDirty = true;
        } else if (mutableNode) {
            float v = static_cast<float>(mutableNode->numberValue);
            if (ImGui::DragFloat("value", &v, 0.1f)) {
                mutableNode->numberValue = v;
                graphDirty = true;
            }
        }
        ImNodes::EndStaticAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, Constant_ValueOut));
        ImGui::Dummy(ImVec2(1, 1));
        ImNodes::EndOutputAttribute();
        break;
    }
    case NodeKind::Branch:
        ImNodes::BeginInputAttribute(attrId(imId, Branch_In));
        ImGui::TextUnformatted("in");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(attrId(imId, Branch_Condition));
        ImGui::TextUnformatted("condition");
        ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, Branch_Then));
        ImGui::TextUnformatted("then");
        ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, Branch_Else));
        ImGui::TextUnformatted("else");
        ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(attrId(imId, Branch_Out));
        ImGui::TextUnformatted("out");
        ImNodes::EndOutputAttribute();
        break;
    }

    ImNodes::EndNode();
}

} // namespace

DryadNodeScriptPanel::DryadNodeScriptPanel() = default;

DryadNodeScriptPanel::~DryadNodeScriptPanel() {
    if (m_nodesContext && ImNodes::GetCurrentContext()) {
        ImNodes::EditorContextFree(m_nodesContext);
        m_nodesContext = nullptr;
    }
    if (m_ownsImNodesRootContext) {
        ImNodes::DestroyContext();
        m_ownsImNodesRootContext = false;
    }
}

void DryadNodeScriptPanel::ensureImNodesReady() {
    if (!ImNodes::GetCurrentContext()) {
        ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
        ImNodes::CreateContext();
        ImNodes::StyleColorsDark();
        m_ownsImNodesRootContext = true;
    }
    if (!m_nodesContext) {
        m_nodesContext = ImNodes::EditorContextCreate();
    }
}

static std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void DryadNodeScriptPanel::syncFromDydSource() {
    if (m_scriptPath.empty()) return;
    const std::string dyd = readTextFile(m_scriptPath);
    if (dyd.empty()) {
        m_status = "Could not read script file.";
        return;
    }

    NodeGraph layoutHint = m_graph;
    const auto jsonPath = nodeJsonPathForScript(m_scriptPath);
    if (std::filesystem::exists(jsonPath)) {
        const LoadResult loaded = loadFromJsonFile(jsonPath);
        if (loaded.ok) layoutHint = loaded.graph;
    }

    const ImportResult imported =
        importFromDyd(dyd, m_scriptPath.filename().string(), &layoutHint);
    if (imported.status == ImportStatus::Success) {
        m_graph = imported.graph;
        m_pendingGraphSync = true;
        m_status = imported.message;
        return;
    }
    if (imported.status == ImportStatus::NoMarkers) {
        m_status = "Script has no // @node markers (hand-edited Dryad is still valid).";
        return;
    }
    m_status = imported.message;
}

void DryadNodeScriptPanel::openFromScript(const std::filesystem::path& scriptPath) {
    m_scriptPath = scriptPath;
    m_open = true;
    m_pendingGraphSync = true;
    m_status.clear();

    const auto jsonPath = nodeJsonPathForScript(scriptPath);
    NodeGraph jsonGraph;
    bool hasJson = false;
    if (std::filesystem::exists(jsonPath)) {
        const LoadResult loaded = loadFromJsonFile(jsonPath);
        if (loaded.ok) {
            jsonGraph = loaded.graph;
            hasJson = true;
            if (jsonGraph.scriptFileName.empty()) {
                jsonGraph.scriptFileName = scriptPath.filename().string();
            }
        } else {
            m_status = loaded.error;
        }
    }

    const std::string dyd = readTextFile(scriptPath);
    const ImportResult imported =
        importFromDyd(dyd, scriptPath.filename().string(), hasJson ? &jsonGraph : nullptr);
    if (imported.status == ImportStatus::Success) {
        m_graph = imported.graph;
        m_status = imported.message;
    } else if (imported.status == ImportStatus::NoMarkers) {
        if (hasJson) {
            m_graph = jsonGraph;
            m_status = "No // @node markers — canvas from .node.json (script may differ).";
        } else {
            m_graph = defaultGraphForScript(scriptPath);
            m_status = "No // @node markers — started with empty OnUpdate graph.";
        }
    } else if (hasJson) {
        m_graph = jsonGraph;
        m_status = imported.message;
    } else {
        m_graph = defaultGraphForScript(scriptPath);
        m_status = imported.message;
    }

    int maxSerial = 1;
    for (const auto& node : m_graph.nodes) {
        if (node.id.size() > 1 && node.id[0] == 'n') {
            try {
                const int n = std::stoi(node.id.substr(1));
                maxSerial = std::max(maxSerial, n);
            } catch (...) {
            }
        }
    }
    m_nextNodeSerial = maxSerial + 1;
}

void DryadNodeScriptPanel::rebuildImNodesFromGraph() {
    ensureImNodesReady();
    if (!m_nodesContext) return;
    ImNodes::EditorContextSet(m_nodesContext);
    ImNodes::EditorContextResetPanning(ImVec2(0, 0));

    for (const auto& node : m_graph.nodes) {
        const int imId = imNodeIdFromString(node.id);
        ImNodes::SetNodeGridSpacePos(imId, ImVec2(node.x, node.y));
    }
    m_pendingGraphSync = false;
}

bool DryadNodeScriptPanel::commitToDisk() {
    if (m_scriptPath.empty()) return false;
    m_graph.scriptFileName = m_scriptPath.filename().string();
    std::string err;
    if (!NodeEmitter::writeSiblingFiles(m_scriptPath, m_graph, &err)) {
        m_status = err.empty() ? "emit failed" : err;
        return false;
    }
    m_status = "Saved " + m_scriptPath.filename().string();
    return true;
}

bool DryadNodeScriptPanel::syncGraphFromEditor(bool writeFiles) {
    if (!m_nodesContext) return false;

    for (auto& node : m_graph.nodes) {
        const int imId = imNodeIdFromString(node.id);
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(imId);
        node.x = pos.x;
        node.y = pos.y;
    }

    if (!writeFiles) return true;
    return commitToDisk();
}

void DryadNodeScriptPanel::addNode(const char* typeName) {
    NodeKind kind{};
    if (!nodeKindFromTypeName(typeName, kind)) return;

    if (kind == NodeKind::OnUpdate) {
        for (const auto& existing : m_graph.nodes) {
            if (existing.kind == NodeKind::OnUpdate) {
                m_status = "Only one OnUpdate node is allowed.";
                return;
            }
        }
    }

    GraphNode node;
    node.id = "n" + std::to_string(m_nextNodeSerial++);
    node.kind = kind;
    node.x = 80.0f + static_cast<f32>(m_graph.nodes.size()) * 24.0f;
    node.y = 120.0f;
    if (kind == NodeKind::Constant) {
        node.numberValue = 1.0;
    }
    m_graph.nodes.push_back(node);
    m_pendingGraphSync = true;
    commitToDisk();
}

void DryadNodeScriptPanel::onImGuiRender(EditorContext& ctx) {
    (void)ctx;
    if (!m_open) return;
    ensureImNodesReady();

    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dryad Node Script", &m_open)) {
        ImGui::End();
        return;
    }

    if (m_scriptPath.empty()) {
        ImGui::TextDisabled("Open a .dyd script from the Asset Browser.");
        ImGui::End();
        return;
    }

    ImGui::Text("Script: %s", m_scriptPath.filename().string().c_str());
    if (ImGui::Button("OnUpdate")) addNode("OnUpdate");
    ImGui::SameLine();
    if (ImGui::Button("SetPosition")) addNode("SetPosition");
    ImGui::SameLine();
    if (ImGui::Button("GetPosition")) addNode("GetPosition");
    ImGui::SameLine();
    if (ImGui::Button("Constant")) addNode("Constant");
    ImGui::SameLine();
    if (ImGui::Button("Branch")) addNode("Branch");
    ImGui::SameLine();
    if (ImGui::Button("Save")) commitToDisk();
    ImGui::SameLine();
    if (ImGui::Button("Sync from .dyd")) syncFromDydSource();

    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_status.c_str());
    }

    ImNodes::EditorContextSet(m_nodesContext);
    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
    ImNodes::BeginNodeEditor();

    if (m_pendingGraphSync) {
        rebuildImNodesFromGraph();
    }

    bool graphDirty = false;
    std::unordered_map<int, std::string> imToId;
    for (const auto& node : m_graph.nodes) {
        imToId[imNodeIdFromString(node.id)] = node.id;
    }

    for (const auto& node : m_graph.nodes) {
        drawNodeBody(node, m_graph, graphDirty);
    }

    for (const auto& edge : m_graph.edges) {
        PinRef from{};
        PinRef to{};
        if (!parsePinRef(edge.from, from) || !parsePinRef(edge.to, to)) continue;
        const int fromIm = imNodeIdFromString(from.nodeId);
        const int toIm = imNodeIdFromString(to.nodeId);
        const GraphNode* fromNode = m_graph.findNode(from.nodeId);
        const GraphNode* toNode = m_graph.findNode(to.nodeId);
        if (!fromNode || !toNode) continue;

        PinSlot fromSlot{};
        PinSlot toSlot{};
        auto slotFromName = [](NodeKind kind, const std::string& pin, PinSlot& out) -> bool {
            if (kind == NodeKind::OnUpdate && pin == "out") {
                out = OnUpdate_Out;
                return true;
            }
            if (kind == NodeKind::SetPosition) {
                if (pin == "in") {
                    out = SetPosition_In;
                    return true;
                }
                if (pin == "x") {
                    out = SetPosition_X;
                    return true;
                }
                if (pin == "y") {
                    out = SetPosition_Y;
                    return true;
                }
                if (pin == "z") {
                    out = SetPosition_Z;
                    return true;
                }
                if (pin == "out") {
                    out = SetPosition_Out;
                    return true;
                }
            }
            if (kind == NodeKind::GetPosition) {
                if (pin == "x") {
                    out = GetPosition_X;
                    return true;
                }
                if (pin == "y") {
                    out = GetPosition_Y;
                    return true;
                }
                if (pin == "z") {
                    out = GetPosition_Z;
                    return true;
                }
            }
            if (kind == NodeKind::Constant && pin == "value") {
                out = Constant_ValueOut;
                return true;
            }
            if (kind == NodeKind::Branch) {
                if (pin == "in") {
                    out = Branch_In;
                    return true;
                }
                if (pin == "condition") {
                    out = Branch_Condition;
                    return true;
                }
                if (pin == "then") {
                    out = Branch_Then;
                    return true;
                }
                if (pin == "else") {
                    out = Branch_Else;
                    return true;
                }
                if (pin == "out") {
                    out = Branch_Out;
                    return true;
                }
            }
            return false;
        };

        if (!slotFromName(fromNode->kind, from.pin, fromSlot) ||
            !slotFromName(toNode->kind, to.pin, toSlot)) {
            continue;
        }

        ImNodes::Link(linkIdForEdge(edge), attrId(fromIm, fromSlot), attrId(toIm, toSlot));
    }

    ImNodes::EndNodeEditor();
    ImNodes::PopAttributeFlag();

    bool topologyChanged = false;

    int startAttr = 0;
    int endAttr = 0;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
        int startNodeIm = 0;
        int endNodeIm = 0;
        PinSlot startSlot{};
        PinSlot endSlot{};
        if (decodeAttr(startAttr, startNodeIm, startSlot) && decodeAttr(endAttr, endNodeIm, endSlot)) {
            const auto startIt = imToId.find(startNodeIm);
            const auto endIt = imToId.find(endNodeIm);
            if (startIt != imToId.end() && endIt != imToId.end()) {
                const GraphNode* fromNode = m_graph.findNode(startIt->second);
                const GraphNode* toNode = m_graph.findNode(endIt->second);
                if (fromNode && toNode && canConnectPins(fromNode, startSlot, toNode, endSlot)) {
                    const std::string toEndpoint = endpoint(endIt->second, pinName(endSlot));
                    m_graph.edges.erase(
                        std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                                       [&](const GraphEdge& e) { return e.to == toEndpoint; }),
                        m_graph.edges.end());
                    GraphEdge edge;
                    edge.from = endpoint(startIt->second, pinName(startSlot));
                    edge.to = toEndpoint;
                    m_graph.edges.push_back(edge);
                    topologyChanged = true;
                }
            }
        }
    }

    int destroyedLink = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLink)) {
        m_graph.edges.erase(std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                                           [&](const GraphEdge& e) {
                                               return linkIdForEdge(e) == destroyedLink;
                                           }),
                            m_graph.edges.end());
        topologyChanged = true;
    }

    if (ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        graphDirty = true;
    }

    if (topologyChanged || graphDirty) {
        syncGraphFromEditor(topologyChanged || graphDirty);
    }

    ImGui::End();
}

} // namespace Caffeine::Editor

#else

namespace Caffeine::Editor {

DryadNodeScriptPanel::DryadNodeScriptPanel() = default;
DryadNodeScriptPanel::~DryadNodeScriptPanel() = default;
void DryadNodeScriptPanel::openFromScript(const std::filesystem::path&) {}
void DryadNodeScriptPanel::onImGuiRender(EditorContext&) {}

} // namespace Caffeine::Editor

#endif
