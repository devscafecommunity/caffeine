#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#endif

#ifdef CF_HAS_SDL3
#include "render/GpuSceneRenderer.hpp"
#include "render/SkyboxRenderer.hpp"
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#endif

namespace Caffeine::Runtime {

#ifdef CF_HAS_IMGUI

class RuntimeSceneRenderer {
public:
#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device);
    void shutdown();
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
#endif

    void render(ECS::World& world, Editor::EditorContext& ctx, ImDrawList* dl, const Mat4& vp,
                const Vec3& camPos, ImVec2 origin, ImVec2 panelSize,
                ECS::Entity skipEntity = ECS::Entity::INVALID,
                const std::string& projectRoot = {},
                const Render::GpuSceneCamera* gpuCamera = nullptr);

private:
#ifdef CF_HAS_SDL3
    void resizeCanvas(u32 width, u32 height);

    RHI::RenderDevice* m_device = nullptr;
    RHI::CommandBuffer* m_frameCmd = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    u32 m_canvasW = 0;
    u32 m_canvasH = 0;
    Render::GpuSceneRenderer m_gpuRenderer;
    Render::SkyboxRenderer m_skyboxRenderer;
    bool m_gpuReady = false;
#endif

    struct RasterTexture {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
    };

    RasterTexture m_frameTexture;
};

#endif

}  // namespace Caffeine::Runtime
