#pragma once

#include "editor/EditorContext.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
#ifdef CF_HAS_SDL3
#include "editor/ImGuiGpuTexture.hpp"
#endif
#endif

namespace Caffeine::Editor {

class ImageManagerPanel {
public:
    ImageManagerPanel() = default;
    ~ImageManagerPanel();

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void openPath(const std::filesystem::path& path);
    void onImGuiRender(EditorContext& ctx);

private:
    void rebuildGpu();
    void flipHorizontal();
    void flipVertical();
    bool savePng(const std::filesystem::path& path) const;

    bool m_open = false;
    std::filesystem::path m_path;
    std::vector<unsigned char> m_pixels;
    int m_width = 0;
    int m_height = 0;
    bool m_showR = true;
    bool m_showG = true;
    bool m_showB = true;
    bool m_showA = true;
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    std::string m_status;
    std::vector<std::filesystem::path> m_library;

#ifdef CF_HAS_SDL3
    std::unique_ptr<ImTextureData> m_texture;
    bool m_gpuDirty = false;
#endif
};

}  // namespace Caffeine::Editor
