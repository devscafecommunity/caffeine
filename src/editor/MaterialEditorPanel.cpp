#include "editor/MaterialEditorPanel.hpp"
#include <imnodes.h>
#include <imgui.h>
#include <unordered_map>

namespace Caffeine::Editor {

// Maps (nodeId, pinIndex, isInput) -> global pin attribute ID
// Built each frame in renderGraphCanvas
struct PinAttr {
    uint32_t nodeId;
    int pinIndex;
    bool isInput;
};

static std::unordered_map<int, PinAttr> s_attrToPin;
static int s_nextAttrId = 1;

MaterialEditorPanel::MaterialEditorPanel() {
    ImNodes::CreateContext();
    m_codeBuffer[0] = '\0';
    addDefaultNodes();
}

MaterialEditorPanel::~MaterialEditorPanel() {
    ImNodes::DestroyContext();
}

void MaterialEditorPanel::addDefaultNodes() {
    m_graph.addNode(NodeType::OutputPBR);
}

void MaterialEditorPanel::onImGuiRender() {
    if (!m_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Material Editor", &m_open, ImGuiWindowFlags_MenuBar);

    renderMenuBar();

    ImGui::Columns(2, "MatEditorMain", false);
    ImGui::SetColumnWidth(0, ImGui::GetContentRegionAvail().x * 0.65f);

    if (m_mode == EditorMode::Graph) {
        renderGraphCanvas();
    } else {
        renderTextEditor();
    }

    ImGui::NextColumn();
    renderPreviewWindow();
    renderInspector();

    ImGui::Columns(1);
    ImGui::End();
    ImGui::PopStyleVar();
}

void MaterialEditorPanel::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) { m_graph.clear(); addDefaultNodes(); }
            if (ImGui::MenuItem("Compile", "F5")) recompileShader();
            ImGui::Separator();
            ImGui::MenuItem("Auto-Compile", nullptr, &m_autoCompile);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Grid", nullptr, &m_showGrid);
            ImGui::Separator();
            if (ImGui::MenuItem("Graph Mode", nullptr, m_mode == EditorMode::Graph)) {
                m_mode = EditorMode::Graph;
            }
            if (ImGui::MenuItem("Text Mode", nullptr, m_mode == EditorMode::Text)) {
                m_mode = EditorMode::Text;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add Node")) {
            if (ImGui::MenuItem("Texture Sample"))   m_graph.addNode(NodeType::TextureSample);
            if (ImGui::MenuItem("Color Constant"))    m_graph.addNode(NodeType::ColorConstant);
            if (ImGui::MenuItem("Float Constant"))    m_graph.addNode(NodeType::FloatConstant);
            if (ImGui::MenuItem("Multiply"))          m_graph.addNode(NodeType::Multiply);
            if (ImGui::MenuItem("Add"))               m_graph.addNode(NodeType::Add);
            if (ImGui::MenuItem("Lerp"))              m_graph.addNode(NodeType::Lerp);
            if (ImGui::MenuItem("Time"))              m_graph.addNode(NodeType::Time);
            if (ImGui::MenuItem("Vertex Position"))   m_graph.addNode(NodeType::VertexPosition);
            ImGui::Separator();
            if (ImGui::MenuItem("PBR Output"))        m_graph.addNode(NodeType::OutputPBR);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void MaterialEditorPanel::renderGraphCanvas() {
    ImGui::BeginChild("GraphCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);

    if (m_graph.empty()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(avail.x * 0.3f, avail.y * 0.4f));
        ImGui::TextUnformatted("Right-click to add nodes, or use Add Node menu");
        ImGui::EndChild();
        return;
    }

    ImNodes::BeginNodeEditor();

    s_attrToPin.clear();

    ImNodesStyle& style = ImNodes::GetStyle();
    style.Colors[ImNodesCol_NodeBackground] = IM_COL32(30, 30, 30, 255);
    style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(40, 40, 40, 255);
    style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(50, 50, 50, 255);

    for (auto& node : m_graph.nodes()) {
        ImNodes::BeginNode(static_cast<int>(node->id()));

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node->title().c_str());
        ImNodes::EndNodeTitleBar();

        for (int i = 0; i < static_cast<int>(node->inputs().size()); i++) {
            int attrId = s_nextAttrId++;
            s_attrToPin[attrId] = {node->id(), i, true};
            ImNodes::BeginInputAttribute(attrId);
            ImGui::TextUnformatted(node->inputs()[i].name.c_str());
            ImNodes::EndInputAttribute();
        }

        ImGui::Spacing();
        node->renderProperties();
        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(node->outputs().size()); i++) {
            int attrId = s_nextAttrId++;
            s_attrToPin[attrId] = {node->id(), i, false};
            ImNodes::BeginOutputAttribute(attrId);
            ImGui::TextUnformatted(node->outputs()[i].name.c_str());
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }

    int linkId = 0;
    for (const auto& conn : m_graph.connections()) {
        int fromAttr = 0, toAttr = 0;
        for (const auto& pair : s_attrToPin) {
            int attrId = pair.first;
            const PinAttr& pin = pair.second;
            if (pin.nodeId == conn.fromNode && pin.pinIndex == conn.fromPin && !pin.isInput) {
                fromAttr = attrId;
            }
            if (pin.nodeId == conn.toNode && pin.pinIndex == conn.toPin && pin.isInput) {
                toAttr = attrId;
            }
        }
        if (fromAttr > 0 && toAttr > 0) {
            ImNodes::Link(linkId++, fromAttr, toAttr);
        }
    }

    ImNodes::EndNodeEditor();

    // Handle new links
    int startAttr, endAttr;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
        auto fromIt = s_attrToPin.find(startAttr);
        auto toIt = s_attrToPin.find(endAttr);
        if (fromIt != s_attrToPin.end() && toIt != s_attrToPin.end()) {
            PinAttr& fromPin = fromIt->second;
            PinAttr& toPin = toIt->second;
            if (!fromPin.isInput && toPin.isInput) {
                m_graph.connect(fromPin.nodeId, fromPin.pinIndex, toPin.nodeId, toPin.pinIndex);
            } else if (!toPin.isInput && fromPin.isInput) {
                m_graph.connect(toPin.nodeId, toPin.pinIndex, fromPin.nodeId, fromPin.pinIndex);
            }
            if (m_autoCompile) recompileShader();
        }
    }

    // Handle deleted links
    int deletedLinkId;
    while (ImNodes::IsLinkDestroyed(&deletedLinkId)) {
        (void)deletedLinkId;
        // Find and remove from graph
        int linkIdx = 0;
        for (auto it = m_graph.connections().begin(); it != m_graph.connections().end(); ++it) {
            if (linkIdx == deletedLinkId) {
                m_graph.disconnect(it->fromNode, it->fromPin);
                break;
            }
            linkIdx++;
        }
    }

    int selectedNodeCount = ImNodes::NumSelectedNodes();
    if (selectedNodeCount > 0) {
        std::vector<int> selectedNodes(static_cast<size_t>(selectedNodeCount));
        ImNodes::GetSelectedNodes(selectedNodes.data());
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            for (int nodeId : selectedNodes) {
                m_graph.removeNode(static_cast<uint32_t>(nodeId));
            }
        }
    }

    if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered()) {
        ImGui::OpenPopup("AddNodePopup");
    }
    renderNodeContextMenu();
    ImGui::EndChild();
}

void MaterialEditorPanel::renderTextEditor() {
    ImGui::BeginChild("TextEditor", ImVec2(0, 0), true);

    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= 30;

    ImGui::InputTextMultiline("##code", m_codeBuffer, sizeof(m_codeBuffer),
        size, ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Compile")) {
        recompileShader();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sync to Graph")) {
        m_compiledShaderCode = m_codeBuffer;
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::renderPreviewWindow() {
    ImGui::BeginChild("Preview", ImVec2(0, 0), true);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= 30;

    m_previewRenderer.renderFallback(m_previewRotation, avail.x, avail.y);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15);
    ImGui::SliderFloat("Rotation", &m_previewRotation, 0.0f, 360.0f, "%.0f deg");

    if (m_hasError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
        ImGui::TextWrapped("%s", m_lastCompileError.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::renderInspector() {
    ImGui::BeginChild("Inspector", ImVec2(0, 0), true);

    if (m_material) {
        ImGui::TextUnformatted("Material Properties");
        ImGui::Separator();
        ImGui::ColorEdit4("Albedo", &m_material->albedoColor.r);
        ImGui::SliderFloat("Roughness", &m_material->roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Metallic", &m_material->metallic, 0.0f, 1.0f);
    } else {
        ImGui::TextUnformatted("No material selected");
    }

    if (m_mode == EditorMode::Graph && !m_graph.empty()) {
        ImGui::Separator();
        ImGui::Text("Nodes: %zu  Connections: %zu",
            m_graph.nodeCount(), m_graph.connectionCount());
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::renderNodeContextMenu() {
    if (ImGui::BeginPopup("AddNodePopup")) {
        if (ImGui::MenuItem("Texture Sample"))   m_graph.addNode(NodeType::TextureSample);
        if (ImGui::MenuItem("Color Constant"))    m_graph.addNode(NodeType::ColorConstant);
        if (ImGui::MenuItem("Float Constant"))    m_graph.addNode(NodeType::FloatConstant);
        if (ImGui::MenuItem("Multiply"))          m_graph.addNode(NodeType::Multiply);
        if (ImGui::MenuItem("Add"))               m_graph.addNode(NodeType::Add);
        if (ImGui::MenuItem("Lerp"))              m_graph.addNode(NodeType::Lerp);
        if (ImGui::MenuItem("Time"))              m_graph.addNode(NodeType::Time);
        if (ImGui::MenuItem("Vertex Position"))   m_graph.addNode(NodeType::VertexPosition);
        ImGui::Separator();
        if (ImGui::MenuItem("PBR Output"))        m_graph.addNode(NodeType::OutputPBR);
        ImGui::EndPopup();
    }
}

void MaterialEditorPanel::recompileShader() {
    m_hasError = false;
    m_lastCompileError.clear();

    std::string code;
    if (m_mode == EditorMode::Graph) {
        code = m_graph.compileGLSL();
    } else {
        code = m_codeBuffer;
    }

    if (code.empty()) {
        m_hasError = true;
        m_lastCompileError = "Generated shader code is empty";
        return;
    }

    if (code.find("void main()") == std::string::npos) {
        m_hasError = true;
        m_lastCompileError = "Shader missing main() function";
        return;
    }

    m_compiledShaderCode = code;
}

} // namespace Caffeine::Editor
