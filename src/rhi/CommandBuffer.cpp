// ============================================================================
// @file    CommandBuffer.cpp
// @brief   CommandBuffer implementation
// ============================================================================
#include "CommandBuffer.hpp"

#include <SDL3/SDL_gpu.h>

namespace Caffeine::RHI {

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_cmdBuffer(other.m_cmdBuffer)
    , m_renderPass(other.m_renderPass)
    , m_inRenderPass(other.m_inRenderPass)
    , m_acquired(other.m_acquired) {
    other.m_device = nullptr;
    other.m_cmdBuffer = nullptr;
    other.m_renderPass = nullptr;
    other.m_inRenderPass = false;
    other.m_acquired = false;
}

CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_cmdBuffer = other.m_cmdBuffer;
        m_renderPass = other.m_renderPass;
        m_inRenderPass = other.m_inRenderPass;
        m_acquired = other.m_acquired;
        other.m_device = nullptr;
        other.m_cmdBuffer = nullptr;
        other.m_renderPass = nullptr;
        other.m_inRenderPass = false;
        other.m_acquired = false;
    }
    return *this;
}

void CommandBuffer::acquire(SDL_GPUDevice* device) {
    m_device = device;
    m_cmdBuffer = SDL_AcquireGPUCommandBuffer(device);
    m_acquired = (m_cmdBuffer != nullptr);
}

void CommandBuffer::submit() {
    if (m_cmdBuffer) {
        SDL_SubmitGPUCommandBuffer(m_cmdBuffer);
        m_cmdBuffer = nullptr;
    }
    m_acquired = false;
}

void CommandBuffer::reset() {
    m_cmdBuffer = nullptr;
    m_renderPass = nullptr;
    m_inRenderPass = false;
    m_acquired = false;
}

void CommandBuffer::beginRenderPass(const RenderPassDesc& desc) {
    if (!m_cmdBuffer || m_inRenderPass) {
        return;
    }

    // This begins a render pass targeting the swapchain texture.
    // For off-screen rendering, the caller would provide a texture target.
    // For now, this sets up the clear color but actual render target
    // binding happens in RenderDevice::endFrame with the swapchain texture.
    m_inRenderPass = true;
    (void)desc;
}

void CommandBuffer::endRenderPass() {
    if (!m_inRenderPass) {
        return;
    }

    if (m_renderPass) {
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }

    m_inRenderPass = false;
}

void CommandBuffer::bindPipeline(Pipeline* pipeline) {
    if (!m_renderPass || !pipeline || !pipeline->handle) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(m_renderPass, pipeline->handle);
}

void CommandBuffer::bindVertexBuffer(Buffer* buf, u32 slot) {
    if (!m_renderPass || !buf || !buf->handle) {
        return;
    }
    SDL_GPUBufferBinding binding{};
    binding.buffer = buf->handle;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(m_renderPass, slot, &binding, 1);
}

void CommandBuffer::bindIndexBuffer(Buffer* buf) {
    if (!m_renderPass || !buf || !buf->handle) {
        return;
    }
    SDL_GPUBufferBinding binding{};
    binding.buffer = buf->handle;
    binding.offset = 0;
    SDL_BindGPUIndexBuffer(m_renderPass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void CommandBuffer::bindTexture(Texture* tex, u32 slot) {
    if (!m_renderPass || !tex || !tex->handle) {
        return;
    }
    SDL_GPUTextureSamplerBinding samplerBinding{};
    samplerBinding.texture = tex->handle;
    samplerBinding.sampler = nullptr;
    SDL_BindGPUFragmentSamplers(m_renderPass, slot, &samplerBinding, 1);
}

void CommandBuffer::setViewport(f32 x, f32 y, f32 w, f32 h) {
    if (!m_renderPass) {
        return;
    }
    SDL_GPUViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.w = w;
    viewport.h = h;
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(m_renderPass, &viewport);
}

void CommandBuffer::setScissor(u32 x, u32 y, u32 w, u32 h) {
    if (!m_renderPass) {
        return;
    }
    SDL_Rect scissor{};
    scissor.x = static_cast<int>(x);
    scissor.y = static_cast<int>(y);
    scissor.w = static_cast<int>(w);
    scissor.h = static_cast<int>(h);
    SDL_SetGPUScissor(m_renderPass, &scissor);
}

void CommandBuffer::draw(u32 vertexCount, u32 firstVertex) {
    if (!m_renderPass) {
        return;
    }
    SDL_DrawGPUPrimitives(m_renderPass, vertexCount, 1, firstVertex, 0);
}

void CommandBuffer::drawIndexed(u32 indexCount, u32 firstIndex, i32 vertexOffset) {
    if (!m_renderPass) {
        return;
    }
    SDL_DrawGPUIndexedPrimitives(m_renderPass, indexCount, 1, firstIndex, vertexOffset, 0);
}

void CommandBuffer::drawInstanced(u32 vertexCount, u32 instanceCount,
                                   u32 firstVertex, u32 firstInstance) {
    if (!m_renderPass) {
        return;
    }
    SDL_DrawGPUPrimitives(m_renderPass, vertexCount, instanceCount,
                          firstVertex, firstInstance);
}

void CommandBuffer::pushUniformData(ShaderStage stage, u32 slot,
                                     const void* data, u32 size) {
    if (!m_cmdBuffer || !data || size == 0) {
        return;
    }
    SDL_PushGPUVertexUniformData(m_cmdBuffer, slot, data, size);
    if (stage == ShaderStage::Fragment) {
        SDL_PushGPUFragmentUniformData(m_cmdBuffer, slot, data, size);
    }
}

}  // namespace Caffeine::RHI
