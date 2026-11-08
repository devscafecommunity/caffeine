#include "caffeine/ui/Widgets.hpp"
#include "editor/DryadNodeScriptPanel.hpp"

#if defined(CF_HAS_IMGUI) && defined(CF_HAS_DRYAD)

#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include "script/ScriptTypes.hpp"
#include "script/nodes/NodeCatalog.hpp"
#include "script/nodes/NodeEmitter.hpp"
#include "script/nodes/NodeGraphIO.hpp"
#include "script/nodes/NodeRoundTrip.hpp"

#include <algorithm>
#include <fstream>

#include <imgui.h>
#include <imnodes.h>

#include <cstdint>
#include <cstdio>
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

    Translate_In = 50,
    Translate_X = 51,
    Translate_Y = 52,
    Translate_Z = 53,
    Translate_Out = 54,

    DeltaTime_Value = 60,

    SetScale_In = 70,
    SetScale_X = 71,
    SetScale_Y = 72,
    SetScale_Z = 73,
    SetScale_Out = 74,

    GetScale_X = 80,
    GetScale_Y = 81,
    GetScale_Z = 82,

    Log_In = 90,
    Log_Out = 91,
    Log_Message = 92,

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
    case Translate_In: return "in";
    case Translate_X: return "x";
    case Translate_Y: return "y";
    case Translate_Z: return "z";
    case Translate_Out: return "out";
    case DeltaTime_Value: return "value";
    case SetScale_In: return "in";
    case SetScale_X: return "x";
    case SetScale_Y: return "y";
    case SetScale_Z: return "z";
    case SetScale_Out: return "out";
    case GetScale_X: return "x";
    case GetScale_Y: return "y";
    case GetScale_Z: return "z";
    case Log_In: return "in";
    case Log_Out: return "out";
    case Log_Message: return "message";
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
    case NodeKind::Translate: return slot == Translate_Out;
    case NodeKind::SetScale: return slot == SetScale_Out;
    case NodeKind::Log: return slot == Log_Out;
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
    case NodeKind::Translate: return slot == Translate_In;
    case NodeKind::SetScale: return slot == SetScale_In;
    case NodeKind::Log: return slot == Log_In;
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
    case NodeKind::GetScale:
        return slot == GetScale_X || slot == GetScale_Y || slot == GetScale_Z;
    case NodeKind::Constant: return slot == Constant_ValueOut;
    case NodeKind::DeltaTime: return slot == DeltaTime_Value;
    default: return false;
    }
}

bool isDataInputPin(NodeKind kind, PinSlot slot) {
    switch (kind) {
    case NodeKind::SetPosition:
        return slot == SetPosition_X || slot == SetPosition_Y || slot == SetPosition_Z;
    case NodeKind::Translate:
        return slot == Translate_X || slot == Translate_Y || slot == Translate_Z;
    case NodeKind::SetScale:
        return slot == SetScale_X || slot == SetScale_Y || slot == SetScale_Z;
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

constexpr ImNodesPinShape kExecPin = ImNodesPinShape_TriangleFilled;
constexpr ImNodesPinShape kDataPin = ImNodesPinShape_CircleFilled;

bool hasDataEdge(const NodeGraph& graph, const std::string& nodeId, const char* pin) {
    const std::string target = nodeId + "." + pin;
    for (const GraphEdge& edge : graph.edges) {
        if (edge.to == target) return true;
    }
    return false;
}

ImU32 lighten(ImU32 color, int amount) {
    auto channel = [&](int shift) {
        const int value = static_cast<int>((color >> shift) & 0xFF) + amount;
        return value > 255 ? 255 : value;
    };
    return IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT),
                    channel(IM_COL32_B_SHIFT), 255);
}

ImU32 nodeHeaderColor(NodeKind kind) {
    switch (kind) {
    case NodeKind::OnUpdate: return IM_COL32(38, 112, 92, 255);
    case NodeKind::SetPosition: return IM_COL32(45, 82, 138, 255);
    case NodeKind::Translate: return IM_COL32(62, 98, 160, 255);
    case NodeKind::Branch: return IM_COL32(148, 92, 42, 255);
    case NodeKind::SetScale: return IM_COL32(52, 96, 128, 255);
    case NodeKind::GetPosition: return IM_COL32(112, 80, 45, 255);
    case NodeKind::GetScale: return IM_COL32(122, 96, 52, 255);
    case NodeKind::Log: return IM_COL32(76, 88, 96, 255);
    case NodeKind::DeltaTime: return IM_COL32(102, 74, 138, 255);
    case NodeKind::Constant: return IM_COL32(88, 90, 102, 255);
    }
    return IM_COL32(70, 74, 84, 255);
}

const char* nodeSubtitle(NodeKind kind) {
    switch (kind) {
    case NodeKind::OnUpdate: return "runs every frame";
    case NodeKind::SetPosition: return "absolute position";
    case NodeKind::Translate: return "relative motion";
    case NodeKind::Branch: return "if / else";
    case NodeKind::SetScale: return "absolute scale";
    case NodeKind::GetPosition: return "reads position";
    case NodeKind::GetScale: return "reads scale";
    case NodeKind::Log: return "console message";
    case NodeKind::DeltaTime: return "frame delta";
    case NodeKind::Constant: return "fixed value";
    }
    return "";
}

void drawNodeBody(const GraphNode& node, NodeGraph& graph, bool& graphDirty,
                  UIKit::NodeCanvas& canvas) {
    const int imId = imNodeIdFromString(node.id);
    GraphNode* mutableNode = graph.findNode(node.id);
    canvas.beginNode(imId, nodeKindToTypeName(node.kind), nodeSubtitle(node.kind),
                     nodeHeaderColor(node.kind));

    auto execInput = [&](PinSlot slot, const char* label) {
        ImNodes::BeginInputAttribute(attrId(imId, slot), kExecPin);
        ImGui::TextUnformatted(label);
        ImNodes::EndInputAttribute();
    };
    auto execOutput = [&](PinSlot slot, const char* label) {
        ImNodes::BeginOutputAttribute(attrId(imId, slot), kExecPin);
        ImGui::TextUnformatted(label);
        ImNodes::EndOutputAttribute();
    };
    auto dataOutput = [&](PinSlot slot, const char* label) {
        ImNodes::BeginOutputAttribute(attrId(imId, slot), kDataPin);
        ImGui::TextUnformatted(label);
        ImNodes::EndOutputAttribute();
    };
    auto dataInput = [&](PinSlot slot, const char* label, const char* pin, double* value) {
        ImNodes::BeginInputAttribute(attrId(imId, slot), kDataPin);
        ImGui::TextUnformatted(label);
        if (!hasDataEdge(graph, node.id, pin)) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(canvas.scaled(84.0f));
            ImGui::PushID(pin);
            if (ImGui::DragScalar("##value", ImGuiDataType_Double, value, 0.1f,
                                  nullptr, nullptr, "%.3f")) {
                graphDirty = true;
            }
            ImGui::PopID();
        }
        ImNodes::EndInputAttribute();
    };

    switch (node.kind) {
    case NodeKind::OnUpdate:
        execOutput(OnUpdate_Out, "then");
        break;
    case NodeKind::SetPosition:
    case NodeKind::Translate:
    case NodeKind::SetScale: {
        PinSlot inSlot = SetPosition_In;
        PinSlot xSlot = SetPosition_X;
        PinSlot ySlot = SetPosition_Y;
        PinSlot zSlot = SetPosition_Z;
        PinSlot outSlot = SetPosition_Out;
        if (node.kind == NodeKind::Translate) {
            inSlot = Translate_In;
            xSlot = Translate_X;
            ySlot = Translate_Y;
            zSlot = Translate_Z;
            outSlot = Translate_Out;
        } else if (node.kind == NodeKind::SetScale) {
            inSlot = SetScale_In;
            xSlot = SetScale_X;
            ySlot = SetScale_Y;
            zSlot = SetScale_Z;
            outSlot = SetScale_Out;
        }
        execInput(inSlot, "in");
        dataInput(xSlot, "x", "x", mutableNode ? &mutableNode->inputX : nullptr);
        dataInput(ySlot, "y", "y", mutableNode ? &mutableNode->inputY : nullptr);
        dataInput(zSlot, "z", "z", mutableNode ? &mutableNode->inputZ : nullptr);
        execOutput(outSlot, "out");
        break;
    }
    case NodeKind::GetPosition:
    case NodeKind::GetScale: {
        const bool scale = node.kind == NodeKind::GetScale;
        dataOutput(scale ? GetScale_X : GetPosition_X, "x");
        dataOutput(scale ? GetScale_Y : GetPosition_Y, "y");
        dataOutput(scale ? GetScale_Z : GetPosition_Z, "z");
        break;
    }
    case NodeKind::Log: {
        execInput(Log_In, "in");
        ImNodes::BeginStaticAttribute(attrId(imId, Log_Message));
        if (mutableNode) {
            char message[256] = {};
            std::strncpy(message, mutableNode->textValue.c_str(), sizeof(message) - 1);
            ImGui::SetNextItemWidth(canvas.scaled(140.0f));
            if (ImGui::InputText("##message", message, sizeof(message))) {
                mutableNode->textValue = message;
                graphDirty = true;
            }
        }
        ImNodes::EndStaticAttribute();
        execOutput(Log_Out, "out");
        break;
    }
    case NodeKind::DeltaTime:
        dataOutput(DeltaTime_Value, "dt");
        break;
    case NodeKind::Constant: {
        ImNodes::BeginStaticAttribute(attrId(imId, Constant_ValueUi));
        if (mutableNode) {
            int mode = mutableNode->constantIsBool ? 1 : 0;
            const char* modes[] = {"Number", "Bool"};
            ImGui::SetNextItemWidth(canvas.scaled(78.0f));
            if (ImGui::Combo("##mode", &mode, modes, 2)) {
                mutableNode->constantIsBool = mode == 1;
                graphDirty = true;
            }
            ImGui::SameLine();
            if (mutableNode->constantIsBool) {
                if (ImGui::Checkbox("##bool", &mutableNode->boolValue)) graphDirty = true;
            } else {
                ImGui::SetNextItemWidth(canvas.scaled(78.0f));
                if (ImGui::DragScalar("##value", ImGuiDataType_Double,
                                      &mutableNode->numberValue, 0.1f, nullptr, nullptr,
                                      "%.3f")) {
                    graphDirty = true;
                }
            }
        }
        ImNodes::EndStaticAttribute();
        dataOutput(Constant_ValueOut, "value");
        break;
    }
    case NodeKind::Branch:
        execInput(Branch_In, "in");
        ImNodes::BeginInputAttribute(attrId(imId, Branch_Condition), kDataPin);
        ImGui::TextUnformatted("if");
        if (!hasDataEdge(graph, node.id, "condition") && mutableNode) {
            ImGui::SameLine();
            if (ImGui::Checkbox("##condition", &mutableNode->inputCondition)) {
                graphDirty = true;
            }
        }
        ImNodes::EndInputAttribute();
        execOutput(Branch_Then, "then");
        execOutput(Branch_Else, "else");
        execOutput(Branch_Out, "next");
        break;
    }

    canvas.endNode();
}

} // namespace

DryadNodeScriptPanel::DryadNodeScriptPanel() = default;
DryadNodeScriptPanel::~DryadNodeScriptPanel() = default;

static std::filesystem::path projectRootFor(const EditorContext& ctx) {
    if (!ctx.projectRootPath.empty()) return ctx.projectRootPath;
    if (!ctx.currentScenePath.empty()) {
        const std::filesystem::path sceneDir =
            std::filesystem::path(ctx.currentScenePath).parent_path();
        const std::filesystem::path root = sceneDir.parent_path();
        return root.empty() ? sceneDir : root;
    }
    return std::filesystem::current_path();
}

std::string runtimePathFor(const std::filesystem::path& scriptPath,
                           const EditorContext& ctx) {
    const std::filesystem::path root = projectRootFor(ctx);
    if (scriptPath.empty() || root.empty()) return scriptPath.generic_string();
    std::error_code ec;
    const std::filesystem::path absoluteScript =
        scriptPath.is_absolute() ? scriptPath : root / scriptPath;
    const std::filesystem::path canonicalScript =
        std::filesystem::weakly_canonical(absoluteScript, ec);
    if (ec) return scriptPath.generic_string();
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return scriptPath.generic_string();
    const std::filesystem::path relative = canonicalScript.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.native().rfind("..", 0) == 0) {
        return canonicalScript.string();
    }
    return relative.generic_string();
}

static std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Compares script text while ignoring line-ending and trailing-whitespace noise.
static bool sameScriptText(std::string a, std::string b) {
    auto normalize = [](std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (char c : text) {
            if (c != '\r') out.push_back(c);
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
            out.pop_back();
        }
        text = std::move(out);
    };
    normalize(a);
    normalize(b);
    return a == b;
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
    m_resetView = true;
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

    // When the sibling JSON emits exactly the current script text, it is the
    // source of truth: reloading it keeps node positions, inline input values
    // and orphan nodes. A hand-edited .dyd no longer matches and wins instead.
    bool jsonInSync = false;
    if (hasJson && !dyd.empty()) {
        const EmitResult fromJson = NodeEmitter::emit(jsonGraph);
        jsonInSync = fromJson.ok && sameScriptText(fromJson.dyd, dyd);
    }
    if (jsonInSync) {
        m_graph = jsonGraph;
        m_status = "Loaded canvas from .node.json.";
    } else {
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
    m_canvas.activate();
    for (const auto& node : m_graph.nodes) {
        const int imId = imNodeIdFromString(node.id);
        ImNodes::SetNodeGridSpacePos(
            imId, m_canvas.worldToCanvas(ImVec2(node.x, node.y)));
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
    if (!m_canvas.context()) return false;

    for (auto& node : m_graph.nodes) {
        const int imId = imNodeIdFromString(node.id);
        const ImVec2 world =
            m_canvas.canvasToWorld(ImNodes::GetNodeGridSpacePos(imId));
        node.x = world.x;
        node.y = world.y;
    }

    if (!writeFiles) return true;
    return commitToDisk();
}

void DryadNodeScriptPanel::addNode(const char* typeName) {
    const int index = static_cast<int>(m_graph.nodes.size());
    const f32 x = 60.0f + static_cast<f32>(index % 4) * 220.0f;
    const f32 y = 60.0f + static_cast<f32>(index / 4) * 170.0f;
    addNodeAt(typeName, x, y);
}

void DryadNodeScriptPanel::addNodeAt(const char* typeName, f32 canvasX, f32 canvasY) {
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
    node.x = canvasX;
    node.y = canvasY;
    if (kind == NodeKind::Constant) {
        node.numberValue = 1.0;
    }
    m_graph.nodes.push_back(node);
    m_pendingGraphSync = true;
    commitToDisk();
    m_status = std::string("Added ") + nodeKindToTypeName(kind) + ".";
}

void DryadNodeScriptPanel::addNodePopupItems() {
    bool hasOnUpdate = false;
    for (const auto& node : m_graph.nodes) {
        if (node.kind == NodeKind::OnUpdate) {
            hasOnUpdate = true;
            break;
        }
    }

    auto item = [&](const NodeCatalogEntry& entry, bool enabled) {
        if (!ImGui::MenuItem(entry.displayName, nullptr, false, enabled)) return;
        if (m_addNodeAtMouse) {
            f32 canvasX = 60.0f;
            f32 canvasY = 60.0f;
            screenToCanvas(m_addNodeScreenX, m_addNodeScreenY, canvasX, canvasY);
            addNodeAt(entry.typeName, canvasX, canvasY);
        } else {
            addNode(entry.typeName);
        }
        ImGui::CloseCurrentPopup();
    };

    const auto& entries = nodeCatalog();
    std::vector<std::string> categories;
    for (const auto& entry : entries) {
        if (std::find(categories.begin(), categories.end(), entry.category) ==
            categories.end()) {
            categories.emplace_back(entry.category);
        }
    }
    for (size_t categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
        const std::string& category = categories[categoryIndex];
        ImGui::TextDisabled("%s", category.c_str());
        for (const auto& entry : entries) {
            if (category != entry.category) continue;
            const bool enabled = !(entry.kind == NodeKind::OnUpdate && hasOnUpdate);
            item(entry, enabled);
        }
        if (categoryIndex + 1 < categories.size()) ImGui::Separator();
    }
}

void DryadNodeScriptPanel::screenToCanvas(f32 screenX, f32 screenY, f32& canvasX,
                                          f32& canvasY) const {
    canvasX = 60.0f;
    canvasY = 60.0f;
    if (m_graph.nodes.empty()) return;
    const Script::Nodes::GraphNode* anchorNode = &m_graph.nodes.front();
    const int anchor = imNodeIdFromString(anchorNode->id);
    const ImVec2 world = m_canvas.screenToWorld(
        ImVec2(screenX, screenY), anchor,
        ImVec2(anchorNode->x, anchorNode->y));
    canvasX = world.x;
    canvasY = world.y;
}

void DryadNodeScriptPanel::deleteSelection() {

    const int selectedNodeCount = ImNodes::NumSelectedNodes();
    std::vector<int> selectedNodes(selectedNodeCount);
    if (selectedNodeCount > 0) ImNodes::GetSelectedNodes(selectedNodes.data());

    const int selectedLinkCount = ImNodes::NumSelectedLinks();
    std::vector<int> selectedLinks(selectedLinkCount);
    if (selectedLinkCount > 0) ImNodes::GetSelectedLinks(selectedLinks.data());

    if (selectedNodeCount == 0 && selectedLinkCount == 0) {
        m_status = "Select a node or link first.";
        return;
    }

    bool changed = false;
    if (selectedLinkCount > 0) {
        m_graph.edges.erase(
            std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                           [&](const GraphEdge& edge) {
                               return std::find(selectedLinks.begin(), selectedLinks.end(),
                                                linkIdForEdge(edge)) != selectedLinks.end();
                           }),
            m_graph.edges.end());
        changed = true;
    }

    int onUpdateCount = 0;
    for (const auto& node : m_graph.nodes) {
        if (node.kind == NodeKind::OnUpdate) ++onUpdateCount;
    }

    std::vector<std::string> removeIds;
    for (int imId : selectedNodes) {
        for (const auto& node : m_graph.nodes) {
            if (imNodeIdFromString(node.id) != imId) continue;
            if (node.kind == NodeKind::OnUpdate && onUpdateCount <= 1) {
                m_status = "The graph needs one OnUpdate node.";
                break;
            }
            removeIds.push_back(node.id);
            break;
        }
    }

    for (const std::string& id : removeIds) {
        const std::string prefix = id + ".";
        m_graph.edges.erase(
            std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                           [&](const GraphEdge& edge) {
                               return edge.from.rfind(prefix, 0) == 0 ||
                                      edge.to.rfind(prefix, 0) == 0;
                           }),
            m_graph.edges.end());
        m_graph.nodes.erase(
            std::remove_if(m_graph.nodes.begin(), m_graph.nodes.end(),
                           [&](const GraphNode& node) { return node.id == id; }),
            m_graph.nodes.end());
        changed = true;
    }

    if (!changed) return;
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    m_pendingGraphSync = true;
    commitToDisk();
    if (m_status.rfind("The graph needs", 0) != 0) {
        m_status = "Deleted selection.";
    }
}

void DryadNodeScriptPanel::arrangeGraph() {
    const GraphNode* root = nullptr;
    for (const auto& node : m_graph.nodes) {
        if (node.kind == NodeKind::OnUpdate) {
            root = &node;
            break;
        }
    }
    if (!root) {
        m_status = "Add an OnUpdate node before arranging.";
        return;
    }

    std::unordered_map<std::string, int> depth;
    std::vector<std::string> order;
    depth[root->id] = 0;
    order.push_back(root->id);
    for (size_t i = 0; i < order.size(); ++i) {
        const std::string id = order[i];
        const int nextDepth = depth[id] + 1;
        for (const auto& edge : m_graph.edges) {
            const bool execEdge = edge.from == id + ".out" ||
                                  edge.from == id + ".then" ||
                                  edge.from == id + ".else";
            if (!execEdge) continue;
            PinRef to{};
            if (!parsePinRef(edge.to, to)) continue;
            if (!m_graph.findNode(to.nodeId)) continue;
            if (depth.find(to.nodeId) != depth.end()) continue;
            depth[to.nodeId] = nextDepth;
            order.push_back(to.nodeId);
        }
    }

    std::unordered_map<int, int> rowsAtDepth;
    for (const std::string& id : order) {
        GraphNode* node = m_graph.findNode(id);
        if (!node) continue;
        const int d = depth[id];
        const int row = rowsAtDepth[d]++;
        node->x = 40.0f + static_cast<f32>(d) * 280.0f;
        node->y = 70.0f + static_cast<f32>(row) * 150.0f;
    }

    int orphanRow = rowsAtDepth[0] + 1;
    for (auto& node : m_graph.nodes) {
        if (depth.find(node.id) != depth.end()) continue;
        node.x = 40.0f;
        node.y = 70.0f + static_cast<f32>(orphanRow++) * 130.0f;
    }

    m_pendingGraphSync = true;
    m_resetView = true;
    commitToDisk();
    m_status = "Arranged the graph.";
}

void DryadNodeScriptPanel::addMotionExample() {
    GraphNode* onUpdate = nullptr;
    for (auto& node : m_graph.nodes) {
        if (node.kind == NodeKind::OnUpdate) {
            onUpdate = &node;
            break;
        }
    }
    if (!onUpdate) {
        GraphNode node;
        node.id = "n" + std::to_string(m_nextNodeSerial++);
        node.kind = NodeKind::OnUpdate;
        node.x = 40.0f;
        node.y = 40.0f;
        m_graph.nodes.push_back(node);
        onUpdate = &m_graph.nodes.back();
    }

    const float baseX = onUpdate->x + 220.0f;
    const float baseY = onUpdate->y;

    GraphNode delta;
    delta.id = "n" + std::to_string(m_nextNodeSerial++);
    delta.kind = NodeKind::DeltaTime;
    delta.x = baseX - 140.0f;
    delta.y = baseY + 110.0f;

    GraphNode translate;
    translate.id = "n" + std::to_string(m_nextNodeSerial++);
    translate.kind = NodeKind::Translate;
    translate.x = baseX;
    translate.y = baseY;

    const std::string onUpdateOut = onUpdate->id + ".out";
    m_graph.nodes.push_back(delta);
    m_graph.nodes.push_back(translate);
    m_graph.edges.push_back({onUpdateOut, translate.id + ".in"});
    m_graph.edges.push_back({delta.id + ".value", translate.id + ".y"});
    m_pendingGraphSync = true;
    commitToDisk();
    m_status = "Added a motion example: OnUpdate -> Translate by dt on Y.";
}

void DryadNodeScriptPanel::drawRuntimeControls(EditorContext& ctx) {
#ifdef CF_HAS_DRYAD
    const std::string runtimePath = runtimePathFor(m_scriptPath, ctx);
    ECS::World* world = ctx.activeWorld;
    const bool hasEntity = world != nullptr && ctx.selectedEntity.isValid();

    bool attached = false;
    bool runningInPlay = ctx.isPlayMode;
    if (hasEntity) {
        if (auto* script = world->get<Script::ScriptComponent>(ctx.selectedEntity)) {
            attached = script->scriptPath == runtimePath ||
                       std::filesystem::path(script->scriptPath).filename() ==
                           m_scriptPath.filename();
        }
    }

    if (UIKit::ToolbarButton(attached ? "Attached" : "Attach to selected",
                             attached
                                 ? "This script is on the selected entity"
                                 : "Add a Script component with this .dyd to the selected entity",
                             attached)) {
        if (!hasEntity) {
            m_status = "Select an entity in the Hierarchy first.";
        } else {
            if (auto* script = world->get<Script::ScriptComponent>(ctx.selectedEntity)) {
                script->scriptPath = runtimePath;
            } else {
                Script::ScriptComponent component;
                component.scriptPath = runtimePath;
                world->add<Script::ScriptComponent>(ctx.selectedEntity, component);
            }
            ctx.markDirty();
            m_status = "Attached " + m_scriptPath.filename().string() + " to the selected entity.";
        }
    }
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Run once", "Run onCreate and one onUpdate on the selected entity")) {
        if (!hasEntity) {
            m_status = "Select an entity in the Hierarchy first.";
        } else {
            m_previewHost.setWorld(world);
            m_previewHost.setSearchRoot(projectRootFor(ctx).string());
            std::string err;
            if (!m_previewHost.load(m_scriptPath.string(), &err)) {
                m_status = "Dryad error: " + err;
            } else {
                if (!m_previewHost.call(m_scriptPath.string(), "onCreate",
                                        ctx.selectedEntity, 0.0f, &err)) {
                    m_status = "onCreate error: " + err;
                } else if (!m_previewHost.call(m_scriptPath.string(), "onUpdate",
                                               ctx.selectedEntity, 1.0f / 60.0f, &err)) {
                    m_status = "onUpdate error: " + err;
                } else {
                    ctx.markDirty();
                    m_status = "Ran onUpdate once on the selected entity.";
                }
            }
        }
    }
    ImGui::SameLine();
    if (attached) {
        if (runningInPlay) {
            UIKit::TextMuted("Play mode is running this script.");
        } else {
            UIKit::TextMuted("Press Play to run it every frame.");
        }
    } else if (hasEntity) {
        UIKit::TextMuted("Not attached to the selected entity.");
    } else {
        UIKit::TextMuted("Select an entity, then Attach.");
    }

    bool hasOnUpdate = false;
    bool onUpdateWired = false;
    for (const auto& node : m_graph.nodes) {
        if (node.kind != NodeKind::OnUpdate) continue;
        hasOnUpdate = true;
        const std::string source = node.id + ".out";
        for (const auto& edge : m_graph.edges) {
            if (edge.from == source) {
                onUpdateWired = true;
                break;
            }
        }
    }
    if (!hasOnUpdate) {
        UIKit::TextDanger("No OnUpdate node: play mode will do nothing.");
    } else if (!onUpdateWired) {
        UIKit::TextMuted("OnUpdate has no outgoing exec link, so the graph does nothing yet.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Add motion example")) addMotionExample();
    }
#endif
}

void DryadNodeScriptPanel::onImGuiRender(EditorContext& ctx) {
    if (!m_open) return;
    m_canvas.ensureReady();

    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    if (!UIKit::BeginPanel("Dryad Node Script", &m_open)) {
        ImGui::End();
        return;
    }

    if (m_scriptPath.empty()) {
        ImGui::TextDisabled("Open a .dyd script from the Asset Browser.");
        ImGui::End();
        return;
    }

    ImGui::Text("Script: %s", m_scriptPath.filename().string().c_str());
    ImGui::SameLine();
    if (UIKit::ToolbarButton("+ Add node", "Add a node to the graph")) {
        m_addNodeAtMouse = false;
        ImGui::OpenPopup("##dryad_add_node");
    }
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Save", "Write the .dyd and its .node.json")) commitToDisk();
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Sync from .dyd", "Rebuild the canvas from the script text")) {
        syncFromDydSource();
    }
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Home", "Center the graph in the canvas")) m_resetView = true;
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Arrange", "Lay the graph out in flow order")) arrangeGraph();
    ImGui::SameLine();
    if (UIKit::ToolbarButton("Delete", "Delete the selected nodes and links")) {
        m_deleteRequested = true;
    }

    auto applyZoom = [&](f32 zoom) {
        m_canvas.setZoom(zoom);
        m_pendingGraphSync = true;
        m_resetView = true;
    };
    ImGui::SameLine();
    if (UIKit::ToolbarButton("-", "Zoom out")) applyZoom(m_canvas.zoom() - 0.1f);
    ImGui::SameLine();
    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%", m_canvas.zoom() * 100.0f);
    if (UIKit::ToolbarButton(zoomLabel, "Reset zoom to 100%")) applyZoom(1.0f);
    ImGui::SameLine();
    if (UIKit::ToolbarButton("+", "Zoom in")) applyZoom(m_canvas.zoom() + 0.1f);

    if (!m_status.empty()) {
        UIKit::TextMuted(m_status.c_str());
    }

    drawRuntimeControls(ctx);
    ImGui::TextDisabled(
        "Middle-drag: pan  ·  Right-click: add node  ·  Del: delete  ·  drag a pin: link");

    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
    m_canvas.begin();

    if (m_pendingGraphSync) {
        rebuildImNodesFromGraph();
    }
    if (m_resetView) {
        if (!m_graph.nodes.empty()) {
            f32 minX = m_graph.nodes.front().x;
            f32 minY = m_graph.nodes.front().y;
            f32 maxX = minX + 220.0f;
            f32 maxY = minY + 160.0f;
            for (const auto& node : m_graph.nodes) {
                minX = std::min(minX, node.x);
                minY = std::min(minY, node.y);
                maxX = std::max(maxX, node.x + 220.0f);
                maxY = std::max(maxY, node.y + 160.0f);
            }
            if (m_canvas.centerOnWorldBounds(ImVec2(minX, minY), ImVec2(maxX, maxY))) {
                m_resetView = false;
            }
        } else {
            ImNodes::EditorContextResetPanning(ImVec2(0, 0));
            m_resetView = false;
        }
    }

    std::string droppedType;
    if (m_canvas.acceptNodeDrop("DRYAD_NODE", droppedType)) {
        m_dropNodeType = droppedType;
        m_dropPending = true;
        m_dropScreenX = ImGui::GetIO().MousePos.x;
        m_dropScreenY = ImGui::GetIO().MousePos.y;
    }

    bool graphDirty = false;
    std::unordered_map<int, std::string> imToId;
    for (const auto& node : m_graph.nodes) {
        imToId[imNodeIdFromString(node.id)] = node.id;
    }

    for (const auto& node : m_graph.nodes) {
        drawNodeBody(node, m_graph, graphDirty, m_canvas);
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
            if (kind == NodeKind::Translate) {
                if (pin == "in") {
                    out = Translate_In;
                    return true;
                }
                if (pin == "x") {
                    out = Translate_X;
                    return true;
                }
                if (pin == "y") {
                    out = Translate_Y;
                    return true;
                }
                if (pin == "z") {
                    out = Translate_Z;
                    return true;
                }
                if (pin == "out") {
                    out = Translate_Out;
                    return true;
                }
            }
            if (kind == NodeKind::SetScale) {
                if (pin == "in") {
                    out = SetScale_In;
                    return true;
                }
                if (pin == "x") {
                    out = SetScale_X;
                    return true;
                }
                if (pin == "y") {
                    out = SetScale_Y;
                    return true;
                }
                if (pin == "z") {
                    out = SetScale_Z;
                    return true;
                }
                if (pin == "out") {
                    out = SetScale_Out;
                    return true;
                }
            }
            if (kind == NodeKind::Log) {
                if (pin == "in") {
                    out = Log_In;
                    return true;
                }
                if (pin == "out") {
                    out = Log_Out;
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
            if (kind == NodeKind::GetScale) {
                if (pin == "x") {
                    out = GetScale_X;
                    return true;
                }
                if (pin == "y") {
                    out = GetScale_Y;
                    return true;
                }
                if (pin == "z") {
                    out = GetScale_Z;
                    return true;
                }
            }
            if (kind == NodeKind::DeltaTime && pin == "value") {
                out = DeltaTime_Value;
                return true;
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

        const bool execLink = isExecOutputPin(fromNode->kind, fromSlot);
        const ImU32 linkColor = execLink ? IM_COL32(150, 198, 255, 235)
                                         : IM_COL32(232, 186, 104, 235);
        ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
        ImNodes::PushColorStyle(ImNodesCol_LinkHovered, lighten(linkColor, 26));
        ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 220, 140, 255));
        ImNodes::Link(linkIdForEdge(edge), attrId(fromIm, fromSlot), attrId(toIm, toSlot));
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    m_canvas.end();
    ImNodes::PopAttributeFlag();

    if (m_canvas.consumeZoomChanged()) {
        m_pendingGraphSync = true;
        m_resetView = true;
    }

    if (m_dropPending) {
        m_dropPending = false;
        f32 canvasX = 60.0f;
        f32 canvasY = 60.0f;
        screenToCanvas(m_dropScreenX, m_dropScreenY, canvasX, canvasY);
        addNodeAt(m_dropNodeType.c_str(), canvasX, canvasY);
    }

    if (m_deleteRequested) {
        m_deleteRequested = false;
        deleteSelection();
    }

    const bool editorActive =
        ImNodes::IsEditorHovered() ||
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (editorActive && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        deleteSelection();
    }

    if (ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        m_addNodeScreenX = mouse.x;
        m_addNodeScreenY = mouse.y;
        m_addNodeAtMouse = true;
        ImGui::OpenPopup("##dryad_add_node");
    }
    if (ImGui::BeginPopup("##dryad_add_node")) {
        addNodePopupItems();
        ImGui::EndPopup();
    }

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
