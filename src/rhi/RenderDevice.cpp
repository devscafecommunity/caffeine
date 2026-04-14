// ============================================================================
// @file    RenderDevice.cpp
// @brief   RenderDevice implementation
// ============================================================================
#include "RenderDevice.hpp"
#include "CommandBuffer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

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
        true,
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

    return cmd;
}

void RenderDevice::endFrame(CommandBuffer* cmd) {
    if (!cmd) {
        return;
    }

    if (cmd->isInRenderPass()) {
        cmd->endRenderPass();
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    u32 swapW = 0, swapH = 0;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            cmd->nativeHandle(), m_window, &swapchainTexture, &swapW, &swapH)) {
        cmd->submit();
        delete cmd;
        return;
    }

    if (swapchainTexture) {
        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = swapchainTexture;
        colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        colorTarget.clear_color.r = 0.0f;
        colorTarget.clear_color.g = 0.0f;
        colorTarget.clear_color.b = 0.0f;
        colorTarget.clear_color.a = 1.0f;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            cmd->nativeHandle(), &colorTarget, 1, nullptr);

        if (pass) {
            SDL_EndGPURenderPass(pass);
        }
    }

    cmd->submit();
    delete cmd;

    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

Texture* RenderDevice::createTexture(const TextureDesc& desc) {
    if (!m_device) {
        return nullptr;
    }

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = static_cast<SDL_GPUTextureFormat>(desc.format);
    texInfo.usage = static_cast<SDL_GPUTextureUsageFlags>(desc.usage);
    texInfo.width = desc.width;
    texInfo.height = desc.height;
    texInfo.layer_count_or_depth = 1;
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
    shaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
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

}  // namespace Caffeine::RHI
