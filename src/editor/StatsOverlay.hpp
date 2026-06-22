#pragma once
#include "core/Types.hpp"
#include "editor/EditorTypes.hpp"
#include "assets/AssetTypes.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class StatsOverlay {
public:
    StatsOverlay() = default;

    bool isOpen()  const { return m_open; }
    void close()   { m_open = false; }
    void open()    { m_open = true; }

#ifdef CF_HAS_IMGUI
    void render(const FrameStats& stats, const Assets::CacheStats& cache) {
        if (!m_open) return;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove;
        ImGui::SetNextWindowBgAlpha(0.65f);
        if (ImGui::Begin("Stats", &m_open, flags)) {
            ImGui::Text("FPS: %.1f", stats.fps);
            ImGui::Text("Frame: %.3f ms", stats.deltaTime * 1000.0);
            ImGui::Text("Frames: %llu", static_cast<unsigned long long>(stats.frameCount));
            ImGui::Separator();
            ImGui::Text("Cache: %llu / %llu MB",
                static_cast<unsigned long long>(cache.totalCachedBytes / (1024*1024)),
                static_cast<unsigned long long>(cache.maxCacheBytes    / (1024*1024)));
            ImGui::Text("Textures: %u  Audio: %u  Pending: %u",
                cache.textureCount, cache.audioCount, cache.pendingJobs);
            ImGui::Text("Cache hit: %.1f%%", cache.cacheHitRate * 100.0f);
        }
        ImGui::End();
    }
#endif

private:
    bool m_open = true;
};

}  // namespace Caffeine::Editor
