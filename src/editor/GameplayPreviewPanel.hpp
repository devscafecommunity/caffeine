#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_SDL3
#include "render/GpuSceneRenderer.hpp"
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include "render/SkyboxRenderer.hpp"
#endif

namespace Caffeine::Editor {

class GameplayPreviewPanel {
public:
#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device);
    void shutdown();
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
#endif

    void render(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

private:
#ifdef CF_HAS_SDL3
    void resizeCanvas(u32 width, u32 height);

    RHI::RenderDevice* m_device = nullptr;
    RHI::CommandBuffer* m_frameCmd = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    u32 m_width = 0;
    u32 m_height = 0;
    Render::GpuSceneRenderer m_renderer;
    bool m_ready = false;
#endif
#ifdef CF_HAS_IMGUI
    Render::SkyboxRenderer m_skyboxRenderer;
#endif
    bool m_open = true;
    bool m_detached = false;
};

}  // namespace Caffeine::Editor
