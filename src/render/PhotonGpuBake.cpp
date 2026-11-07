#include "render/PhotonGpuBake.hpp"

#include "render/ShaderBytecode.hpp"
#include "rhi/RenderDevice.hpp"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Caffeine::Render {
namespace {

struct BakeParams {
    int counts[4];    // triangle count, texel count, resolution, rays
    float params[4];  // max distance, bias
};

SDL_GPUBuffer* createStorageBuffer(SDL_GPUDevice* device, u64 size) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = static_cast<Uint32>(size);
    return SDL_CreateGPUBuffer(device, &info);
}

// Uploads and computes in one command buffer so the bake never touches the
// editor's in-flight frame command buffer.
bool runBake(SDL_GPUDevice* device, SDL_GPUComputePipeline* pipeline,
             const void* triangles, u64 triangleBytes, const void* positions, u64 positionBytes,
             const void* normals, u64 normalBytes, u32 triangleCount, u32 texelCount,
             u32 resolution, const BakeParams& params, std::vector<f32>& outAO) {
    SDL_GPUBuffer* triangleBuffer = createStorageBuffer(device, triangleBytes);
    SDL_GPUBuffer* positionBuffer = createStorageBuffer(device, positionBytes);
    SDL_GPUBuffer* normalBuffer = createStorageBuffer(device, normalBytes);

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    texInfo.width = resolution;
    texInfo.height = resolution;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* aoTexture = SDL_CreateGPUTexture(device, &texInfo);

    bool ok = triangleBuffer && positionBuffer && normalBuffer && aoTexture;
    outAO.assign(texelCount, 1.0f);
    if (!ok) {
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }

    // Uploads must share the command buffer that later dispatches the compute.
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(triangleBytes + positionBytes + normalBytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }
    std::memcpy(static_cast<u8*>(mapped), triangles, triangleBytes);
    std::memcpy(static_cast<u8*>(mapped) + triangleBytes, positions, positionBytes);
    std::memcpy(static_cast<u8*>(mapped) + triangleBytes + positionBytes, normals, normalBytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    auto uploadRegion = [&](u64 offset, SDL_GPUBuffer* dst, u64 size) {
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = transfer;
        src.offset = static_cast<Uint32>(offset);
        SDL_GPUBufferRegion dstRegion{};
        dstRegion.buffer = dst;
        dstRegion.size = static_cast<Uint32>(size);
        SDL_UploadToGPUBuffer(copy, &src, &dstRegion, false);
    };
    uploadRegion(0, triangleBuffer, triangleBytes);
    uploadRegion(triangleBytes, positionBuffer, positionBytes);
    uploadRegion(triangleBytes + positionBytes, normalBuffer, normalBytes);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUStorageTextureReadWriteBinding binding{};
    binding.texture = aoTexture;
    binding.mip_level = 0;
    binding.layer = 0;
    binding.cycle = true;
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &binding, 1, nullptr, 0);
    if (!pass) {
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }
    SDL_GPUBuffer* inputs[3] = {triangleBuffer, positionBuffer, normalBuffer};
    SDL_BindGPUComputePipeline(pass, pipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, inputs, 3);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    const u32 groups = (resolution + 7u) / 8u;
    SDL_DispatchGPUCompute(pass, groups, groups, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);
    } else {
        SDL_WaitForGPUIdle(device);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);

    // Download the AO texture.
    SDL_GPUTransferBufferCreateInfo downloadInfo{};
    downloadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    downloadInfo.size = static_cast<Uint32>(static_cast<u64>(texelCount) * 4);
    SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(device, &downloadInfo);
    if (!download) {
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }

    SDL_GPUCommandBuffer* downloadCmd = SDL_AcquireGPUCommandBuffer(device);
    if (!downloadCmd) {
        SDL_ReleaseGPUTransferBuffer(device, download);
        SDL_ReleaseGPUBuffer(device, triangleBuffer);
        SDL_ReleaseGPUBuffer(device, positionBuffer);
        SDL_ReleaseGPUBuffer(device, normalBuffer);
        SDL_ReleaseGPUTexture(device, aoTexture);
        return false;
    }
    SDL_GPUCopyPass* downloadPass = SDL_BeginGPUCopyPass(downloadCmd);
    SDL_GPUTextureRegion src{};
    src.texture = aoTexture;
    src.w = resolution;
    src.h = resolution;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = download;
    dst.pixels_per_row = resolution;
    dst.rows_per_layer = resolution;
    SDL_DownloadFromGPUTexture(downloadPass, &src, &dst);
    SDL_EndGPUCopyPass(downloadPass);
    SDL_GPUFence* downloadFence = SDL_SubmitGPUCommandBufferAndAcquireFence(downloadCmd);
    if (downloadFence) {
        SDL_WaitForGPUFences(device, true, &downloadFence, 1);
        SDL_ReleaseGPUFence(device, downloadFence);
    } else {
        SDL_WaitForGPUIdle(device);
    }

    const void* read = SDL_MapGPUTransferBuffer(device, download, false);
    if (read) {
        const float* values = static_cast<const float*>(read);
        for (u32 i = 0; i < texelCount; ++i) outAO[i] = std::clamp(values[i], 0.0f, 1.0f);
        SDL_UnmapGPUTransferBuffer(device, download);
    } else {
        ok = false;
    }

    SDL_ReleaseGPUTransferBuffer(device, download);
    SDL_ReleaseGPUBuffer(device, triangleBuffer);
    SDL_ReleaseGPUBuffer(device, positionBuffer);
    SDL_ReleaseGPUBuffer(device, normalBuffer);
    SDL_ReleaseGPUTexture(device, aoTexture);
    return ok;
}

}  // namespace

bool gpuBakeAmbientOcclusion(RHI::RenderDevice* device,
                             const Editor::PluginAOBakeDesc& desc,
                             std::vector<f32>& outAO) {
    if (!device || !device->isInitialized() || !desc.triangles || !desc.positions ||
        !desc.normals || desc.triangleCount == 0 || desc.texelCount == 0 ||
        desc.resolution == 0) {
        return false;
    }

    const RHI::ShaderBytecodeFormat format = detectShaderFormat(device);
    const ShaderBytecodeView bytecode =
        getBuiltinShaderBytecode(BuiltinShader::PhotonBakeAO, format);
    if (!bytecode.data || bytecode.size == 0) return false;

    RHI::ComputePipelineDesc pipelineDesc{};
    pipelineDesc.code = bytecode.data;
    pipelineDesc.codeSize = bytecode.size;
    pipelineDesc.entryPoint = bytecode.entryPoint;
    pipelineDesc.format = format;
    pipelineDesc.threadCountX = 8;
    pipelineDesc.threadCountY = 8;
    pipelineDesc.threadCountZ = 1;
    pipelineDesc.numReadonlyStorageBuffers = 3;
    pipelineDesc.numReadwriteStorageTextures = 1;
    pipelineDesc.numUniformBuffers = 1;

    RHI::ComputePipeline* pipeline = device->createComputePipeline(pipelineDesc);
    if (!pipeline) return false;

    std::vector<float> triangleData(static_cast<size_t>(desc.triangleCount) * 12, 0.0f);
    for (u32 t = 0; t < desc.triangleCount; ++t) {
        const float* src = desc.triangles + static_cast<size_t>(t) * 9;
        float* dst = triangleData.data() + static_cast<size_t>(t) * 12;
        std::memcpy(dst + 0, src + 0, sizeof(float) * 3);
        std::memcpy(dst + 4, src + 3, sizeof(float) * 3);
        std::memcpy(dst + 8, src + 6, sizeof(float) * 3);
    }

    BakeParams params{};
    params.counts[0] = static_cast<int>(desc.triangleCount);
    params.counts[1] = static_cast<int>(desc.texelCount);
    params.counts[2] = static_cast<int>(desc.resolution);
    params.counts[3] = static_cast<int>(std::clamp(desc.rays, 1u, 64u));
    params.params[0] = std::max(desc.maxDistance, 0.01f);
    params.params[1] = std::max(desc.bias, 0.0f);

    const bool ok = runBake(
        device->nativeDevice(), pipeline->handle, triangleData.data(),
        static_cast<u64>(triangleData.size() * sizeof(float)), desc.positions,
        static_cast<u64>(desc.texelCount) * 4 * sizeof(float), desc.normals,
        static_cast<u64>(desc.texelCount) * 4 * sizeof(float), desc.triangleCount,
        desc.texelCount, desc.resolution, params, outAO);

    device->destroyComputePipeline(pipeline);
    return ok;
}

}  // namespace Caffeine::Render
