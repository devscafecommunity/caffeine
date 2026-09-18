#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

class AssetPreviewRenderer {
public:
    void renderVisual(const std::filesystem::path& path, AssetType type, const std::string& projectRoot,
                      ImVec2 size);
    void invalidate();

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

    std::string m_cachedKey;
    ImageEntry m_image;
    WaveformEntry m_waveform;

    void drawPreviewFrame(ImVec2 size);
    void renderImagePreview(const std::filesystem::path& path, ImVec2 size);
    void renderMeshPreview(const std::filesystem::path& path, const std::string& projectRoot, ImVec2 size);
    void renderAudioPreview(const std::filesystem::path& path, ImVec2 size);
    void renderScenePreview(const std::filesystem::path& path, ImVec2 size);
    void renderFallbackIcon(AssetType type, const std::filesystem::path& path, ImVec2 size);

    bool ensureImageLoaded(const std::filesystem::path& path);
    bool ensureWaveformLoaded(const std::filesystem::path& path);
};

#endif

}  // namespace Caffeine::Editor
