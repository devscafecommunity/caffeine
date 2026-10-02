#pragma once
#include "assets/MaterialTypes.hpp"
#include "editor/EditorContext.hpp"
#include <filesystem>
#include <string>

#ifdef CF_HAS_SDL3
#include "render/MaterialPreviewRenderer.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"
#endif

namespace Caffeine::Editor {

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    void onImGuiRender(EditorContext& ctx);
    bool openFromPath(const std::filesystem::path& path);
    void setProjectRoot(const std::string& projectRoot) { m_projectRoot = projectRoot; }

    void open()  { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

#ifdef CF_HAS_SDL3
    void initGpu(RHI::RenderDevice* device);
    void shutdownGpu();
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
#endif

private:
    void renderProperties(EditorContext& ctx);
    void renderPresetPicker();
    void renderPreview(const EditorContext& ctx, float width, float height);
    bool saveCurrent();
    void publishLive();
    std::filesystem::path resolvedPath(const std::filesystem::path& path) const;

    bool m_open = true;
    float m_previewRotation = 0.0f;
    float m_previewPitch = 12.0f;
    bool m_previewFloor = true;
    Assets::MaterialSurface m_surface;
    std::filesystem::path m_materialPath;
    std::string m_projectRoot;
    std::string m_status;

#ifdef CF_HAS_SDL3
    RHI::CommandBuffer* m_frameCmd = nullptr;
    Render::MaterialPreviewRenderer m_previewRenderer;
#endif
};

} // namespace Caffeine::Editor
