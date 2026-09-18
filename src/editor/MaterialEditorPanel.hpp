#pragma once
#include "editor/ShaderGraph.hpp"
#include "editor/MaterialSerializer.hpp"
#include "editor/PreviewRenderer.hpp"
#include "assets/MeshTypes.hpp"
#ifdef CF_HAS_SDL3
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"
#endif
#include <imgui.h>
#include <filesystem>
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
    bool openFromPath(const std::filesystem::path& path);

    void open()  { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    ShaderGraph& graph() { return m_graph; }

#ifdef CF_HAS_SDL3
    void initGpu(RHI::RenderDevice* device);
    void shutdownGpu();
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
#endif

private:
    void renderMenuBar();
    void renderGraphCanvas(ImVec2 size);
    void renderTextEditor(ImVec2 size);
    void renderPreviewWindow(float height);
    void renderInspector(float height);
    void recompileShader();
    void renderNodeContextMenu();
    void addDefaultNodes();
    bool saveCurrent();
    MaterialDocument buildDocument() const;

    ShaderGraph m_graph;
    EditorMode m_mode = EditorMode::Graph;
    bool m_open = true;
    bool m_showGrid = true;
    bool m_autoCompile = true;
    float m_previewRotation = 0.0f;
    PreviewRenderer m_previewRenderer;

    char m_codeBuffer[16 * 1024];
    bool m_textDirty = false;

    Assets::Material3D m_ownedMaterial;
    Assets::Material3D* m_material = nullptr;
    std::filesystem::path m_materialPath;
    EvaluatedMaterial m_evaluated;
    std::string m_lastCompileError;
    bool m_hasError = false;
    std::string m_compiledShaderCode;

#ifdef CF_HAS_SDL3
    RHI::CommandBuffer* m_frameCmd = nullptr;
#endif
};

} // namespace Caffeine::Editor
