#pragma once
#include "editor/ShaderGraph.hpp"
#include "editor/PreviewRenderer.hpp"
#include "assets/MeshTypes.hpp"
#include <string>

namespace Caffeine::Editor {

enum class EditorMode { Graph, Text };

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    void onImGuiRender();

    void setMaterial(Assets::Material3D* material) { m_material = material; }
    Assets::Material3D* material() const { return m_material; }

    void open()  { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    ShaderGraph& graph() { return m_graph; }

private:
    void renderMenuBar();
    void renderGraphCanvas();
    void renderTextEditor();
    void renderPreviewWindow();
    void renderInspector();
    void recompileShader();
    void renderNodeContextMenu();
    void addDefaultNodes();

    ShaderGraph m_graph;
    EditorMode m_mode = EditorMode::Graph;
    bool m_open = true;
    bool m_showGrid = true;
    bool m_autoCompile = false;
    float m_previewRotation = 0.0f;
    PreviewRenderer m_previewRenderer;

    char m_codeBuffer[16 * 1024];
    bool m_textDirty = false;

    Assets::Material3D* m_material = nullptr;
    std::string m_lastCompileError;
    bool m_hasError = false;
    std::string m_compiledShaderCode;
};

} // namespace Caffeine::Editor
