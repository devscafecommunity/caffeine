#pragma once
#include "caffeine/ui/UI.hpp"
#include "core/Types.hpp"
#include "editor/EditorTypes.hpp"
#include "assets/AssetTypes.hpp"

#include <cstdio>

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

        using namespace Caffeine::UIKit;
        using namespace Caffeine::UIKit::Elements;

        Panel root("Stats", "", "", &m_open);
        root.setWindowFlags(ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImGui::SetNextWindowBgAlpha(0.65f);
        UIContext ui;

        char buffer[160];
        const TextTone fpsTone = stats.fps >= 55.0f ? TextTone::Success
                                : stats.fps >= 30.0f ? TextTone::Warning
                                                     : TextTone::Danger;
        std::snprintf(buffer, sizeof(buffer), "FPS: %.1f", stats.fps);
        root.emplace<Label>(buffer, fpsTone);
        std::snprintf(buffer, sizeof(buffer), "Frame: %.3f ms", stats.deltaTime * 1000.0);
        root.emplace<KeyValue>("Frame", buffer);
        std::snprintf(buffer, sizeof(buffer), "%llu",
                      static_cast<unsigned long long>(stats.frameCount));
        root.emplace<KeyValue>("Frames", buffer);

        root.emplace<Separator>();
        std::snprintf(buffer, sizeof(buffer), "%llu / %llu MB",
                      static_cast<unsigned long long>(cache.totalCachedBytes / (1024 * 1024)),
                      static_cast<unsigned long long>(cache.maxCacheBytes / (1024 * 1024)));
        root.emplace<KeyValue>("Cache", buffer);
        std::snprintf(buffer, sizeof(buffer), "Textures: %u  Audio: %u  Pending: %u",
                      cache.textureCount, cache.audioCount, cache.pendingJobs);
        root.emplace<Label>(buffer, TextTone::Muted);
        std::snprintf(buffer, sizeof(buffer), "Cache hit: %.1f%%", cache.cacheHitRate * 100.0f);
        root.emplace<Label>(buffer, TextTone::Muted);

        ui.draw(root);
    }
#endif

private:
    bool m_open = true;
};

}  // namespace Caffeine::Editor
