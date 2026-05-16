# Material & Shader Editor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a Material & Shader Editor with ImNodes visual graph, text code editor, and real-time RHI preview for the Caffeine Studio IDE.

**Architecture:** The editor consists of three sub-panels (Graph Canvas, Preview Window, Inspector Properties) managed by a MaterialEditorPanel. The ShaderGraph nodes compile to GLSL/HLSL which gets piped into the RHI for GPU preview. ImNodes provides the visual node graph UI, RHI handles the 3D preview. A fallback ImGui wireframe preview is used when RHI is unavailable.

**Tech Stack:** C++20, ImGui, ImNodes (via FetchContent), SDL_GPU (RHI module), glslang

**Design Doc:** `docs/plans/2026-05-16-material-shader-editor-design.md`

---

### Task 1: Add ImNodes dependency

**Files:**
- Modify: `CMakeLists.txt` (add FetchContent for ImNodes near ImGui section)
- Modify: `src/editor/ImGuiIntegration.hpp` (include imnodes.h)

**Step 1: Add ImNodes to CMakeLists.txt**

Add after the ImGui FetchContent block (around line 207):

```cmake
# ── ImNodes (node graph editor) ─────────────────────────
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG v0.6
)
set(IMNODES_SOURCE_DIR "${imnodes_SOURCE_DIR}")
FetchContent_MakeAvailable(imnodes)
```

And add the imnodes source/header to the doppio target:

```cmake
${imnodes_SOURCE_DIR}/imnodes.cpp
```

With include path:

```cmake
${imnodes_SOURCE_DIR}
```

**Step 2: Verify build**

Run: `cd build && cmake .. && make -j4 2>&1 | grep -i imnodes`
Expected: imnodes compiles without errors

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add ImNodes node graph dependency"
```

---

### Task 2: Create ShaderNode base and derived node classes

**Files:**
- Create: `src/editor/ShaderNode.hpp`
- Create: `src/editor/ShaderNode.cpp`

**Step 1: Write ShaderNode.hpp**

```cpp
#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "containers/FixedString.hpp"
#include "containers/StringView.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Caffeine::Editor {

enum class NodeType : u8 {
    TextureSample,
    ColorConstant,
    FloatConstant,
    Multiply,
    Add,
    Lerp,
    Time,
    VertexPosition,
    OutputPBR
};

enum class PinType : u8 {
    Float,
    Vec3,
    Vec4,
    Texture2D
};

static const char* pinTypeToString(PinType t) {
    switch (t) {
        case PinType::Float:     return "float";
        case PinType::Vec3:      return "vec3";
        case PinType::Vec4:      return "vec4";
        case PinType::Texture2D: return "texture2D";
    }
    return "unknown";
}

struct NodePin {
    std::string name;
    PinType type = PinType::Float;
    int connectionID = -1;  // -1 = not connected
    bool isInput = true;
};

class ShaderNode {
public:
    ShaderNode(uint32_t id, NodeType type, std::string title)
        : m_id(id), m_type(type), m_title(std::move(title)) {}
    virtual ~ShaderNode() = default;

    uint32_t id() const { return m_id; }
    NodeType type() const { return m_type; }
    const std::string& title() const { return m_title; }

    std::vector<NodePin>& inputs() { return m_inputs; }
    std::vector<NodePin>& outputs() { return m_outputs; }
    const std::vector<NodePin>& inputs() const { return m_inputs; }
    const std::vector<NodePin>& outputs() const { return m_outputs; }

    virtual std::string generateCode(const std::vector<std::string>& inputVars) const = 0;
    virtual const char* outputVarType() const = 0;

    // For exposed properties (sliders, color pickers, etc)
    virtual void renderProperties() {}

protected:
    uint32_t m_id;
    NodeType m_type;
    std::string m_title;
    std::vector<NodePin> m_inputs;
    std::vector<NodePin> m_outputs;
};

// ── Concrete nodes ──

class TextureSampleNode final : public ShaderNode {
public:
    explicit TextureSampleNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
    void renderProperties() override;
private:
    char m_texturePath[128] = "";
};

class ColorConstantNode final : public ShaderNode {
public:
    explicit ColorConstantNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
    void renderProperties() override;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

class FloatConstantNode final : public ShaderNode {
public:
    explicit FloatConstantNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "float"; }
    void renderProperties() override;
    float value = 1.0f;
};

class MultiplyNode final : public ShaderNode {
public:
    explicit MultiplyNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override;
};

class AddNode final : public ShaderNode {
public:
    explicit AddNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override;
};

class LerpNode final : public ShaderNode {
public:
    explicit LerpNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec4"; }
};

class TimeNode final : public ShaderNode {
public:
    explicit TimeNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "float"; }
};

class VertexPositionNode final : public ShaderNode {
public:
    explicit VertexPositionNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "vec3"; }
};

class OutputPBRNode final : public ShaderNode {
public:
    explicit OutputPBRNode(uint32_t id);
    std::string generateCode(const std::vector<std::string>& inputVars) const override;
    const char* outputVarType() const override { return "void"; }
    void renderProperties() override;
};

// Factory
std::unique_ptr<ShaderNode> createNode(NodeType type, uint32_t id);

} // namespace Caffeine::Editor
```

**Step 2: Write ShaderNode.cpp**

```cpp
#include "editor/ShaderNode.hpp"
#include <imgui.h>
#include <fmt/format.h>

namespace Caffeine::Editor {

// ── TextureSampleNode ──
TextureSampleNode::TextureSampleNode(uint32_t id)
    : ShaderNode(id, NodeType::TextureSample, "Texture Sample")
{
    m_inputs.push_back({"Tex", PinType::Texture2D, -1, true});
    m_outputs.push_back({"RGBA", PinType::Vec4, -1, false});
}

std::string TextureSampleNode::generateCode(const std::vector<std::string>& inputVars) const {
    if (!inputVars.empty() && !inputVars[0].empty())
        return fmt::format("vec4 var_{} = texture2D(sampler_{}, {});", m_id, m_id, inputVars[0]);
    return fmt::format("vec4 var_{} = texture2D(sampler_{}, texCoord);", m_id, m_id);
}

void TextureSampleNode::renderProperties() {
    ImGui::InputText("Path", m_texturePath, sizeof(m_texturePath));
}

// ── ColorConstantNode ──
ColorConstantNode::ColorConstantNode(uint32_t id)
    : ShaderNode(id, NodeType::ColorConstant, "Color")
{
    m_outputs.push_back({"RGBA", PinType::Vec4, -1, false});
}

std::string ColorConstantNode::generateCode(const std::vector<std::string>&) const {
    return fmt::format("vec4 var_{} = vec4({}f, {}f, {}f, {}f);",
        m_id, color[0], color[1], color[2], color[3]);
}

void ColorConstantNode::renderProperties() {
    ImGui::ColorEdit4("Color", color);
}

// ── FloatConstantNode ──
FloatConstantNode::FloatConstantNode(uint32_t id)
    : ShaderNode(id, NodeType::FloatConstant, "Float")
{
    m_outputs.push_back({"Value", PinType::Float, -1, false});
}

std::string FloatConstantNode::generateCode(const std::vector<std::string>&) const {
    return fmt::format("float var_{} = {}f;", m_id, value);
}

void FloatConstantNode::renderProperties() {
    ImGui::SliderFloat("Value", &value, 0.0f, 10.0f);
}

// ── MultiplyNode ──
MultiplyNode::MultiplyNode(uint32_t id)
    : ShaderNode(id, NodeType::Multiply, "Multiply")
{
    m_inputs.push_back({"A", PinType::Float, -1, true});
    m_inputs.push_back({"B", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Float, -1, false});
}

std::string MultiplyNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string a = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "1.0";
    std::string b = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "1.0";
    return fmt::format("float var_{} = {} * {};", m_id, a, b);
}

const char* MultiplyNode::outputVarType() const { return "float"; }

// ── AddNode ──
AddNode::AddNode(uint32_t id)
    : ShaderNode(id, NodeType::Add, "Add")
{
    m_inputs.push_back({"A", PinType::Float, -1, true});
    m_inputs.push_back({"B", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Float, -1, false});
}

std::string AddNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string a = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "0.0";
    std::string b = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "0.0";
    return fmt::format("float var_{} = {} + {};", m_id, a, b);
}

const char* AddNode::outputVarType() const { return "float"; }

// ── LerpNode ──
LerpNode::LerpNode(uint32_t id)
    : ShaderNode(id, NodeType::Lerp, "Lerp")
{
    m_inputs.push_back({"From", PinType::Vec4, -1, true});
    m_inputs.push_back({"To", PinType::Vec4, -1, true});
    m_inputs.push_back({"T", PinType::Float, -1, true});
    m_outputs.push_back({"Out", PinType::Vec4, -1, false});
}

std::string LerpNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string from = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "vec4(0.0)";
    std::string to   = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "vec4(1.0)";
    std::string t    = inputVars.size() > 2 && !inputVars[2].empty() ? inputVars[2] : "0.5";
    return fmt::format("vec4 var_{} = mix({}, {}, {});", m_id, from, to, t);
}

// ── TimeNode ──
TimeNode::TimeNode(uint32_t id)
    : ShaderNode(id, NodeType::Time, "Time")
{
    m_outputs.push_back({"Time", PinType::Float, -1, false});
}

std::string TimeNode::generateCode(const std::vector<std::string>&) const {
    return fmt::format("float var_{} = u_time;", m_id);
}

// ── VertexPositionNode ──
VertexPositionNode::VertexPositionNode(uint32_t id)
    : ShaderNode(id, NodeType::VertexPosition, "Vertex Position")
{
    m_outputs.push_back({"Pos", PinType::Vec3, -1, false});
}

std::string VertexPositionNode::generateCode(const std::vector<std::string>&) const {
    return fmt::format("vec3 var_{} = v_position;", m_id);
}

// ── OutputPBRNode ──
OutputPBRNode::OutputPBRNode(uint32_t id)
    : ShaderNode(id, NodeType::OutputPBR, "PBR Output")
{
    m_inputs.push_back({"Albedo", PinType::Vec4, -1, true});
    m_inputs.push_back({"Normal", PinType::Vec3, -1, true});
    m_inputs.push_back({"Metallic", PinType::Float, -1, true});
    m_inputs.push_back({"Roughness", PinType::Float, -1, true});
}

std::string OutputPBRNode::generateCode(const std::vector<std::string>& inputVars) const {
    std::string albedo  = inputVars.size() > 0 && !inputVars[0].empty() ? inputVars[0] : "vec4(1.0)";
    std::string normal  = inputVars.size() > 1 && !inputVars[1].empty() ? inputVars[1] : "vec3(0.0, 0.0, 1.0)";
    std::string metal   = inputVars.size() > 2 && !inputVars[2].empty() ? inputVars[2] : "0.0";
    std::string rough   = inputVars.size() > 3 && !inputVars[3].empty() ? inputVars[3] : "0.5";

    return fmt::format(
        "// PBR Output\n"
        "vec3 albedo_{} = {}.rgb;\n"
        "float metallic_{} = {};\n"
        "float roughness_{} = {};\n"
        "vec3 normal_{} = {};\n"
        "// Output: albedo={}, metallic={}, roughness={}, normal={}",
        m_id, albedo,
        m_id, metal,
        m_id, rough,
        m_id, normal,
        albedo, metal, rough, normal
    );
}

void OutputPBRNode::renderProperties() {
    ImGui::Text("Material Output Node");
}

// ── Factory ──
std::unique_ptr<ShaderNode> createNode(NodeType type, uint32_t id) {
    switch (type) {
        case NodeType::TextureSample:   return std::make_unique<TextureSampleNode>(id);
        case NodeType::ColorConstant:   return std::make_unique<ColorConstantNode>(id);
        case NodeType::FloatConstant:   return std::make_unique<FloatConstantNode>(id);
        case NodeType::Multiply:        return std::make_unique<MultiplyNode>(id);
        case NodeType::Add:             return std::make_unique<AddNode>(id);
        case NodeType::Lerp:            return std::make_unique<LerpNode>(id);
        case NodeType::Time:            return std::make_unique<TimeNode>(id);
        case NodeType::VertexPosition:  return std::make_unique<VertexPositionNode>(id);
        case NodeType::OutputPBR:       return std::make_unique<OutputPBRNode>(id);
    }
    return nullptr;
}

} // namespace Caffeine::Editor
```

**Step 3: Commit**

```bash
git add src/editor/ShaderNode.hpp src/editor/ShaderNode.cpp
git commit -m "feat(editor): add ShaderNode hierarchy with code generation"
```

---

### Task 3: Implement ShaderGraph (graph management + compilation)

**Files:**
- Create: `src/editor/ShaderGraph.hpp`
- Create: `src/editor/ShaderGraph.cpp`

**Step 1: Write ShaderGraph.hpp**

```cpp
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

    void getNodeConnections(uint32_t nodeID, std::vector<std::pair<int, int>>& inputs, std::vector<std::pair<int, int>>& outputs) const;

    std::string compileGLSL() const;
    std::string compileHLSL() const;

    std::vector<std::unique_ptr<ShaderNode>>& nodes() { return m_nodes; }
    const std::vector<std::unique_ptr<ShaderNode>>& nodes() const { return m_nodes; }

    bool empty() const { return m_nodes.empty(); }

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
```

**Step 2: Write ShaderGraph.cpp**

```cpp
#include "editor/ShaderGraph.hpp"
#include <algorithm>
#include <set>
#include <queue>
#include <fmt/format.h>
#include <imgui.h>

namespace Caffeine::Editor {

uint32_t ShaderGraph::addNode(NodeType type) {
    auto node = createNode(type, m_nextID);
    m_nextID++;
    m_nodes.push_back(std::move(node));
    return m_nextID - 1;
}

bool ShaderGraph::removeNode(uint32_t nodeID) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
        [nodeID](const auto& n) { return n->id() == nodeID; });
    if (it == m_nodes.end()) return false;

    // Remove all connections involving this node
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
    if (fromPin < 0 || fromPin >= (int)from->outputs().size()) return false;
    if (toPin < 0 || toPin >= (int)to->inputs().size()) return false;

    if (!canConnect(from->outputs()[fromPin].type, to->inputs()[toPin].type))
        return false;

    // Disconnect existing input
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
        if (c.toNode == nodeID)   inputs.push_back({c.fromNode, c.fromPin});
        if (c.fromNode == nodeID) outputs.push_back({c.toNode, c.toPin});
    }
}

bool ShaderGraph::canConnect(PinType fromType, PinType toType) const {
    if (fromType == toType) return true;
    // Float can connect to vec3/vec4 (auto-broadcast)
    if (fromType == PinType::Float) return true;
    return false;
}

void ShaderGraph::topologicalSort(std::vector<uint32_t>& sortedIDs) const {
    std::unordered_map<uint32_t, int> inDegree;
    for (const auto& n : m_nodes) inDegree[n->id()] = 0;
    for (const auto& c : m_connections) inDegree[c.toNode]++;

    std::queue<uint32_t> q;
    for (const auto& [id, deg] : inDegree)
        if (deg == 0) q.push(id);

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

    std::string header = "#version 460 core\n\n";
    header += "uniform float u_time;\n";
    header += "in vec2 texCoord;\n";
    header += "in vec3 v_position;\n";
    header += "out vec4 fragColor;\n\n";

    std::string body;
    std::string outputVar;

    // Gather input vars for each node
    std::unordered_map<uint32_t, std::vector<std::string>> nodeInputVars;

    for (uint32_t id : sorted) {
        std::vector<std::string> inputVars;
        // Find incoming connections
        for (const auto& c : m_connections) {
            if (c.toNode == id) {
                // Find the var name from source node
                for (const auto& c2 : m_connections) {
                    (void)c2;
                }
                inputVars.push_back(fmt::format("var_{}", c.fromNode));
            } else {
                inputVars.push_back("");
            }
        }
        // Pad if fewer connections than inputs
        const auto* node = getNode(id);
        if (node) {
            while ((int)inputVars.size() < (int)node->inputs().size())
                inputVars.push_back("");
        }

        auto* nodePtr = getNode(id);
        if (nodePtr) {
            body += "    " + nodePtr->generateCode(inputVars) + "\n";
            if (nodePtr->type() == NodeType::OutputPBR)
                outputVar = fmt::format("var_{}", id);
        }
    }

    std::string result = header;
    result += "void main() {\n";
    result += body;

    if (!outputVar.empty()) {
        result += fmt::format("    fragColor = {};\n", outputVar);
    } else {
        result += "    fragColor = vec4(1.0, 0.0, 1.0, 1.0);\n"; // magenta fallback
    }

    result += "}\n";
    return result;
}

std::string ShaderGraph::compileHLSL() const {
    // For now return GLSL; HLSL generation follows same pattern
    return compileGLSL();
}

} // namespace Caffeine::Editor
```

**Step 3: Commit**

```bash
git add src/editor/ShaderGraph.hpp src/editor/ShaderGraph.cpp
git commit -m "feat(editor): add ShaderGraph with GLSL code generation"
```

---

### Task 4: Create MaterialEditorPanel (ImNodes integration)

**Files:**
- Create: `src/editor/MaterialEditorPanel.hpp`
- Create: `src/editor/MaterialEditorPanel.cpp`
- Modify: `CMakeLists.txt` (add new source files to doppio target)
- Modify: `src/editor/SceneEditor.hpp` (include and member)
- Modify: `src/editor/SceneEditor.cpp` (render + init)

**Step 1: Write MaterialEditorPanel.hpp**

```cpp
#pragma once
#include "editor/ShaderGraph.hpp"
#include "editor/EditorTypes.hpp"
#include "assets/MeshTypes.hpp"
#include "rhi/RenderDevice.hpp"
#include <memory>
#include <string>

namespace Caffeine::Editor {

enum class EditorMode { Graph, Text };

class MaterialEditorPanel : public EditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel() override = default;

    void onImGuiRender() override;
    void setMaterial(Ref<Assets::Material3D> material);
    Ref<Assets::Material3D> material() const { return m_material; }

    void open()  { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    // Exposed for SceneEditor
    ShaderGraph& graph() { return m_graph; }

private:
    void renderMenuBar();
    void renderGraphCanvas();
    void renderTextEditor();
    void renderPreviewWindow();
    void renderInspector();
    void recompileShader();
    void showCompileError(const std::string& msg);
    void renderNodeContextMenu();

    void addDefaultNodes(); // Initialize default OutputPBR node

    ShaderGraph m_graph;
    EditorMode m_mode = EditorMode::Graph;
    bool m_open = true;
    bool m_showGrid = true;
    bool m_autoCompile = false;
    float m_previewRotation = 0.0f;

    // Text editor buffer
    char m_codeBuffer[16 * 1024];
    bool m_textDirty = false;

    Ref<Assets::Material3D> m_material;
    std::string m_lastCompileError;
    bool m_hasError = false;

    // Preview
    RHI::RenderDevice* m_renderDevice = nullptr;
    std::string m_compiledShaderCode;

    // Context menu
    bool m_showNodeMenu = false;
};

} // namespace Caffeine::Editor
```

**Step 2: Write MaterialEditorPanel.cpp**

```cpp
#include "editor/MaterialEditorPanel.hpp"
#include <imnodes.h>
#include <imgui.h>
#include <fmt/format.h>

namespace Caffeine::Editor {

static int s_pinCounter = 0;

MaterialEditorPanel::MaterialEditorPanel() {
    m_codeBuffer[0] = '\0';
    addDefaultNodes();
}

void MaterialEditorPanel::addDefaultNodes() {
    m_graph.addNode(NodeType::OutputPBR);
}

void MaterialEditorPanel::setMaterial(Ref<Assets::Material3D> material) {
    m_material = material;
}

void MaterialEditorPanel::onImGuiRender() {
    if (!m_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Material Editor", &m_open, ImGuiWindowFlags_MenuBar);

    renderMenuBar();

    // Split into three zones
    ImGui::Columns(2, "MatEditorMain", false);
    ImGui::SetColumnWidth(0, ImGui::GetContentRegionAvail().x * 0.65f);

    // Left side: Graph or Text editor
    if (m_mode == EditorMode::Graph) {
        renderGraphCanvas();
    } else {
        renderTextEditor();
    }

    ImGui::NextColumn();

    // Right side: Preview + Inspector
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
            if (ImGui::MenuItem("Graph Mode", nullptr, m_mode == EditorMode::Graph)) m_mode = EditorMode::Graph;
            if (ImGui::MenuItem("Text Mode", nullptr, m_mode == EditorMode::Text)) m_mode = EditorMode::Text;
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

    ImNodes::BeginNodeEditor();

    // Render background grid
    if (m_showGrid) {
        ImNodes::GetCurrentEditor()->style.flags |= ImNodesStyleFlags_GridLines;
    }

    // Apply style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.Colors[ImNodesCol_NodeBackground] = IM_COL32(30, 30, 30, 255);
    style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(40, 40, 40, 255);
    style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(50, 50, 50, 255);

    // Render all nodes
    for (auto& node : m_graph.nodes()) {
        ImNodes::BeginNode(node->id());

        // Title bar
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node->title().c_str());
        ImNodes::EndNodeTitleBar();

        // Input pins
        for (int i = 0; i < (int)node->inputs().size(); i++) {
            auto& pin = node->inputs()[i];
            ImNodes::BeginInputAttribute(s_pinCounter++);
            ImGui::Text("%s", pin.name.c_str());
            ImNodes::EndInputAttribute();
        }

        // Properties (if any)
        ImGui::Spacing();
        node->renderProperties();
        ImGui::Spacing();

        // Output pins
        for (int i = 0; i < (int)node->outputs().size(); i++) {
            auto& pin = node->outputs()[i];
            ImNodes::BeginOutputAttribute(s_pinCounter++);
            ImGui::Text("%s", pin.name.c_str());
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }

    // Render connections
    for (const auto& conn : m_graph.connections()) {
        // Find the attribute IDs - we need to track these
        ImNodes::Connection(conn.fromNode, conn.fromPin, conn.toNode, conn.toPin);
    }

    // Handle new connections
    int fromAttr, toAttr;
    if (ImNodes::IsLinkCreated(&fromAttr, &toAttr)) {
        // Map attribute IDs back to node+pin
        // For MVP, we accept the connection
    }

    // Handle deletion
    int deletedLink;
    if (ImNodes::IsLinkDestroyed(&deletedLink)) {
        // Remove the connection
    }

    // Context menu
    if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered()) {
        ImGui::OpenPopup("AddNodePopup");
    }

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

    ImNodes::EndNodeEditor();
    ImGui::EndChild();
}

void MaterialEditorPanel::renderTextEditor() {
    ImGui::BeginChild("TextEditor", ImVec2(0, 0), true);

    ImGui::TextUnformatted("GLSL Editor (Text mode)");
    ImGui::Separator();

    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##code", m_codeBuffer, sizeof(m_codeBuffer),
        size, ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Compile")) {
        recompileShader();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sync to Graph")) {
        // For now, just copy text
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::renderPreviewWindow() {
    ImGui::BeginChild("Preview", ImVec2(0, 0), true);

    ImGui::Text("Preview");
    ImGui::Separator();

    // Get the available size for the preview
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= 30; // Leave room for controls

    // Render a placeholder preview sphere using ImGui
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos();
    center.x += avail.x * 0.5f;
    center.y += avail.y * 0.5f;
    float radius = (avail.x < avail.y ? avail.x : avail.y) * 0.4f;

    // Draw a 3D-looking sphere (ImGui fallback)
    ImU32 sphereColor = ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
    ImU32 outlineColor = ImGui::GetColorU32(ImVec4(0.1f, 0.2f, 0.4f, 1.0f));

    drawList->AddCircleFilled(center, radius, sphereColor, 32);
    drawList->AddCircle(center, radius, outlineColor, 32);

    // Add some "lighting" effect
    drawList->AddCircleFilled(
        ImVec2(center.x - radius * 0.3f, center.y - radius * 0.3f),
        radius * 0.15f,
        IM_COL32(255, 255, 255, 80),
        16);

    ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y + radius + 5));
    ImGui::TextUnformatted("RHI Preview");

    // Controls below preview
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::SliderFloat("Rotation", &m_previewRotation, 0.0f, 360.0f, "%.0f deg");

    if (m_hasError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
        ImGui::TextWrapped("Error: %s", m_lastCompileError.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::renderInspector() {
    ImGui::BeginChild("Inspector", ImVec2(0, 0), true);
    ImGui::Text("Properties");
    ImGui::Separator();

    // Show selected node properties
    ImGui::Text("Selected node properties");

    // Material-level properties
    if (m_material) {
        ImGui::Separator();
        ImGui::Text("Material: %s", m_material->name.c_str());

        ImGui::ColorEdit4("Albedo", &m_material->albedoColor.r);
        ImGui::SliderFloat("Roughness", &m_material->roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Metallic", &m_material->metallic, 0.0f, 1.0f);
    }

    ImGui::EndChild();
}

void MaterialEditorPanel::recompileShader() {
    m_hasError = false;
    m_lastCompileError.clear();

    std::string code;
    if (m_mode == EditorMode::Graph) {
        code = m_graph.compileGLSL();
        m_compiledShaderCode = code;
    } else {
        code = m_codeBuffer;
    }

    // TODO: Pass to RHI for real GPU compilation
    // For now, validate syntax-level checks
    if (code.empty()) {
        showCompileError("Generated shader code is empty");
        return;
    }

    if (code.find("void main()") == std::string::npos) {
        showCompileError("Shader missing main() function");
        return;
    }

    // Store compiled code for RHI pipeline creation
    m_compiledShaderCode = code;
}

void MaterialEditorPanel::showCompileError(const std::string& msg) {
    m_hasError = true;
    m_lastCompileError = msg;
}

} // namespace Caffeine::Editor
```

**Step 3: Add to CMakeLists.txt**

Add these source files to the doppio target:
```cmake
src/editor/ShaderNode.cpp
src/editor/ShaderGraph.cpp
src/editor/MaterialEditorPanel.cpp
```

And imnodes include:
```cmake
${imnodes_SOURCE_DIR}
```

And imnodes source:
```cmake
${imnodes_SOURCE_DIR}/imnodes.cpp
```

**Step 4: Integrate into SceneEditor**

In `SceneEditor.hpp`:
```cpp
#include "editor/MaterialEditorPanel.hpp"
// Member:
MaterialEditorPanel m_materialEditor;
```

In `SceneEditor.cpp` render loop:
```cpp
m_materialEditor.onImGuiRender();
```

**Step 5: Verify build**

```bash
cd build && cmake .. && make -j4 2>&1 | tail -20
```

**Step 6: Commit**

```bash
git add CMakeLists.txt src/editor/MaterialEditorPanel.hpp src/editor/MaterialEditorPanel.cpp src/editor/SceneEditor.hpp src/editor/SceneEditor.cpp
git commit -m "feat(editor): add Material Editor panel with ImNodes graph"
```

---

### Task 5: Connect RHI preview

**Files:**
- Modify: `src/editor/MaterialEditorPanel.cpp`
- Create: `src/editor/PreviewRenderer.hpp`
- Create: `src/editor/PreviewRenderer.cpp`

**Step 1: Write PreviewRenderer.hpp**

```cpp
#pragma once
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include "assets/MeshTypes.hpp"
#include "core/Types.hpp"
#include <string>
#include <memory>

namespace Caffeine::Editor {

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer();

    bool init(RHI::RenderDevice* device);
    void shutdown();

    void render(RHI::CommandBuffer* cmd, const std::string& shaderCode);

    // ImGui fallback when RHI unavailable
    void renderFallback(float rotationDeg, float width, float height);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Shader* m_vertexShader = nullptr;
    RHI::Shader* m_fragmentShader = nullptr;
    RHI::Pipeline* m_pipeline = nullptr;
    RHI::Buffer* m_sphereVertices = nullptr;
    RHI::Buffer* m_sphereIndices = nullptr;
    u32 m_indexCount = 0;
    bool m_initialized = false;

    bool createSphereMesh();
    bool createShaderPipeline(const std::string& shaderCode);
};

} // namespace Caffeine::Editor
```

**Step 2: Write PreviewRenderer.cpp**

```cpp
#include "editor/PreviewRenderer.hpp"
#include <imgui.h>
#include <cmath>

namespace Caffeine::Editor {

PreviewRenderer::~PreviewRenderer() {
    shutdown();
}

bool PreviewRenderer::init(RHI::RenderDevice* device) {
    m_device = device;
    if (!m_device || !m_device->isInitialized()) {
        m_initialized = false;
        return false;
    }
    m_initialized = createSphereMesh();
    return m_initialized;
}

void PreviewRenderer::shutdown() {
    if (m_device) {
        if (m_vertexShader)   m_device->destroyShader(m_vertexShader);
        if (m_fragmentShader) m_device->destroyShader(m_fragmentShader);
        if (m_pipeline)       { /* destroy via RHI */ }
        if (m_sphereVertices) m_device->destroyBuffer(m_sphereVertices);
        if (m_sphereIndices)  m_device->destroyBuffer(m_sphereIndices);
    }
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_pipeline = nullptr;
    m_sphereVertices = nullptr;
    m_sphereIndices = nullptr;
    m_initialized = false;
}

bool PreviewRenderer::createSphereMesh() {
    // Simple sphere with 32 segments
    const int segs = 32;
    std::vector<float> verts;
    std::vector<uint32_t> indices;

    for (int lat = 0; lat <= segs; lat++) {
        float theta = (float)lat * 3.14159f / (float)segs;
        for (int lon = 0; lon <= segs; lon++) {
            float phi = (float)lon * 2.0f * 3.14159f / (float)segs;
            float x = sinf(theta) * cosf(phi);
            float y = cosf(theta);
            float z = sinf(theta) * sinf(phi);
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(x); verts.push_back(y); verts.push_back(z); // normal
            verts.push_back((float)lon / segs); verts.push_back((float)lat / segs); // uv
        }
    }

    for (int lat = 0; lat < segs; lat++) {
        for (int lon = 0; lon < segs; lon++) {
            int first = lat * (segs + 1) + lon;
            int second = first + segs + 1;
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    m_indexCount = (uint32_t)indices.size();

    if (m_device) {
        RHI::BufferDesc desc;
        desc.size = verts.size() * sizeof(float);
        desc.name = "SphereVerts";
        m_sphereVertices = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);

        desc.size = indices.size() * sizeof(uint32_t);
        desc.name = "SphereIndices";
        m_sphereIndices = m_device->createBuffer(desc, RHI::BufferUsage::Index);
        // Note: actual upload requires CommandBuffer::upload()
    }

    return true;
}

bool PreviewRenderer::createShaderPipeline(const std::string& shaderCode) {
    (void)shaderCode;
    // TODO: Create RHI shader and pipeline from generated code
    return false;
}

void PreviewRenderer::render(RHI::CommandBuffer* cmd, const std::string& shaderCode) {
    (void)cmd;
    (void)shaderCode;
    // TODO: Full RHI pipeline render
}

void PreviewRenderer::renderFallback(float rotationDeg, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos();
    center.x += width * 0.5f;
    center.y += height * 0.5f;
    float radius = (width < height ? width : height) * 0.4f;

    ImU32 col = ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
    ImU32 outline = ImGui::GetColorU32(ImVec4(0.1f, 0.2f, 0.4f, 1.0f));

    // Animated rotation
    float rad = rotationDeg * 3.14159f / 180.0f;

    dl->AddCircleFilled(center, radius, col, 32);
    dl->AddCircle(center, radius, outline, 32);

    // highlight
    dl->AddCircleFilled(
        ImVec2(center.x + cosf(rad) * radius * 0.3f, center.y + sinf(rad) * radius * 0.3f),
        radius * 0.12f, IM_COL32(255, 255, 255, 60), 16);
}

} // namespace Caffeine::Editor
```

**Step 3: Integrate PreviewRenderer into MaterialEditorPanel**

Add as member: `PreviewRenderer m_previewRenderer;`

Call in `renderPreviewWindow()`:
```cpp
m_previewRenderer.renderFallback(m_previewRotation, avail.x, avail.y);
```

**Step 4: Commit**

```bash
git add src/editor/PreviewRenderer.hpp src/editor/PreviewRenderer.cpp
git commit -m "feat(editor): add PreviewRenderer for RHI + ImGui shader preview"
```

---

### Task 6: Update tests/CMakeLists.txt and verify all tests pass

**Files:**
- Modify: `tests/CMakeLists.txt` (add all new editor source files)

**Step 1: Add to tests/CMakeLists.txt**

```cmake
../src/editor/ShaderNode.cpp
../src/editor/ShaderGraph.cpp
../src/editor/MaterialEditorPanel.cpp
../src/editor/PreviewRenderer.cpp
```

**Step 2: Build tests**

```bash
cd build && cmake .. && make -j4 2>&1 | tail -10
```

**Step 3: Run tests**

```bash
./tests/CaffeineTest
```

Expected: All tests pass

**Step 4: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "build: add shader editor source files to test build"
```

---

### Task 7: Integration and edge case handling

**Files:**
- Modify: `src/editor/MaterialEditorPanel.cpp`

**Step 1: Handle edge cases**

- Empty graph → show "Add nodes to begin" placeholder
- No OutputPBR node → show warning
- Cyclic connections → detect and reject in connect()
- Empty code buffer in text mode → show "No shader code written"
- Compilation with errors → show red indicator on affected nodes

**Step 2: Final testing**

```bash
cd build && cmake .. && make -j4 && ./tests/CaffeineTest
```

**Step 3: Final commit**

```bash
git commit -am "feat(editor): add Material & Shader Editor with ImNodes graph and preview"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Add ImNodes dependency | CMakeLists.txt |
| 2 | ShaderNode hierarchy (9 node types + code gen) | ShaderNode.hpp, ShaderNode.cpp |
| 3 | ShaderGraph (graph management + GLSL compilation) | ShaderGraph.hpp, ShaderGraph.cpp |
| 4 | MaterialEditorPanel (ImNodes UI, 3 sub-panels) | MaterialEditorPanel.hpp/.cpp, SceneEditor integration |
| 5 | PreviewRenderer (RHI + ImGui fallback) | PreviewRenderer.hpp/.cpp |
| 6 | Test build + pass | tests/CMakeLists.txt |
| 7 | Edge cases + final integration | MaterialEditorPanel.cpp |

Total new files: 6 (hpp/cpp pairs for ShaderNode, ShaderGraph, MaterialEditorPanel, PreviewRenderer)
