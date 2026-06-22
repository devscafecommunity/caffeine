#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#include <memory>
#include <string>
#include <unordered_map>
#endif

namespace Caffeine::Editor {

class CameraPreviewPanel {
public:
    void onImGuiRender(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void open()         { m_open = true; }
    void close()        { m_open = false; }

private:
#ifdef CF_HAS_IMGUI
    void renderNoCamera(ImVec2 panelSize);
    void renderCameraView(ECS::World& world, EditorContext& ctx,
                          ImVec2 origin, ImVec2 panelSize,
                          float camX, float camY, float zoom);

    struct TexEntry {
        std::unique_ptr<ImTextureData> texture;
        int width  = 0;
        int height = 0;
        bool loadFailed = false;
    };
    std::unordered_map<std::string, TexEntry> m_texCache;

    std::string resolveSpritePath(const std::string& name, const EditorContext& ctx) const;
#endif

    bool m_open = true;
};

}
