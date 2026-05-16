# Material & Shader Editor - Design Document

**Date:** 2026-05-16  
**Issue:** #122  
**Milestone:** M4 — Advanced Tools & Polish  
**Status:** Approved

---

## Overview

The Material & Shader Editor is a visual and code-based tool for creating and editing shaders in the Caffeine Studio. It provides two complementary workflows:

1. **Visual Graph Mode** - Drag-and-drop node editor using ImNodes for artists
2. **Text Mode** - Direct GLSL/HLSL code editing for technical artists

The editor integrates with the RHI (Rendering Hardware Interface) for real-time 3D preview on GPU, with ImGui fallback for wireframe visualization.

---

## Architecture

### Core Data Model

```
MaterialEditorPanel (EditorPanel)
├── ShaderGraph m_activeGraph
│   ├── std::vector<Ref<ShaderNode>> m_nodes
│   └── std::vector<std::pair<int,int>> m_connections
├── char m_codeBuffer[16KB]  // Text mode buffer
├── Ref<Material> m_currentMaterial
└── PreviewRenderer m_preview
    ├── RHI::Pipeline* m_pipeline
    └── Assets::Mesh3D* m_previewMesh
```

### Component Breakdown

#### 1. ShaderNode Hierarchy

**Base Class:** `ShaderNode`
- `uint32_t ID` - Unique identifier for graph tracking
- `std::string title` - Display name
- `NodeType type` - Enum discriminating node kind
- `std::vector<NodePin> inputs` - Input pins with type info
- `std::vector<NodePin> outputs` - Output pins
- Pure virtual `generateCode()` - Subclass-specific code generation

**Concrete Nodes:**

| Node Type | Purpose | Inputs | Outputs |
|-----------|---------|--------|---------|
| TextureSample | Load texture from asset | Texture asset | RGBA |
| ColorConstant | Static color value | (none) | RGBA |
| FloatConstant | Static scalar | (none) | Float |
| Multiply | Multiply two values | Input A, Input B | Output |
| Add | Add two values | Input A, Input B | Output |
| Lerp | Linear interpolation | From, To, T | Output |
| Time | Elapsed time | (none) | Float |
| VertexPosition | Vertex position in world space | (none) | Vec3 |
| OutputPBR | Final material output | Albedo, Normal, Metallic, Roughness | (pipeline output) |

#### 2. ShaderGraph

Manages nodes and connections, orchestrates compilation:

```cpp
class ShaderGraph {
public:
    uint32_t addNode(NodeType type);
    void removeNode(uint32_t nodeID);
    void connect(uint32_t fromNode, int fromPin, uint32_t toNode, int toPin);
    void disconnect(uint32_t fromNode, int fromPin);
    
    std::string compileGLSL();  // Generate GLSL source
    std::string compileHLSL();  // Generate HLSL source
    
private:
    std::vector<Ref<ShaderNode>> m_nodes;
    std::vector<std::pair<int, int>> m_connections;
};
```

#### 3. MaterialEditorPanel

Main editor UI with three sub-panels:

```cpp
class MaterialEditorPanel : public EditorPanel {
public:
    void onImGuiRender() override;
    
private:
    void renderGraphCanvas();      // ImNodes canvas
    void renderPreviewWindow();    // 3D preview or fallback
    void renderInspector();        // Property sliders
    void recompileShader();        // Trigger compilation
    void displayCompileErrors();   // Show error messages
    
    ShaderGraph m_activeGraph;
    char m_codeBuffer[16 * 1024];
    Ref<Material> m_currentMaterial;
};
```

#### 4. PreviewRenderer

Handles both RHI-based 3D rendering and ImGui fallback:

```cpp
class PreviewRenderer {
public:
    bool init(RHI::RenderDevice* device);
    void render(RHI::CommandBuffer* cmd, const std::string& shaderCode);
    void renderFallback();  // ImGui wireframe fallback
    
private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Pipeline* m_pipeline = nullptr;
    Assets::Mesh3D* m_previewMesh = nullptr;  // Sphere/Cube
};
```

---

## Data Flow

### Compilation Pipeline

```
User edits graph/text
         ↓
ShaderGraph::compileGLSL() / compileHLSL()
         ↓
glslang validation (optional)
         ↓
RHI::RenderDevice::createShader()
         ↓
RHI::Pipeline creation
         ↓
Preview re-render with new shader
         ↓
Display on preview window (GPU or ImGui fallback)
```

### Connection Validation

Type checking occurs at connection time:
- `vec3` → `vec3` ✓
- `vec3` → `float` ✗ (prevented)
- Incompatible connections shown with red visual indicator

---

## Error Handling

1. **Compilation Errors** - Caught from shader compiler, displayed inline
2. **Validation Errors** - Type mismatches, missing required inputs
3. **Runtime Errors** - GPU pipeline creation failures logged to console

Invalid nodes visually indicate issues (red border, error tooltip).

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **ImNodes integration** | Mature, widely-used node library; familiar to artists from Unreal/Blender |
| **Dual graph + text mode** | Accommodates both visual-first artists and code-focused technical artists |
| **RHI + ImGui fallback** | GPU preview when available, graceful degradation to wireframe in headless/fallback mode |
| **Background compilation** | Prevents UI freeze during shader compilation |
| **Material3D reuse** | Leverage existing PBR material struct from `src/assets/MeshTypes.hpp` |

---

## Acceptance Criteria

- [x] Design approved
- [ ] Visual node graph with ImNodes (drag-and-drop connections)
- [ ] All 9 node types implemented with code generation
- [ ] Text editor with syntax highlighting
- [ ] Real-time preview on sphere mesh (RHI + ImGui fallback)
- [ ] Property inspector with sliders for exposed parameters
- [ ] Compilation error display with line/node references
- [ ] Save/load materials to .caf format
- [ ] All tests passing

---

## Dependencies

- **ImNodes** - Node graph UI library (via FetchContent)
- **glslang** - Shader compilation validation (optional, can use RHI directly)
- **RHI (existing)** - GPU pipeline management
- **Assets (existing)** - Material3D struct, mesh generation

---

## Next Steps

1. Implementation plan created
2. Implement ShaderNode hierarchy
3. Implement ShaderGraph compilation
4. Integrate ImNodes into MaterialEditorPanel
5. Connect RHI for preview rendering
6. Add error handling and validation
7. Testing and integration into SceneEditor

