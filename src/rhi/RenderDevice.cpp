// ============================================================================
// @file    RenderDevice.cpp
// @brief   RenderDevice implementation
// ============================================================================
#include "RenderDevice.hpp"
#include "CommandBuffer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstring>

namespace Caffeine::RHI {

RenderDevice::~RenderDevice() {
    shutdown();
}

bool RenderDevice::init(SDL_Window* window, const RenderConfig& config) {
    if (m_device) {
        return false;
    }
    if (!window) {
        return false;
    }

    m_config = config;
    m_window = window;

    m_device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        false,
        nullptr
    );

    if (!m_device) {
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
        SDL_DestroyGPUDevice(m_device);
        m_device = nullptr;
        return false;
    }

    if (config.vsync) {
        SDL_SetGPUSwapchainParameters(m_device, m_window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_VSYNC);
    } else {
        SDL_SetGPUSwapchainParameters(m_device, m_window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_IMMEDIATE);
    }

    m_frameIndex = 0;
    return true;
}

void RenderDevice::shutdown() {
    if (!m_device) {
        return;
    }

    SDL_WaitForGPUIdle(m_device);
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        releasePendingTransfers(i);
    }
    m_activeFrameCmd = nullptr;

    if (m_window) {
        SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
    }

    SDL_DestroyGPUDevice(m_device);
    m_device = nullptr;
    m_window = nullptr;
    m_frameIndex = 0;
}

CommandBuffer* RenderDevice::beginFrame() {
    if (!m_device) {
        return nullptr;
    }

    auto* cmd = new CommandBuffer();
    cmd->acquire(m_device);

    if (!cmd->nativeHandle()) {
        delete cmd;
        return nullptr;
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    u32 swapW = 0, swapH = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            cmd->nativeHandle(), m_window, &swapchainTexture, &swapW, &swapH)) {
        cmd->submit();
        delete cmd;
        return nullptr;
    }

    if (!swapchainTexture) {
        cmd->submit();
        delete cmd;
        return nullptr;
    }

    cmd->m_swapchainTexture = swapchainTexture;
    m_activeFrameCmd = cmd;

    return cmd;
}

void RenderDevice::releasePendingTransfers(u32 slot) {
    if (!m_device || slot >= MAX_FRAMES_IN_FLIGHT) return;
    for (u32 i = 0; i < m_pendingTransferCounts[slot]; ++i) {
        if (m_pendingTransfers[slot][i]) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_pendingTransfers[slot][i]);
            m_pendingTransfers[slot][i] = nullptr;
        }
    }
    m_pendingTransferCounts[slot] = 0;
}

void RenderDevice::endFrame(CommandBuffer* cmd) {
    if (!cmd) {
        return;
    }

    if (cmd->isInRenderPass()) {
        cmd->endRenderPass();
    }

    cmd->submit();
    if (m_activeFrameCmd == cmd) {
        m_activeFrameCmd = nullptr;
    }
    delete cmd;

    // Release transfer buffers from the frame that just retired (3 frames ago).
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    releasePendingTransfers(m_frameIndex);
}

Texture* RenderDevice::createTexture(const TextureDesc& desc) {
    if (!m_device) {
        return nullptr;
    }

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = (desc.type == TextureType::Cube)
        ? SDL_GPU_TEXTURETYPE_CUBE
        : SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = static_cast<SDL_GPUTextureFormat>(desc.format);
    texInfo.usage = static_cast<SDL_GPUTextureUsageFlags>(desc.usage);
    texInfo.width = desc.width;
    texInfo.height = desc.height;
    texInfo.layer_count_or_depth = (desc.type == TextureType::Cube) ? 6u : 1u;
    texInfo.num_levels = desc.mipLevels;
    texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* gpuTex = SDL_CreateGPUTexture(m_device, &texInfo);
    if (!gpuTex) {
        return nullptr;
    }

    auto* tex = new Texture();
    tex->handle = gpuTex;
    tex->width = desc.width;
    tex->height = desc.height;
    tex->format = desc.format;
    return tex;
}

Shader* RenderDevice::createShader(const ShaderDesc& desc) {
    if (!m_device || !desc.code || desc.codeSize == 0) {
        return nullptr;
    }

    SDL_GPUShaderCreateInfo shaderInfo{};
    shaderInfo.code = desc.code;
    shaderInfo.code_size = desc.codeSize;
    shaderInfo.entrypoint = desc.entryPoint;
    shaderInfo.stage = static_cast<SDL_GPUShaderStage>(desc.stage);
    switch (desc.format) {
        case ShaderBytecodeFormat::DXBC:
            shaderInfo.format = SDL_GPU_SHADERFORMAT_DXBC;
            break;
        case ShaderBytecodeFormat::MSL:
            shaderInfo.format = SDL_GPU_SHADERFORMAT_MSL;
            break;
        case ShaderBytecodeFormat::Metallib:
            shaderInfo.format = SDL_GPU_SHADERFORMAT_METALLIB;
            break;
        case ShaderBytecodeFormat::SPIRV:
        default:
            shaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
            break;
    }
    shaderInfo.num_samplers = desc.numSamplers;
    shaderInfo.num_storage_textures = desc.numStorageTextures;
    shaderInfo.num_storage_buffers = desc.numStorageBuffers;
    shaderInfo.num_uniform_buffers = desc.numUniformBuffers;

    SDL_GPUShader* gpuShader = SDL_CreateGPUShader(m_device, &shaderInfo);
    if (!gpuShader) {
        return nullptr;
    }

    auto* shader = new Shader();
    shader->handle = gpuShader;
    shader->stage = desc.stage;
    return shader;
}

static SDL_GPUBufferUsageFlags toSDLBufferUsage(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Vertex:  return SDL_GPU_BUFFERUSAGE_VERTEX;
        case BufferUsage::Index:   return SDL_GPU_BUFFERUSAGE_INDEX;
        case BufferUsage::Storage: return SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        default:                   return SDL_GPU_BUFFERUSAGE_VERTEX;
    }
}

Buffer* RenderDevice::createBuffer(const BufferDesc& desc, BufferUsage usage) {
    if (!m_device || desc.size == 0) {
        return nullptr;
    }

    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = toSDLBufferUsage(usage);
    bufInfo.size = static_cast<Uint32>(desc.size);

    SDL_GPUBuffer* gpuBuf = SDL_CreateGPUBuffer(m_device, &bufInfo);
    if (!gpuBuf) {
        return nullptr;
    }

    auto* buf = new Buffer();
    buf->handle = gpuBuf;
    buf->size = desc.size;
    buf->usage = usage;
    return buf;
}

void RenderDevice::destroyTexture(Texture* tex) {
    if (!tex) return;
    if (m_device && tex->handle) {
        SDL_ReleaseGPUTexture(m_device, tex->handle);
    }
    delete tex;
}

void RenderDevice::destroyShader(Shader* shader) {
    if (!shader) return;
    if (m_device && shader->handle) {
        SDL_ReleaseGPUShader(m_device, shader->handle);
    }
    delete shader;
}

void RenderDevice::destroyBuffer(Buffer* buf) {
    if (!buf) return;
    if (m_device && buf->handle) {
        SDL_ReleaseGPUBuffer(m_device, buf->handle);
    }
    delete buf;
}

static SDL_GPUVertexElementFormat toSDLVertexFormat(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        default: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    }
}

Pipeline* RenderDevice::createGraphicsPipeline(Shader* vertexShader, Shader* fragmentShader,
                                               const GraphicsPipelineDesc& desc) {
    if (!m_device || !vertexShader || !fragmentShader ||
        !vertexShader->handle || !fragmentShader->handle ||
        !desc.vertexBuffers || desc.numVertexBuffers == 0 ||
        !desc.attributes || desc.numAttributes == 0) {
        return nullptr;
    }

    SDL_GPUVertexBufferDescription sdlVertexBuffers[4]{};
    const u32 vbCount = desc.numVertexBuffers > 4 ? 4 : desc.numVertexBuffers;
    for (u32 i = 0; i < vbCount; ++i) {
        sdlVertexBuffers[i].slot = desc.vertexBuffers[i].slot;
        sdlVertexBuffers[i].pitch = desc.vertexBuffers[i].stride;
        sdlVertexBuffers[i].input_rate = desc.vertexBuffers[i].perInstance
            ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
            : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        sdlVertexBuffers[i].instance_step_rate = 0;
    }

    SDL_GPUVertexAttribute sdlAttributes[8]{};
    const u32 attrCount = desc.numAttributes > 8 ? 8 : desc.numAttributes;
    for (u32 i = 0; i < attrCount; ++i) {
        sdlAttributes[i].location = desc.attributes[i].location;
        sdlAttributes[i].buffer_slot = desc.attributes[i].bufferSlot;
        sdlAttributes[i].format = toSDLVertexFormat(desc.attributes[i].format);
        sdlAttributes[i].offset = desc.attributes[i].offset;
    }

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = sdlVertexBuffers;
    vertexInput.num_vertex_buffers = vbCount;
    vertexInput.vertex_attributes = sdlAttributes;
    vertexInput.num_vertex_attributes = attrCount;

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode = SDL_GPU_CULLMODE_BACK;
    rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.enable_depth_bias = false;
    rasterizer.enable_depth_clip = true;

    SDL_GPUMultisampleState multisample{};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;
    multisample.enable_mask = false;

    SDL_GPUDepthStencilState depthStencil{};
    depthStencil.enable_depth_test = desc.depthTest;
    depthStencil.enable_depth_write = desc.depthWrite;
    depthStencil.enable_stencil_test = false;
    depthStencil.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = desc.enableBlend;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    blend.enable_color_write_mask = true;

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = static_cast<SDL_GPUTextureFormat>(desc.colorFormat);
    colorTarget.blend_state = blend;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.num_color_targets = 1;
    targetInfo.color_target_descriptions = &colorTarget;
    targetInfo.has_depth_stencil_target = desc.depthTest || desc.depthWrite;
    targetInfo.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(desc.depthFormat);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader->handle;
    pipelineInfo.fragment_shader = fragmentShader->handle;
    pipelineInfo.vertex_input_state = vertexInput;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state = rasterizer;
    pipelineInfo.multisample_state = multisample;
    pipelineInfo.depth_stencil_state = depthStencil;
    pipelineInfo.target_info = targetInfo;

    SDL_GPUGraphicsPipeline* gpuPipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineInfo);
    if (!gpuPipeline) {
        return nullptr;
    }

    auto* pipeline = new Pipeline();
    pipeline->handle = gpuPipeline;
    return pipeline;
}

Sampler* RenderDevice::createSampler(const SamplerDesc& desc) {
    if (!m_device) {
        return nullptr;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = desc.linearFilter ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = desc.linearFilter ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = desc.linearFilter
        ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
        : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = desc.clampToEdge
        ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
        : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = samplerInfo.address_mode_u;
    samplerInfo.address_mode_w = samplerInfo.address_mode_u;
    samplerInfo.mip_lod_bias = 0.0f;
    samplerInfo.min_lod = -1000.0f;
    samplerInfo.max_lod = 1000.0f;
    samplerInfo.enable_anisotropy = false;
    samplerInfo.max_anisotropy = 1.0f;
    samplerInfo.enable_compare = false;

    SDL_GPUSampler* gpuSampler = SDL_CreateGPUSampler(m_device, &samplerInfo);
    if (!gpuSampler) {
        return nullptr;
    }

    auto* sampler = new Sampler();
    sampler->handle = gpuSampler;
    return sampler;
}

bool RenderDevice::uploadBuffer(Buffer* buffer, const void* data, u64 size, u64 offset) {
    if (!m_device || !buffer || !buffer->handle || !data || size == 0) {
        return false;
    }
    if (offset + size > buffer->size) {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
    if (!transferBuffer) {
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, true);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);

    const bool useFrameCmd = m_activeFrameCmd && m_activeFrameCmd->nativeHandle() &&
                             !m_activeFrameCmd->isInRenderPass();
    SDL_GPUCommandBuffer* cmd =
        useFrameCmd ? m_activeFrameCmd->nativeHandle() : SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        if (!useFrameCmd) {
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transferBuffer;
    src.offset = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = buffer->handle;
    dst.offset = static_cast<Uint32>(offset);
    dst.size = static_cast<Uint32>(size);

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);

    if (useFrameCmd) {
        const u32 slot = m_frameIndex;
        if (m_pendingTransferCounts[slot] < 32) {
            m_pendingTransfers[slot][m_pendingTransferCounts[slot]++] = transferBuffer;
        } else {
            // Overflow: wait this frame so we can free the extra transfer.
            SDL_WaitForGPUIdle(m_device);
            SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        }
        return true;
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(m_device);
    SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
    return true;
}

bool RenderDevice::uploadTexture(Texture* texture, const void* pixels, u32 width, u32 height,
                                 u32 bytesPerPixel) {
    if (!m_device || !texture || !texture->handle || !pixels || width < 1 || height < 1) {
        return false;
    }
    if (bytesPerPixel < 1 || bytesPerPixel > 16) {
        return false;
    }

    const u32 rowPitch = width * bytesPerPixel;
    const u64 transferSize = static_cast<u64>(rowPitch) * static_cast<u64>(height);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(transferSize);
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
    if (!transferBuffer) {
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, true);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }
    std::memcpy(mapped, pixels, transferSize);
    SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);

    const bool useFrameCmd = m_activeFrameCmd && m_activeFrameCmd->nativeHandle() &&
                             !m_activeFrameCmd->isInRenderPass();
    SDL_GPUCommandBuffer* cmd =
        useFrameCmd ? m_activeFrameCmd->nativeHandle() : SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        if (!useFrameCmd) {
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        return false;
    }

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transferBuffer;
    src.offset = 0;
    src.pixels_per_row = width;
    src.rows_per_layer = height;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture->handle;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.x = 0;
    dst.y = 0;
    dst.z = 0;
    dst.w = width;
    dst.h = height;
    dst.d = 1;

    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);

    if (useFrameCmd) {
        const u32 slot = m_frameIndex;
        if (m_pendingTransferCounts[slot] < 32) {
            m_pendingTransfers[slot][m_pendingTransferCounts[slot]++] = transferBuffer;
        } else {
            SDL_WaitForGPUIdle(m_device);
            SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        }
        return true;
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(m_device);
    SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
    return true;
}

void RenderDevice::destroyPipeline(Pipeline* pipeline) {
    if (!pipeline) {
        return;
    }
    if (m_device && pipeline->handle) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, pipeline->handle);
    }
    delete pipeline;
}

void RenderDevice::destroySampler(Sampler* sampler) {
    if (!sampler) {
        return;
    }
    if (m_device && sampler->handle) {
        SDL_ReleaseGPUSampler(m_device, sampler->handle);
    }
    delete sampler;
}

}  // namespace Caffeine::RHI
