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
    void bindVertexBuffer(Buffer* buf, u32 slot = 0, u32 offset = 0);
    void bindIndexBuffer(Buffer* buf);
    void bindTexture(Texture* tex, u32 slot = 0, Sampler* sampler = nullptr);
    void setViewport(f32 x, f32 y, f32 w, f32 h);
    void setScissor(u32 x, u32 y, u32 w, u32 h);

    void draw(u32 vertexCount, u32 firstVertex = 0);
    void drawIndexed(u32 indexCount, u32 firstIndex = 0, i32 vertexOffset = 0);
    void drawIndexedInstanced(u32 indexCount, u32 instanceCount, u32 firstIndex = 0,
                              i32 vertexOffset = 0, u32 firstInstance = 0);
    void drawInstanced(u32 vertexCount, u32 instanceCount,
                       u32 firstVertex = 0, u32 firstInstance = 0);

    void pushUniformData(ShaderStage stage, u32 slot, const void* data, u32 size);

    // Compute. `beginComputePass` accepts one or more read-write storage
    // textures (with an explicit mip/layer so a single texture can be reduced
    // level by level); read-only storage buffers are bound with
    // bindComputeStorageBuffers and sampled textures with bindComputeTexture(s).
    void beginComputePass(Texture* storageTexture = nullptr, u32 mipLevel = 0, u32 layer = 0);
    /// Multiple read-write storage textures (slots 0..count-1), mip 0/layer 0.
    void beginComputePass(Texture* const* storageTextures, u32 count);
    void endComputePass();
    void bindComputePipeline(ComputePipeline* pipeline);
    void bindComputeStorageBuffers(Buffer* const* buffers, u32 firstSlot, u32 count);
    /// Binds sampled textures for compute. Slots must stay below
    /// ComputePipelineDesc::numSamplers. Requires a non-null sampler per entry.
    void bindComputeTextures(Texture* const* textures, Sampler* const* samplers,
                             u32 firstSlot, u32 count);
    void bindComputeTexture(Texture* tex, u32 slot = 0, Sampler* sampler = nullptr);
    void pushComputeUniformData(u32 slot, const void* data, u32 size);
    void dispatch(u32 x, u32 y, u32 z);

    /// GPU-side copy of mip 0 / layer 0. Must be called outside a render pass; formats must match.
    void copyTexture(Texture* src, Texture* dst, u32 width, u32 height);
    /// Copies an explicit mip of `src` into `dstMip` of `dst` (layer 0). Used by
    /// the Min-Z pyramid, which reduces in place and stages the previous level
    /// in a scratch texture to avoid same-texture read/write hazards.
    void copyTextureRegion(Texture* src, u32 srcMip, Texture* dst, u32 dstMip,
                           u32 width, u32 height);

    /// Fills mips 1..N from mip 0. Outside a render pass; texture needs Sampler | ColorTarget.
    void generateMipmaps(Texture* texture);

    bool isInRenderPass() const { return m_inRenderPass; }
    bool isInComputePass() const { return m_inComputePass; }

    SDL_GPUCommandBuffer* nativeHandle() const { return m_cmdBuffer; }
    SDL_GPURenderPass*    nativeRenderPass() const { return m_renderPass; }

private:
    friend class RenderDevice;

    void acquire(SDL_GPUDevice* device);
    void submit();
    void reset();

    SDL_GPUDevice*        m_device            = nullptr;
    SDL_GPUCommandBuffer* m_cmdBuffer         = nullptr;
    SDL_GPURenderPass*    m_renderPass        = nullptr;
    SDL_GPUComputePass*   m_computePass       = nullptr;
    SDL_GPUTexture*       m_swapchainTexture  = nullptr;
    bool                  m_inRenderPass      = false;
    bool                  m_inComputePass     = false;
    bool                  m_acquired          = false;
};

}  // namespace Caffeine::RHI
