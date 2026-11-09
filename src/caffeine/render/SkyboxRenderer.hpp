#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#endif

namespace Caffeine::Render {

#ifdef CF_HAS_IMGUI

struct SkyboxCamera {
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    f32  fovY = 1.0472f;
    f32  aspect = 1.0f;

    bool nearlyEqual(const SkyboxCamera& other, f32 epsilon = 0.0015f) const;
};

class SkyboxRenderer {
public:
    bool draw(ImDrawList* drawList, ImVec2 origin, ImVec2 panelSize,
              const SkyboxCamera& camera, const std::string& texturePath,
              int maxRasterDim = 1024);
    void releaseGpuTextures();

private:
    struct SourceImage {
        std::vector<u8> pixels;
        u32 width = 0;
        u32 height = 0;
        bool loaded = false;
    };

    struct FrameCache {
        std::vector<u8> pixels;
        ImVec2 origin{};
        int panelW = 0;
        int panelH = 0;
        int renderW = 0;
        int renderH = 0;
        SkyboxCamera camera{};
        std::string texturePath;
        bool valid = false;
        bool gpuDirty = false;
    };

    struct GpuTexture {
        std::unique_ptr<ImTextureData> texture;
        std::string sourcePath;
        int width = 0;
        int height = 0;
    };

    void destroyGpuTexture(GpuTexture& gpu);

    bool loadSource(const std::string& path, SourceImage& out);
    void renderPixels(SourceImage& source, FrameCache& frame, ImVec2 origin, ImVec2 panelSize,
                      const SkyboxCamera& camera, int maxRasterDim);
    void blitFrame(ImDrawList* drawList, FrameCache& frame, GpuTexture& gpu);

    std::unordered_map<std::string, SourceImage> m_sources;
    FrameCache m_frame;
    GpuTexture m_gpu;
    u32 m_settledFrames = 0;
};

#endif

}  // namespace Caffeine::Render
