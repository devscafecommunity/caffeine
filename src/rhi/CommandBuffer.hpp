// ============================================================================
// @file    CommandBuffer.hpp
// @brief   GPU command buffer — encapsulates per-frame GPU commands
// @note    Part of rhi/ module
// ============================================================================
#pragma once

#include "RenderDevice.hpp"

namespace Caffeine::RHI {

// ============================================================================
// @brief  Command buffer — records GPU commands for a single frame.
//
//  Lifecycle:
//    1. Obtained from RenderDevice::beginFrame()
//    2. beginRenderPass() / endRenderPass() — bracket a render pass
//    3. Bind resources and issue draw calls between begin/end
//    4. Returned to RenderDevice::endFrame() for submission
// ============================================================================
class CommandBuffer {
public:
    CommandBuffer() = default;
    ~CommandBuffer() = default;

    // Non-copyable
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Movable
    CommandBuffer(CommandBuffer&& other) noexcept;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;

    void beginRenderPass(const RenderPassDesc& desc);
    void endRenderPass();

    void bindPipeline(Pipeline* pipeline);
    void bindVertexBuffer(Buffer* buf, u32 slot = 0);
    void bindIndexBuffer(Buffer* buf);
    void bindTexture(Texture* tex, u32 slot = 0);
    void setViewport(f32 x, f32 y, f32 w, f32 h);
    void setScissor(u32 x, u32 y, u32 w, u32 h);

    void draw(u32 vertexCount, u32 firstVertex = 0);
    void drawIndexed(u32 indexCount, u32 firstIndex = 0, i32 vertexOffset = 0);
    void drawInstanced(u32 vertexCount, u32 instanceCount,
                       u32 firstVertex = 0, u32 firstInstance = 0);

    void pushUniformData(ShaderStage stage, u32 slot, const void* data, u32 size);

    bool isInRenderPass() const { return m_inRenderPass; }

    SDL_GPUCommandBuffer* nativeHandle() const { return m_cmdBuffer; }
    SDL_GPURenderPass*    nativeRenderPass() const { return m_renderPass; }

private:
    friend class RenderDevice;

    void acquire(SDL_GPUDevice* device);
    void submit();
    void reset();

    SDL_GPUDevice*        m_device     = nullptr;
    SDL_GPUCommandBuffer* m_cmdBuffer  = nullptr;
    SDL_GPURenderPass*    m_renderPass = nullptr;
    bool                  m_inRenderPass = false;
    bool                  m_acquired     = false;
};

}  // namespace Caffeine::RHI
