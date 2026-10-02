#pragma once

#include "assets/MaterialTypes.hpp"
#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#ifdef CF_HAS_SDL3
#include "render/MaterialPreviewRenderer.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"
#include <unordered_map>
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

class AssetPreviewRenderer {
public:
    ~AssetPreviewRenderer();

    void renderVisual(const std::filesystem::path& path, AssetType type, const std::string& projectRoot,
                      ImVec2 size);
    void invalidate();
    void shutdownGpu();

#ifdef CF_HAS_SDL3
    void initGpu(RHI::RenderDevice* device);
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
    void setEnvironment(std::string path, f32 exposure);
    /// Records at most one thumbnail render for items that do not have one yet.
    void prepareThumbnails(const std::vector<std::pair<std::filesystem::path, AssetType>>& items,
                           const std::string& projectRoot);
    /// Draws a GPU or image thumbnail. Returns false when the thumbnail is not ready.
    bool drawThumbnail(const std::filesystem::path& path, AssetType type, float size);
#endif

private:
    struct ImageEntry {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
        bool failed = false;
    };

    struct WaveformEntry {
        std::vector<std::pair<float, float>> peaks;
        float durationSec = 0.0f;
        bool failed = false;
    };

    struct MaterialEntry {
        Assets::MaterialSurface surface;
        bool loaded = false;
        bool failed = false;
    };

    std::string m_cachedKey;
    ImageEntry m_image;
    WaveformEntry m_waveform;
    MaterialEntry m_material;

    void drawPreviewFrame(ImVec2 size);
    void renderMaterialPreview(const std::filesystem::path& path, const std::string& projectRoot,
                               ImVec2 size);
    void renderImagePreview(const std::filesystem::path& path, ImVec2 size);
    void renderMeshPreview(const std::filesystem::path& path, const std::string& projectRoot, ImVec2 size);
    void renderAudioPreview(const std::filesystem::path& path, ImVec2 size);
    void renderScenePreview(const std::filesystem::path& path, ImVec2 size);
    void renderFallbackIcon(AssetType type, const std::filesystem::path& path, ImVec2 size);

    bool ensureImageLoaded(const std::filesystem::path& path);
    bool ensureWaveformLoaded(const std::filesystem::path& path);
    bool ensureMaterialLoaded(const std::filesystem::path& path);

#ifdef CF_HAS_SDL3
    Render::MaterialPreviewSettings previewSettings(u32 pixelSize) const;
    bool drawGpuTexture(RHI::Texture* texture, ImVec2 size);
    void pumpThumbnail(const std::string& projectRoot);
    bool thumbnailReady(const std::string& key) const;

    struct GpuThumb {
        RHI::Texture* texture = nullptr;
        bool ready = false;
        bool failed = false;
    };
    struct ImageThumb {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
        bool ready = false;
        bool failed = false;
    };

    RHI::RenderDevice* m_device = nullptr;
    RHI::CommandBuffer* m_frameCmd = nullptr;
    Render::MaterialPreviewRenderer m_pane;
    Render::MaterialPreviewRenderer m_thumbs;
    std::string m_envPath;
    f32 m_envExposure = 1.0f;
    std::unordered_map<std::string, GpuThumb> m_gpuThumbs;
    std::unordered_map<std::string, ImageThumb> m_imageThumbs;
    std::vector<std::pair<std::filesystem::path, AssetType>> m_thumbItems;
    std::string m_activeThumbKey;
    bool m_activeThumbIsImage = false;
    bool m_activeThumbIsMesh = false;
#endif
};

#endif

}  // namespace Caffeine::Editor
