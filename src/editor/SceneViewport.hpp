#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "scene/SceneComponents.hpp"
#include "render/Camera2D.hpp"
#include "math/Math.hpp"
#include "editor/EditorContext.hpp"
#include "editor/TransformGizmo.hpp"

#include <cmath>

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
using namespace Caffeine;

class SceneViewport {
public:
#ifdef CF_HAS_SDL3
    struct Config {
        u32  width       = 1280;
        u32  height      = 720;
        bool grid        = true;
        f32  gridSpacing = 64.0f;
        f32  gridWidth   = 2000.0f;

        static Config defaults() { return {}; }
    };
#endif

    SceneViewport() = default;

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Config cfg = Config::defaults());
    void shutdown();
    RHI::Texture* colorTarget() const { return m_colorTarget; }
#endif

    void render(ECS::World& world, EditorContext& ctx
#ifdef CF_HAS_SDL3
                , Render::Camera2D& editorCamera
#endif
               );

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

private:
#ifdef CF_HAS_IMGUI
    void drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize);
#endif

    bool m_open = true;
    bool m_initialized = false;
    TransformGizmo m_Gizmo;
    bool m_gizmoDragging = false;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    Config m_config;
#endif
};

} // namespace Caffeine::Editor