// ============================================================================
// @file    CommandBuffer.cpp
// @brief   CommandBuffer implementation
// ============================================================================
#include "CommandBuffer.hpp"

#include <SDL3/SDL_gpu.h>

#include <vector>

namespace Caffeine::RHI {

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_cmdBuffer(other.m_cmdBuffer)
    , m_renderPass(other.m_renderPass)
    , m_computePass(other.m_computePass)
    , m_inRenderPass(other.m_inRenderPass)
    , m_inComputePass(other.m_inComputePass)
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
        m_computePass = other.m_computePass;
        m_inRenderPass = other.m_inRenderPass;
        m_inComputePass = other.m_inComputePass;
        m_acquired = other.m_acquired;
        other.m_device = nullptr;
        other.m_cmdBuffer = nullptr;
        other.m_renderPass = nullptr;
        other.m_computePass = nullptr;
        other.m_inRenderPass = false;
        other.m_inComputePass = false;
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
    m_computePass = nullptr;
    m_inRenderPass = false;
    m_inComputePass = false;
    m_acquired = false;
}

void CommandBuffer::beginRenderPass(const RenderPassDesc& desc) {
    if (!m_cmdBuffer || m_inRenderPass) {
        return;
    }

    const u32 targetCount = desc.numColorTargets < 1 ? 1u
                          : (desc.numColorTargets > kMaxColorTargets ? kMaxColorTargets
                                                                     : desc.numColorTargets);

    SDL_GPUColorTargetInfo colorTargets[kMaxColorTargets]{};
    u32 textureCount = 0;
    for (u32 i = 0; i < targetCount; ++i) {
        // Attachment 0 may be the swapchain; extra attachments must be real
        // textures (swapchain images are never part of an MRT set).
        SDL_GPUTexture* handle = (i == 0) ? desc.colorTarget ? desc.colorTarget->handle
                                                             : m_swapchainTexture
                                         : (desc.extraColorTargets[i - 1]
                                                ? desc.extraColorTargets[i - 1]->handle
                                                : nullptr);
        if (!handle) {
            break;
        }

        SDL_GPUColorTargetInfo& target = colorTargets[textureCount];
        target.texture       = handle;
        // Mip/layer selection only applies to attachment 0; MRT extras are used
        // at mip 0 / layer 0.
        target.mip_level     = (i == 0) ? desc.colorMipLevel : 0;
        target.layer_or_depth_plane = (i == 0) ? desc.colorLayer : 0;
        // Cycling hands back a fresh backing texture, so it can't be combined with LOAD.
        target.cycle         = desc.cycle && !desc.loadColor;
        target.load_op       = desc.loadColor ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        target.store_op      = SDL_GPU_STOREOP_STORE;

        const f32* clear = desc.clearColor;
        if (i > 0 && desc.usePerTargetClear) {
            clear = desc.extraClearColors[i - 1];
        }
        target.clear_color.r = clear[0];
        target.clear_color.g = clear[1];
        target.clear_color.b = clear[2];
        target.clear_color.a = clear[3];
        ++textureCount;
    }

    if (textureCount == 0) {
        return;
    }

    SDL_GPUDepthStencilTargetInfo depthTarget{};
    SDL_GPUDepthStencilTargetInfo* depthPtr = nullptr;
    if (desc.depthTarget && desc.depthTarget->handle) {
        depthTarget.texture = desc.depthTarget->handle;
        depthTarget.cycle = desc.cycle && desc.clearDepth;
        depthTarget.load_op = desc.clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.clear_depth = desc.depthValue;
        depthTarget.clear_stencil = 0;
        depthPtr = &depthTarget;
    }

    m_renderPass   = SDL_BeginGPURenderPass(m_cmdBuffer, colorTargets, textureCount, depthPtr);
    m_inRenderPass = (m_renderPass != nullptr);
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

void CommandBuffer::bindVertexBuffer(Buffer* buf, u32 slot, u32 offset) {
    if (!m_renderPass || !buf || !buf->handle) {
        return;
    }
    SDL_GPUBufferBinding binding{};
    binding.buffer = buf->handle;
    binding.offset = offset;
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

void CommandBuffer::bindTexture(Texture* tex, u32 slot, Sampler* sampler) {
    if (!m_renderPass || !tex || !tex->handle || !sampler || !sampler->handle) {
        return;
    }
    SDL_GPUTextureSamplerBinding samplerBinding{};
    samplerBinding.texture = tex->handle;
    samplerBinding.sampler = sampler ? sampler->handle : nullptr;
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

void CommandBuffer::drawIndexedInstanced(u32 indexCount, u32 instanceCount, u32 firstIndex,
                                         i32 vertexOffset, u32 firstInstance) {
    if (!m_renderPass || indexCount == 0 || instanceCount == 0) {
        return;
    }
    SDL_DrawGPUIndexedPrimitives(m_renderPass, indexCount, instanceCount, firstIndex, vertexOffset,
                                 firstInstance);
}

void CommandBuffer::drawInstanced(u32 vertexCount, u32 instanceCount,
                                   u32 firstVertex, u32 firstInstance) {
    if (!m_renderPass) {
        return;
    }
    SDL_DrawGPUPrimitives(m_renderPass, vertexCount, instanceCount,
                          firstVertex, firstInstance);
}

void CommandBuffer::beginComputePass(Texture* storageTexture, u32 mipLevel, u32 layer) {
    if (!m_cmdBuffer || m_inRenderPass || m_inComputePass) return;
    SDL_GPUStorageTextureReadWriteBinding binding{};
    if (storageTexture && storageTexture->handle) {
        binding.texture = storageTexture->handle;
        binding.mip_level = mipLevel;
        binding.layer = layer;
        binding.cycle = true;
    }
    m_computePass = SDL_BeginGPUComputePass(
        m_cmdBuffer, storageTexture && storageTexture->handle ? &binding : nullptr,
        storageTexture && storageTexture->handle ? 1u : 0u, nullptr, 0);
    m_inComputePass = m_computePass != nullptr;
}

void CommandBuffer::beginComputePass(Texture* const* storageTextures, u32 count) {
    if (!m_cmdBuffer || m_inRenderPass || m_inComputePass) return;
    std::vector<SDL_GPUStorageTextureReadWriteBinding> bindings;
    bindings.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        if (!storageTextures || !storageTextures[i] || !storageTextures[i]->handle) {
            return;
        }
        SDL_GPUStorageTextureReadWriteBinding binding{};
        binding.texture = storageTextures[i]->handle;
        binding.mip_level = 0;
        binding.layer = 0;
        binding.cycle = true;
        bindings.push_back(binding);
    }
    m_computePass = SDL_BeginGPUComputePass(
        m_cmdBuffer, bindings.empty() ? nullptr : bindings.data(), count, nullptr, 0);
    m_inComputePass = m_computePass != nullptr;
}

void CommandBuffer::endComputePass() {
    if (!m_inComputePass) return;
    if (m_computePass) {
        SDL_EndGPUComputePass(m_computePass);
        m_computePass = nullptr;
    }
    m_inComputePass = false;
}

void CommandBuffer::bindComputePipeline(ComputePipeline* pipeline) {
    if (!m_computePass || !pipeline || !pipeline->handle) return;
    SDL_BindGPUComputePipeline(m_computePass, pipeline->handle);
}

void CommandBuffer::bindComputeStorageBuffers(Buffer* const* buffers, u32 firstSlot, u32 count) {
    if (!m_computePass || !buffers || count == 0) return;
    std::vector<SDL_GPUBuffer*> handles;
    handles.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        handles.push_back(buffers[i] && buffers[i]->handle ? buffers[i]->handle : nullptr);
    }
    SDL_BindGPUComputeStorageBuffers(m_computePass, firstSlot, handles.data(), count);
}

void CommandBuffer::bindComputeTextures(Texture* const* textures, Sampler* const* samplers,
                                        u32 firstSlot, u32 count) {
    if (!m_computePass || !textures || !samplers || count == 0) return;

    std::vector<SDL_GPUTextureSamplerBinding> bindings;
    bindings.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        // SDL requires a texture and a sampler for every sampled slot; silently
        // binding a hole would make the shader read undefined data.
        if (!textures[i] || !textures[i]->handle || !samplers[i] || !samplers[i]->handle) {
            return;
        }
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = textures[i]->handle;
        binding.sampler = samplers[i]->handle;
        bindings.push_back(binding);
    }
    SDL_BindGPUComputeSamplers(m_computePass, firstSlot, bindings.data(), count);
}

void CommandBuffer::bindComputeTexture(Texture* tex, u32 slot, Sampler* sampler) {
    Texture* textures[1] = {tex};
    Sampler* samplers[1] = {sampler};
    bindComputeTextures(textures, samplers, slot, 1);
}

void CommandBuffer::pushComputeUniformData(u32 slot, const void* data, u32 size) {
    if (!m_cmdBuffer || !data || size == 0) return;
    SDL_PushGPUComputeUniformData(m_cmdBuffer, slot, data, size);
}

void CommandBuffer::dispatch(u32 x, u32 y, u32 z) {
    if (!m_computePass) return;
    SDL_DispatchGPUCompute(m_computePass, x, y, z);
}

void CommandBuffer::copyTexture(Texture* src, Texture* dst, u32 width, u32 height) {
    copyTextureRegion(src, 0, dst, 0, width, height);
}

void CommandBuffer::copyTextureRegion(Texture* src, u32 srcMip, Texture* dst, u32 dstMip,
                                      u32 width, u32 height) {
    if (!m_cmdBuffer || m_inRenderPass || !src || !dst || !src->handle || !dst->handle ||
        width == 0 || height == 0) {
        return;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(m_cmdBuffer);
    if (!copyPass) return;
    SDL_GPUTextureLocation from{};
    from.texture = src->handle;
    from.mip_level = srcMip;
    SDL_GPUTextureLocation to{};
    to.texture = dst->handle;
    to.mip_level = dstMip;
    SDL_CopyGPUTextureToTexture(copyPass, &from, &to, width, height, 1, false);
    SDL_EndGPUCopyPass(copyPass);
}

void CommandBuffer::generateMipmaps(Texture* texture) {
    if (!m_cmdBuffer || m_inRenderPass || !texture || !texture->handle) return;
    SDL_GenerateMipmapsForGPUTexture(m_cmdBuffer, texture->handle);
}

void CommandBuffer::pushUniformData(ShaderStage stage, u32 slot,
                                     const void* data, u32 size) {
    if (!m_cmdBuffer || !data || size == 0) {
        return;
    }
    if (stage == ShaderStage::Fragment) {
        SDL_PushGPUFragmentUniformData(m_cmdBuffer, slot, data, size);
    } else {
        SDL_PushGPUVertexUniformData(m_cmdBuffer, slot, data, size);
    }
}

}  // namespace Caffeine::RHI
