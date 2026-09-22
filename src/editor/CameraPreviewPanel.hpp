#pragma once
#include "core/Types.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include "editor/Camera2DPreviewRenderer.hpp"
#include "render/SkyboxRenderer.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#include <memory>
#include <string>
#include <unordered_map>
#endif

namespace Caffeine::Editor {

class SceneViewport;

class CameraPreviewPanel {
public:
    void onImGuiRender(ECS::World& world, EditorContext& ctx, SceneViewport& viewport);

    bool isOpen() const { return m_open; }
    void open()         { m_open = true; }
    void close()        { m_open = false; }

private:
#ifdef CF_HAS_IMGUI
    void renderNoCamera(ImVec2 panelSize);
    void renderCameraView(ECS::World& world, EditorContext& ctx,
                          ImVec2 origin, ImVec2 panelSize,
                          float camX, float camY, float zoom);
    void renderCamera3DView(ECS::World& world, EditorContext& ctx, SceneViewport& viewport,
                            ImVec2 origin, ImVec2 panelSize,
                            ECS::Entity cameraEntity,
                            ECS::Camera3DComponent& cam);
    void renderEditorCameraFallback(ECS::World& world, EditorContext& ctx, SceneViewport& viewport,
                                    ImVec2 origin, ImVec2 panelSize);

    std::unordered_map<std::string, Camera2DPreviewTextureEntry> m_texCache;
    Render::SkyboxRenderer m_skyboxRenderer;

    std::string resolveSpritePath(const std::string& name, const EditorContext& ctx) const;
#endif

    bool m_open = true;
    bool m_detached = false;
};

}
